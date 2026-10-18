#include "script_system.h"
#include <engine/animation/ecs/components/animation_component.h>
#include "engine/assets/impl/asset_extensions.h"
#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/play_mode.h>
#include <engine/loading_screen.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/profiler/profiler.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/script.h>

#include "script_value_encoding.h"


#include <dotnetpp/dotnetpp.h>


#include <core/base/platform/config.hpp>
#include <filesystem/filesystem.h>
#include <logging/logging.h>
#include <seq/seq.h>
#include <simulation/simulation.h>

#include <algorithm>
#include <array>

namespace unravel
{
namespace
{

enum class recompile_command : int
{
    none,
    compile_at_schedule,
    compile_now,
};

std::chrono::milliseconds check_interval(50);
std::atomic_bool initted{};
// How often a wait for a script build redraws the loading screen.
constexpr std::chrono::milliseconds compilation_poll_interval(16);

std::atomic<recompile_command> needs_recompile{};
std::mutex container_mutex;
std::vector<std::string> needs_to_recompile;
std::atomic<uint64_t> compilation_version{};

/// What a protocol's library was last built from. Every change queued for its sources bumps
/// source_generation and a build records the generation it read, so the library is current when
/// its last build succeeded from the latest generation. The sources' stamp also catches a file
/// saved that the watcher has not reported yet.
struct library_build
{
    uint64_t source_generation{};
    uint64_t built_generation{};
    uint64_t sources_stamp{};
    bool is_built{};
    bool is_debug{};
};

/// Guarded by container_mutex.
std::map<std::string, library_build> library_builds;

/// The result of a script build, with the loading screen drawn while it runs: a build takes
/// seconds and reports nothing on its own.
auto wait_for_compilation(rtti::context& ctx, tpp::job_future<bool> job) -> bool
{
    auto& ls = ctx.get_cached<loading_screen>();
    while(!job.is_ready())
    {
        job.wait_for(compilation_poll_interval);
        ls.refresh();
    }
    return job.get();
}

/// Caller holds container_mutex.
void mark_sources_changed(const std::string& protocol)
{
    ++library_builds[protocol].source_generation;
    // App scripts are built against the engine library: a new engine library needs a new app build.
    if(protocol == "engine")
    {
        ++library_builds["app"].source_generation;
    }
}

auto get_source_generation(const std::string& protocol) -> uint64_t
{
    std::lock_guard<std::mutex> lock(container_mutex);
    return library_builds[protocol].source_generation;
}

void record_library_build(const std::string& protocol,
                          uint64_t generation,
                          uint64_t sources_stamp,
                          bool debug,
                          bool is_built)
{
    std::lock_guard<std::mutex> lock(container_mutex);
    auto& build = library_builds[protocol];
    build.built_generation = generation;
    build.sources_stamp = sources_stamp;
    build.is_debug = debug;
    build.is_built = is_built;
}

/// A digest of the protocol's script sources as they are on disk, their paths and write times; it
/// does not depend on the order the asset manager lists them in.
auto get_sources_stamp(asset_manager& am, const std::string& protocol) -> uint64_t
{
    constexpr uint64_t mix = 0x9E3779B97F4A7C15ull;
    uint64_t stamp = 0;
    for(const auto& asset : am.get_assets<script>(protocol))
    {
        if(!asset)
        {
            continue;
        }
        const auto path = fs::resolve_protocol(asset.id());
        fs::error_code ec;
        const auto write_time = uint64_t(fs::last_write_time(path, ec).time_since_epoch().count());
        uint64_t entry = uint64_t(std::hash<std::string>{}(path.string()));
        entry ^= write_time + mix + (entry << 6) + (entry >> 2);
        stamp ^= entry * mix;
    }
    return stamp;
}

// Guards the files every build of a script library writes: the compiler output at a fixed
// temp path (the pdb stays there, so the dll's debug entry keeps pointing at it) and the temp
// compiled key it is copied to, which domain loads copy into place. A build holds it for its
// whole run and reads the sources only once it has it, so the last build to run compiles the
// latest change and nothing older overwrites it. One lock for every protocol: app builds also
// read the engine library that engine domain loads copy into place.
std::mutex library_files_mutex;

/// Builds script libraries; installed by the editor through set_library_builder, empty elsewhere.
script_system::library_builder script_library_builder;

/// Files that belong to one build of a script library dll: its debug symbols (portable pdb
/// from csc and newer mcs, mdb from older mcs) and its XML docs.
auto get_lib_companion_files(const fs::path& lib) -> std::array<fs::path, 3>
{
    auto pdb = lib;
    pdb.replace_extension(".pdb");
    auto mdb = lib;
    mdb.concat(".mdb");
    auto xml = lib;
    xml.replace_extension(".xml");
    return {pdb, mdb, xml};
}

std::atomic_bool debug_mode{true};

auto print_assembly_info(const dotnet::assembly& assembly)
{
    std::stringstream ss;
    auto refs = assembly.dump_references();

    ss << fmt::format(" ----- References -----");

    for(const auto& ref : refs)
    {
        ss << fmt::format("\n{}", ref);
    }

    APPLOG_TRACE("\n{}", ss.str());

    auto types = assembly.get_types();

    ss = {};
    ss << fmt::format(" ----- Types -----");

    for(const auto& type : types)
    {
        ss << fmt::format("\n{}", type.get_fullname());
        ss << fmt::format("\n sizeof {}", type.get_sizeof());
        ss << fmt::format("\n alignof {}", type.get_alignof());

        {
            auto attribs = type.get_attributes();
            for(const auto& attrib : attribs)
            {
                ss << fmt::format("\n - Attribute : {}", attrib.get_type().get_fullname());
            }
        }

        auto fields = type.get_fields();
        for(const auto& field : fields)
        {
            ss << fmt::format("\n - Field : {}", field.get_name());

            auto attribs = field.get_attributes();
            for(const auto& attrib : attribs)
            {
                ss << fmt::format("\n -- Attribute : {}", attrib.get_type().get_fullname());
            }
        }

        auto properties = type.get_properties();
        for(const auto& prop : properties)
        {
            ss << fmt::format("\n - Property : {}", prop.get_name());

            auto attribs = prop.get_attributes();
            for(const auto& attrib : attribs)
            {
                ss << fmt::format("\n -- Attribute : {}", attrib.get_type().get_fullname());
            }
        }
    }
    APPLOG_TRACE("\n{}", ss.str());
}

/// The timing values the managed Time class reads for a frame that advances the play clock from
/// @p elapsed by @p dt (already scaled by the time scale).
auto make_frame_update_data(const simulation& sim, delta_t elapsed, delta_t dt) -> script_system::frame_update_data
{
    const float time_scale = sim.get_time_scale();
    script_system::frame_update_data data;
    data.time = elapsed.count();
    data.delta_time = dt.count();
    data.time_scale = time_scale;
    data.frame_count = sim.get_frame();
    // From dt rather than the simulation, so a zeroed first frame and a paused frame step agree.
    data.unscaled_delta_time = time_scale > 0.0f ? dt.count() / time_scale : sim.get_unscaled_delta_time().count();
    data.realtime_since_startup = std::chrono::duration_cast<delta_t>(sim.get_time_since_launch()).count();
    return data;
}

} // namespace

void script_system::log_exception(const dotnet::exception& e, const hpp::source_location& loc)
{
    auto frame = dotnet::extract_relevant_stack_frame(e.what());
    if(!frame.file_name.empty())
    {
        APPLOG_ERROR_LOC(frame.file_name.c_str(), frame.line, frame.function_name.c_str(), e.what());
    }
    else
    {
        APPLOG_ERROR_LOC(loc.file_name(), int(loc.line()), loc.function_name(), e.what());
    }
}

void script_system::copy_compiled_lib(const fs::path& from, const fs::path& to)
{
    fs::error_code er;
    if(!fs::copy_file(from, to, fs::copy_options::overwrite_existing, er))
    {
        return;
    }
    fs::remove(from, er);

    const auto from_companions = get_lib_companion_files(from);
    const auto to_companions = get_lib_companion_files(to);
    for(size_t i = 0; i < from_companions.size(); ++i)
    {
        // A companion this build did not produce (a release build writes no symbols) must not
        // survive from an older build: the loaders pair the dll with any symbols beside it.
        if(!fs::copy_file(from_companions[i], to_companions[i], fs::copy_options::overwrite_existing, er))
        {
            fs::remove(to_companions[i], er);
        }
        fs::remove(from_companions[i], er);
    }
}

void script_system::remove_compiled_lib(const fs::path& lib)
{
    fs::error_code er;
    fs::remove(lib, er);
    for(const auto& companion : get_lib_companion_files(lib))
    {
        fs::remove(companion, er);
    }
}

auto script_system::find_dotnet_paths(const rtti::context& ctx) -> dotnet::compiler_paths
{
    bool is_deploy_mode = ctx.has<deploy>();

    dotnet::compiler_paths result;

    if(is_deploy_mode)
    {
#if DOTNETPP_BACKEND_MONO
        auto mono_dir = fs::resolve_protocol("engine:/mono");
        result.assembly_dir = fs::absolute(mono_dir / "lib").string();
        result.config_dir = fs::absolute(mono_dir / "etc").string();
#else
        fs::error_code ec;

        // The managed bridge is shipped next to the bundled dotnet root; pass
        // its location explicitly. If missing, the loader falls back to
        // probing <exe_dir>/<bridge dir> and the working directory.
        auto bridge_dir = fs::resolve_protocol("engine:/" + std::string(dotnet::managed_runtime_dir()));
        if(fs::exists(bridge_dir, ec))
        {
            result.assembly_dir = fs::absolute(bridge_dir).string();
        }

        // Self-contained deploys bundle a pruned dotnet root (hostfxr + shared
        // framework); pass it as the dotnet root override when present,
        // otherwise fall back to a machine-wide install.
        auto dotnet_dir = fs::resolve_protocol("engine:/dotnet");
        if(fs::exists(dotnet_dir, ec))
        {
            result.config_dir = fs::absolute(dotnet_dir).string();
        }
#endif
    }
    else
    {
        const auto& names = dotnet::get_common_library_names();
        const auto& library_paths = dotnet::get_common_library_paths();
        const auto& config_paths = dotnet::get_common_config_paths();

        for(size_t i = 0; i < library_paths.size(); ++i)
        {
            const auto& library_path = library_paths.at(i);
            const auto& config_path = config_paths.at(i);
            std::vector<std::string> paths{library_path};
            auto found_library = fs::find_library(names, paths);

            if(!found_library.empty())
            {
                result.assembly_dir = fs::path(library_path).make_preferred().string();
                result.config_dir = fs::path(config_path).make_preferred().string();

                break;
            }
        }
    }

    {
        const auto& names = dotnet::get_common_executable_names();
        const auto& paths = dotnet::get_common_executable_paths();

        result.msc_executable = fs::find_program(names, paths).make_preferred().string();
    }

    APPLOG_TRACE("DOTNET_PATHS:");
    APPLOG_TRACE("Assembly path - {}", result.assembly_dir);
    APPLOG_TRACE("Config path - {}", result.config_dir);

    return result;
}

/*
 * automatic: leave CoreCLR alone (default). JIT platforms use the JIT;
 * no-JIT packs (iOS) enable the interpreter themselves.
 * forced: set DOTNET_InterpMode=1 for desktop testing against an
 * interpreter-capable runtime (--interpreter forced, or
 * UNRAVEL_FORCE_DOTNET_INTERPRETER).
 */
auto select_interpreter_config(const cmd_line::parser& parser) -> dotnet::interpreter_config
{
    dotnet::interpreter_config config;
#if defined(UNRAVEL_FORCE_DOTNET_INTERPRETER)
    config.interp_mode = dotnet::interpreter_config::mode::forced;
#endif
    std::string mode;
    if(parser.try_get("interpreter", mode) && mode == "forced")
    {
        config.interp_mode = dotnet::interpreter_config::mode::forced;
    }
    return config;
}

#if !DOTNETPP_BACKEND_MONO
/*
 * auto: hostfxr when the dotnet root has one (every desktop install and the default deploy),
 * the coreclr host otherwise. coreclr: start the runtime without hostfxr, as platforms that ship
 * no hostfxr must (--clr-host coreclr exercises that path on desktop).
 */
auto select_runtime_host(const cmd_line::parser& parser) -> dotnet::runtime_host
{
    std::string host;
    parser.try_get("clr-host", host);
    if(host == "hostfxr")
    {
        return dotnet::runtime_host::hostfxr;
    }
    if(host == "coreclr")
    {
        return dotnet::runtime_host::coreclr;
    }
    return dotnet::runtime_host::automatic;
}
#endif

script_system::script_system(rtti::context& ctx, cmd_line::parser& parser)
{
    (void)ctx;
    parser.set_optional<std::string>("",
                                     "interpreter",
                                     "auto",
                                     "CoreCLR interpreter mode (auto|forced).");
    parser.set_optional<std::string>("",
                                     "clr-host",
                                     "auto",
                                     "How the .NET runtime is started (auto|hostfxr|coreclr).");
}

auto validate_paths(const dotnet::compiler_paths& paths, bool is_deploy_mode) -> bool
{
#if DOTNETPP_BACKEND_MONO
    (void)is_deploy_mode;
    return !paths.assembly_dir.empty() && !paths.config_dir.empty() && !paths.msc_executable.empty();
#else
    if(is_deploy_mode)
    {
        // Deployed games load precompiled assemblies, so no compiler is
        // required. The runtime comes from the bundled dotnet root
        // (config_dir) or, if absent, from the machine install.
        return true;
    }
    return !paths.msc_executable.empty();
#endif
}

auto script_system::init(rtti::context& ctx, const cmd_line::parser& parser) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ev = ctx.get_cached<events>();
    // Scripts run after the scene-system band on purpose: animation_system
    // rewrites every animated bone's local transform, so IK and other bone
    // writes issued from OnUpdate must come after it to survive the frame.
    // See frame_update_priority in engine/events.h.
    ev.on_frame_update.connect(sentinel_, frame_update_priority::scripts, this, &script_system::on_frame_update);
    ev.on_frame_fixed_update.connect(sentinel_, this, &script_system::on_frame_fixed_update);
    ev.on_frame_update.connect(sentinel_,
                               frame_update_priority::scripts_late,
                               this,
                               &script_system::on_frame_late_update);
    ev.on_play_begin.connect(sentinel_, -1000, this, &script_system::on_play_begin);
    ev.on_play_end.connect(sentinel_, 1000, this, &script_system::on_play_end);
    ev.on_pause.connect(sentinel_, 100, this, &script_system::on_pause);
    ev.on_resume.connect(sentinel_, -100, this, &script_system::on_resume);

    auto mono_paths = find_dotnet_paths(ctx);

    if(!validate_paths(mono_paths, ctx.has<deploy>()))
    {
#if DOTNETPP_BACKEND_MONO
        ctx.get_cached<loading_screen>().fail(
            "Failed to locate Mono C#. Please install it from - https://www.mono-project.com/download/stable/");
#else
        ctx.get_cached<loading_screen>().fail(
            "Failed to locate the .NET runtime. Please install it from - https://dotnet.microsoft.com/download");
#endif
        return false;
    }

    debug_config_.enable_debugging = true;

    dotnet::set_log_handler("info",
                          [](const std::string& msg)
                          {
                              APPLOG_INFO("{}", msg);
                          });
    dotnet::set_log_handler("trace",
                          [](const std::string& msg)
                          {
                              APPLOG_TRACE("{}", msg);
                          });
    dotnet::set_log_handler("warning",
                          [](const std::string& msg)
                          {
                              APPLOG_WARNING("{}", msg);
                          });
    dotnet::set_log_handler("error",
                          [](const std::string& msg)
                          {
                              APPLOG_ERROR("{}", msg);
                          });

#if !DOTNETPP_BACKEND_MONO
    mono_paths.host = select_runtime_host(parser);
#endif

    if(dotnet::init(mono_paths, debug_config_, select_interpreter_config(parser)))
    {
#if !DOTNETPP_BACKEND_MONO
        APPLOG_INFO(".NET runtime started through {}",
                    dotnet::get_runtime_host() == dotnet::runtime_host::coreclr ? "coreclr" : "hostfxr");
#endif
        bind_internal_calls(ctx);

        dotnet::domain::set_assemblies_path(fs::resolve_protocol(ex::get_compiled_directory("engine")).string());

        try
        {
            if(!load_engine_domain(ctx, true))
            {
                return false;
            }
        }
        catch(const dotnet::exception& e)
        {
            log_exception(e);
            return false;
        }

        initted = true;
        return true;
    }
#if DOTNETPP_BACKEND_MONO
    ctx.get_cached<loading_screen>().fail(
        "Failed to initialize Mono C#. Please install it from - https://www.mono-project.com/download/stable/");
    return false;
#else
    ctx.get_cached<loading_screen>().fail(
        "Failed to initialize the .NET runtime. Please install it from - https://dotnet.microsoft.com/download");
    return false;
#endif
}

auto script_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    unload_app_domain();
    unload_engine_domain();

    dotnet::shutdown();

    return true;
}

void script_system::set_debug_config(const std::string& address, uint32_t port, uint32_t loglevel)
{
    debug_config_.address = address;
    debug_config_.port = port;
    debug_config_.loglevel = loglevel;
}

auto script_system::load_engine_domain(rtti::context& ctx, bool recompile) -> bool
{
    APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "Load Engine Domain");

    reloads_pending_.erase("engine");

    bool is_deploy_mode = ctx.has<deploy>();

    if(!is_deploy_mode && recompile)
    {
        bool debug = false;
#ifndef NDEBUG
        debug = get_script_debug_mode();
#endif

        if(!is_library_current(ctx, "engine", debug) && !wait_for_compilation(ctx, create_compilation_job(ctx, "engine", debug)))
        {
            return false;
        }
    }

    domain_ = std::make_unique<dotnet::domain>("Unravel.Engine");
    dotnet::domain::set_current_domain(domain_.get());

    auto engine_script_lib = fs::resolve_protocol(get_lib_compiled_key("engine"));
    auto engine_script_lib_temp = fs::resolve_protocol(get_lib_temp_compiled_key("engine"));

    {
        std::lock_guard<std::mutex> lock(library_files_mutex);
        copy_compiled_lib(engine_script_lib_temp, engine_script_lib);
    }

    auto assembly = domain_->get_assembly(engine_script_lib.string());
    // print_assembly_info(assembly);

    APPLOG_TRACE("-------------------------------------------------------------");
    APPLOG_TRACE("Loading domain {} with version: {}", domain_->get_name(), domain_->get_version());
    APPLOG_TRACE("-------------------------------------------------------------");

    cache_.update_manager_type = assembly.get_type("Unravel.Core", "SystemManager");
    cache_.component_type = assembly.get_type("Unravel.Core", "Component");
    cache_.script_component_type = assembly.get_type("Unravel.Core", "ScriptComponent");
    cache_.ui_event_manager_type = assembly.get_type("Unravel.Core", "UIEventManager");

    // Cache methods once per engine domain (avoid per-call name lookup).
    cache_.begin_frame_method = cache_.update_manager_type.get_method("internal_n2m_begin_frame", 1);
    cache_.update_method = cache_.update_manager_type.get_method("internal_n2m_update", 1);
    cache_.fixed_update_method = cache_.update_manager_type.get_method("internal_n2m_fixed_update", 1);
    cache_.late_update_method = cache_.update_manager_type.get_method("internal_n2m_late_update", 0);

    // Build the frame-hook invokers once: this runs the signature check here
    // instead of on every frame (each check costs bridge round-trips).
    cache_.begin_frame_invoker = dotnet::make_method_invoker<void(frame_update_data)>(cache_.begin_frame_method);
    cache_.update_invoker = dotnet::make_method_invoker<void(frame_update_data)>(cache_.update_method);
    cache_.fixed_update_invoker =
        dotnet::make_method_invoker<void(fixed_update_data)>(cache_.fixed_update_method);
    cache_.late_update_invoker = dotnet::make_method_invoker<void()>(cache_.late_update_method);

    cache_.set_entity_method = cache_.component_type.get_method("internal_n2m_set_entity", 1);
    cache_.on_create_method = cache_.script_component_type.get_method("internal_n2m_on_create", 0);
    cache_.on_enable_method = cache_.script_component_type.get_method("internal_n2m_on_enable", 0);
    cache_.on_disable_method = cache_.script_component_type.get_method("internal_n2m_on_disable", 0);
    cache_.on_start_method = cache_.script_component_type.get_method("internal_n2m_on_start", 0);
    cache_.on_destroy_method = cache_.script_component_type.get_method("internal_n2m_on_destroy", 0);
    cache_.on_sensor_enter_method = cache_.script_component_type.get_method("internal_n2m_on_sensor_enter", 2);
    // Exits carry a contact_end_reason on top of (entity, contacts); enters do not.
    cache_.on_sensor_exit_method = cache_.script_component_type.get_method("internal_n2m_on_sensor_exit", 3);
    cache_.on_collision_enter_method =
        cache_.script_component_type.get_method("internal_n2m_on_collision_enter", 2);
    cache_.on_collision_exit_method =
        cache_.script_component_type.get_method("internal_n2m_on_collision_exit", 3);
    cache_.on_animation_event_method =
        cache_.script_component_type.get_method("internal_n2m_on_animation_event", 8);
    cache_.invoke_method = cache_.script_component_type.get_method("internal_n2m_invoke", 2);

    cache_.ui_dispatch_method = cache_.ui_event_manager_type.get_method("InternalDispatchPayload", 5);
    cache_.ui_dispatch_invoker = dotnet::make_method_invoker<ui_dispatch_signature>(cache_.ui_dispatch_method);

    cache_.ui_data_models_type = assembly.get_type("Unravel.Core", "UIDataModels");
    const auto& models = cache_.ui_data_models_type;
    cache_.ui_model_get_invoker =
        dotnet::make_method_invoker<std::string(int64_t)>(models.get_method("internal_n2m_get", 1));
    cache_.ui_model_set_invoker =
        dotnet::make_method_invoker<void(int64_t, std::string)>(models.get_method("internal_n2m_set", 2));
    cache_.ui_model_size_invoker =
        dotnet::make_method_invoker<int32_t(int64_t)>(models.get_method("internal_n2m_size", 1));
    cache_.ui_model_child_invoker =
        dotnet::make_method_invoker<int64_t(int64_t, std::string, int32_t)>(models.get_method("internal_n2m_child", 3));
    cache_.ui_model_event_invoker =
        dotnet::make_method_invoker<ui_model_event_signature>(models.get_method("internal_n2m_event", 9));
    cache_.ui_model_transform_invoker = dotnet::make_method_invoker<std::string(entt::entity, std::string, std::string)>(
        models.get_method("internal_n2m_transform", 3));
    cache_.ui_inline_event_invoker =
        dotnet::make_method_invoker<ui_inline_event_signature>(models.get_method("internal_n2m_inline_event", 7));
    cache_.ui_model_describe_invoker = dotnet::make_method_invoker<std::string(entt::entity, std::string, int32_t, int32_t)>(
        models.get_method("internal_n2m_describe", 4));

    return true;
}
void script_system::unload_engine_domain()
{
    APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "Unload Engine Domain");
    cache_ = {};
    if(domain_)
    {
        APPLOG_TRACE("-------------------------------------------------------------");
        APPLOG_TRACE("Unloading domain {} with version: {}", domain_->get_name(), domain_->get_version());
        APPLOG_TRACE("-------------------------------------------------------------");
    }
    domain_.reset();
    dotnet::domain::set_current_domain(nullptr);
}

auto script_system::load_app_domain(rtti::context& ctx, bool recompile) -> bool
{
    APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "Load App Domain");

    reloads_pending_.erase("app");

    bool is_deploy_mode = ctx.has<deploy>();

    bool result = true;

    if(!is_deploy_mode && recompile)
    {
        // Entering play mode builds every queued change first (wait_for_jobs_to_finish), so the
        // library is usually current already: reload it instead of compiling the same sources again.
        if(!is_library_current(ctx, "app", get_script_debug_mode()))
        {
            result &= wait_for_compilation(ctx, create_compilation_job(ctx, "app", get_script_debug_mode()));
        }

        has_compilation_errors_ = !result;
    }

    app_domain_ = std::make_unique<dotnet::domain>("Unravel.App");
    dotnet::domain::set_current_domain(app_domain_.get());

    auto app_script_lib = fs::resolve_protocol(get_lib_compiled_key("app"));
    auto app_script_lib_temp = fs::resolve_protocol(get_lib_temp_compiled_key("app"));

    {
        std::lock_guard<std::mutex> lock(library_files_mutex);
        copy_compiled_lib(app_script_lib_temp, app_script_lib);
    }

    if(!is_deploy_mode)
    {
        auto& am = ctx.get_cached<asset_manager>();
        auto assets = am.get_assets<script>("app");
        // assets include the empty asset
        if(assets.size() <= 1)
        {
            return result;
        }
    }

    fs::error_code ec;
    if(fs::exists(app_script_lib, ec))
    {
        APPLOG_TRACE("-------------------------------------------------------------");
        APPLOG_TRACE("Loading domain {} with version: {}", app_domain_->get_name(), app_domain_->get_version());
        APPLOG_TRACE("-------------------------------------------------------------");
        try
        {
            auto assembly = app_domain_->get_assembly(app_script_lib.string());
            // print_assembly_info(assembly);
    
            app_cache_.scriptable_component_types.clear();
            if(!has_compilation_errors_)
            {
                app_cache_.scriptable_component_types = assembly.get_types_derived_from(get_scriptable_component_base_type());
                // An abstract base (a framework's controller) cannot be added to an entity.
                auto& types = app_cache_.scriptable_component_types;
                types.erase(std::remove_if(types.begin(),
                                           types.end(),
                                           [](const auto& type)
                                           {
                                               return type.is_abstract();
                                           }),
                            types.end());

                // Same-named types in the engine and app assemblies are two
                // distinct .NET types - name-based lookups resolve app-first,
                // so make the shadowing visible instead of silent.
                auto engine_assembly = get_engine_assembly();
                for(const auto& type : app_cache_.scriptable_component_types)
                {
                    auto fullname = type.get_fullname();
                    if(engine_assembly.get_type(fullname).valid())
                    {
                        APPLOG_WARNING("Script type '{}' is defined in both the engine and the app scripts. "
                                       "The app version will be used; rename it to avoid ambiguity.",
                                       fullname);
                    }
                }
            }
        }
        catch(const dotnet::exception& e)
        {
            log_exception(e);
            result = false;
        }
    }
    

    return result;
}
void script_system::unload_app_domain()
{
    APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "Unload App Domain");

    app_cache_ = {};

    if(app_domain_)
    {
        APPLOG_TRACE("-------------------------------------------------------------");
        APPLOG_TRACE("Unloading domain {} with version: {}", app_domain_->get_name(), app_domain_->get_version());
        APPLOG_TRACE("-------------------------------------------------------------");
    }
    app_domain_.reset();
    dotnet::domain::set_current_domain(domain_.get());
}

void script_system::on_create_component(entt::registry& r, entt::entity e)
{
}
void script_system::on_load_component(entt::registry& r, entt::entity e)
{

}
void script_system::on_destroy_component(entt::registry& r, entt::entity e)
{
    auto& comp = r.get<script_component>(e);
    comp.destroy();
}

void script_system::on_create_active_component(entt::registry& r, entt::entity e)
{
    if(auto comp = r.try_get<script_component>(e))
    {
        comp->enable();
    }
}
void script_system::on_destroy_active_component(entt::registry& r, entt::entity e)
{
    if(auto comp = r.try_get<script_component>(e))
    {
        comp->disable();
    }
}

void script_system::on_play_begin(hpp::span<const entt::handle> entities)
{
    APP_SCOPE_PERF("Script/On Play Begin");
    if(!app_domain_ || !domain_)
    {
        return;
    }
    try
    {
        create_call_ = call_progress::started;

        {
            APP_SCOPE_PERF("Script/On Play Begin Create");
            for(auto entity : entities)
            {
                if(auto comp = entity.try_get<script_component>())
                {
                    comp->create();
                }
            }
        }
        
        create_call_ = call_progress::finished;

        {
            APP_SCOPE_PERF("Script/On Play Begin Enable");
            for(auto entity : entities)
            {
                if(auto comp = entity.try_get<script_component>())
                {
                    if(entity.all_of<active_component>())
                    {
                        comp->enable();
                    }
                    else
                    {
                        comp->disable();
                    }
                }
            }
        }
    }
    catch(const dotnet::exception& e)
    {
        log_exception(e);
    }
}

void script_system::on_play_begin(entt::registry& entities)
{
    APP_SCOPE_PERF("Script/On Play Begin Scene");
    if(!app_domain_ || !domain_)
    {
        return;
    }
    try
    {
        create_call_ = call_progress::started;

        {
            APP_SCOPE_PERF("Script/On Play Begin Scene Create");
            entities.view<script_component>().each(
                [&](auto e, auto&& comp)
                {
                    comp.create();
                });
        }

        create_call_ = call_progress::finished;

        {
            APP_SCOPE_PERF("Script/On Play Begin Scene Enable");
            entities.view<script_component>().each(
                [&](auto e, auto&& comp)
                {
                    if(entities.all_of<active_component>(e))
                    {
                        comp.enable();
                    }
                    else
                    {
                        comp.disable();
                    }
                });
        }
    }
    catch(const dotnet::exception& e)
    {
        log_exception(e);
    }
}

void script_system::on_play_begin(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    if(!app_domain_ || !domain_)
    {
        return;
    }

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    registry.on_construct<script_component>().connect<&on_create_component>();
    registry.on_destroy<script_component>().connect<&on_destroy_component>();
    on_load<script_component>(registry).connect<&on_load_component>();

    registry.on_construct<active_component>().connect<&on_create_active_component>();
    registry.on_destroy<active_component>().connect<&on_destroy_active_component>();

    // The play clock starts at zero before any script is created, so OnCreate does not read the
    // previous session's time.
    elapsed_time_ = {};
    if(cache_.begin_frame_invoker.valid())
    {
        try
        {
            cache_.begin_frame_invoker(make_frame_update_data(ctx.get_cached<simulation>(), elapsed_time_, {}));
        }
        catch(const dotnet::exception& e)
        {
            log_exception(e);
        }
    }

    on_play_begin(registry);
}

void script_system::on_play_end(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    seq::scope::stop_all("script");

    // try
    // {
    //     registry.view<script_component>().each(
    //         [&](auto e, auto&& comp)
    //         {
    //             comp.destroy();
    //         });
    // }
    // catch(const dotnet::exception& e)
    // {
    //     log_exception(e);
    // }

    registry.on_construct<active_component>().disconnect<&on_create_active_component>();
    registry.on_destroy<active_component>().disconnect<&on_destroy_active_component>();

    registry.on_construct<script_component>().disconnect<&on_create_component>();
    registry.on_destroy<script_component>().disconnect<&on_destroy_component>();
    on_load<script_component>(registry).disconnect<&on_load_component>();

    elapsed_time_ = {};
}

void script_system::on_pause(rtti::context& ctx)
{
}

void script_system::on_resume(rtti::context& ctx)
{
}

void script_system::on_frame_update(rtti::context& ctx, delta_t dt)
{
    APP_SCOPE_PERF("Script/System Update");

    auto& play = ctx.get_cached<play_mode>();
    if(!play.is_active())
    {
        check_for_recompile(ctx, dt, true);
    }

    is_updating_ = true;

    try
    {
        if(!app_domain_ || !domain_)
        {
            return;
        }

        auto& ec = ctx.get_cached<ecs>();
        auto& scn = ec.get_scene();
        auto& registry = *scn.registry;

        // Mid-frame enter_running: process() could not zero dt. Start() below still runs; skip
        // integrating motion on this first frame.
        if(play.frames_running() == 0)
        {
            dt = {};
        }
        const auto data = make_frame_update_data(ctx.get_cached<simulation>(), elapsed_time_, dt);

        // Time first, so the scripts created or started below read this frame's values.
        if(play.is_simulation_running() && cache_.begin_frame_invoker.valid())
        {
            cache_.begin_frame_invoker(data);
        }

        registry.view<script_component>().each(
            [&](auto e, auto&& comp)
            {
                comp.process_pending_deletions();

                if(play.is_simulation_running() && registry.all_of<active_component>(e))
                {
                    comp.start();
                }
            });

        if(play.is_simulation_running() && !play.is_simulation_paused())
        {
            dispatch_animation_events(scn);
        }

        if(play.is_simulation_running() && !play.is_simulation_paused() && cache_.update_invoker.valid())
        {
            {
                APP_SCOPE_PERF("Script/System Update Managed");
                cache_.update_invoker(data);
            }

            elapsed_time_ += dt;
        }
    }
    catch(const dotnet::exception& e)
    {
        log_exception(e);
    }
    is_updating_ = false;
}

void script_system::dispatch_animation_events(scene& scn)
{
    APP_SCOPE_PERF("Script/Animation Events");
    if(!cache_.on_animation_event_method.valid())
    {
        return;
    }

    auto& registry = *scn.registry;
    std::vector<animation_event_hit> events;
    std::vector<entt::entity> sources;
    registry.view<animation_component>().each(
        [&](auto e, auto&& comp)
        {
            if(!comp.get_player().get_fired_events().empty())
            {
                sources.push_back(e);
            }
        });

    for(auto source : sources)
    {
        auto* animation = registry.try_get<animation_component>(source);
        if(animation == nullptr)
        {
            continue;
        }
        // Copied out first: a handler may change what the player plays (or destroy the entity).
        events = animation->get_player().get_fired_events();
        animation->get_player().clear_fired_events();

        // The animated entity takes its events, or its nearest ancestor with scripts.
        entt::handle receiver(registry, source);
        while(receiver && !(receiver.all_of<script_component>() && receiver.get<script_component>().has_script_components()))
        {
            auto* transform = receiver.try_get<transform_component>();
            receiver = transform ? transform->get_parent() : entt::handle{};
        }
        if(!receiver)
        {
            continue;
        }

        for(const auto& hit : events)
        {
            auto* scripts = receiver.valid() ? receiver.try_get<script_component>() : nullptr;
            if(scripts == nullptr)
            {
                break;
            }
            scripts->on_animation_event(hit);
        }
    }
}

void script_system::on_frame_fixed_update(rtti::context& ctx, delta_t dt)
{
    APP_SCOPE_PERF("Script/System Fixed Update");

    auto& play = ctx.get_cached<play_mode>();

    try
    {
        if(!app_domain_ || !domain_)
        {
            return;
        }

        auto& ec = ctx.get_cached<ecs>();
        auto& scn = ec.get_scene();
        auto& registry = *scn.registry;

        registry.view<script_component>().each(
            [&](auto e, auto&& comp)
            {
                comp.process_pending_deletions();
            });

        if(play.is_simulation_running() && play.frames_running() > 0 && dt > delta_t::zero() &&
           cache_.fixed_update_invoker.valid())
        {
            fixed_update_data data;
            data.fixed_delta_time = dt.count();

            {
                APP_SCOPE_PERF("Script/System Fixed Update Managed");
                cache_.fixed_update_invoker(data);
            }
        }
    }
    catch(const dotnet::exception& e)
    {
        log_exception(e);
    }
}

void script_system::on_frame_late_update(rtti::context& ctx, delta_t dt)
{
    {
        APP_SCOPE_PERF("Script/System Late Update");

        auto& play = ctx.get_cached<play_mode>();
    
        try
        {
            if(!app_domain_ || !domain_)
            {
                return;
            }
    
            auto& ec = ctx.get_cached<ecs>();
            auto& scn = ec.get_scene();
            auto& registry = *scn.registry;
    
            if(play.is_simulation_running() && play.frames_running() > 0 && dt > delta_t::zero() &&
               cache_.late_update_invoker.valid())
            {
                APP_SCOPE_PERF("Script/System Late Update Managed");
                cache_.late_update_invoker();
            }
        }
        catch(const dotnet::exception& e)
        {
            log_exception(e);
        }

    }
}

auto script_system::get_all_scriptable_components() const -> const std::vector<dotnet::type>&
{
    return app_cache_.scriptable_component_types;
}

auto script_system::get_scriptable_component_base_type() const -> dotnet::type
{
    return cache_.script_component_type;
}

auto script_system::get_engine_assembly() const -> dotnet::assembly
{
    auto engine_script_lib = fs::resolve_protocol(get_lib_compiled_key("engine"));
    return domain_->get_assembly(engine_script_lib.string());
}

auto script_system::get_app_assembly() const -> dotnet::assembly
{
    auto app_script_lib = fs::resolve_protocol(get_lib_compiled_key("app"));
    return app_domain_->get_assembly(app_script_lib.string());
}

namespace
{
/// One argument as ScriptComponent.internal_n2m_invoke decodes it; numbers for a vector cross as
/// comma-separated text.
auto encode_script_argument(const script_system::script_argument& argument) -> std::string
{
    using namespace script_value_encoding;
    return std::visit(
        [](const auto& value) -> std::string
        {
            using value_t = std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<value_t, bool>)
            {
                return std::string(1, VALUE_BOOL) + (value ? "1" : "0");
            }
            else if constexpr(std::is_same_v<value_t, int64_t>)
            {
                return VALUE_INTEGER + std::to_string(value);
            }
            else if constexpr(std::is_same_v<value_t, double>)
            {
                return VALUE_NUMBER + fmt::format("{}", value);
            }
            else if constexpr(std::is_same_v<value_t, std::string>)
            {
                return VALUE_STRING + value;
            }
            else
            {
                std::string text(1, VALUE_STRING);
                for(size_t i = 0; i < value.size(); ++i)
                {
                    text += (i > 0 ? "," : "") + fmt::format("{}", value[i]);
                }
                return text;
            }
        },
        argument);
}

/// The XML documentation files of the engine and game script libraries, ';'-separated. A
/// library that was never built simply has no file; the index skips it.
auto get_api_doc_paths() -> std::string
{
    std::string paths;
    for(const char* protocol : {"engine", "app"})
    {
        auto doc = fs::resolve_protocol(script_system::get_lib_compiled_key(protocol));
        doc.replace_extension(".xml");
        paths += (paths.empty() ? "" : ";") + doc.string();
    }
    return paths;
}

/// Calls a static method of the managed API index (Unravel.Core.ApiIndex).
template<typename Signature, typename... Args>
auto invoke_api_index(const dotnet::assembly& assembly, const std::string& method_name, Args&&... args) -> std::string
{
    try
    {
        auto type = assembly.get_type("Unravel.Core", "ApiIndex");
        auto method = type.get_method(method_name, sizeof...(Args));
        auto invoker = dotnet::make_method_invoker<Signature>(method);
        return invoker(std::forward<Args>(args)...);
    }
    catch(const dotnet::exception& e)
    {
        script_system::log_exception(e);
        return std::string("The API index failed: ") + e.what();
    }
}
} // namespace

auto script_system::search_api(const std::string& query, int limit) const -> std::string
{
    if(!domain_)
    {
        return "Scripting is not loaded.";
    }
    return invoke_api_index<std::string(std::string, int, std::string)>(get_engine_assembly(),
                                                                        "internal_n2m_search",
                                                                        query,
                                                                        limit,
                                                                        get_api_doc_paths());
}

auto script_system::describe_api_type(const std::string& type_name) const -> std::string
{
    if(!domain_)
    {
        return "Scripting is not loaded.";
    }
    return invoke_api_index<std::string(std::string, std::string)>(get_engine_assembly(),
                                                                   "internal_n2m_describe_type",
                                                                   type_name,
                                                                   get_api_doc_paths());
}

auto script_system::describe_ui_models(entt::entity owner, const std::string& model, int max_depth, int max_items) const
    -> std::string
{
    if(!domain_ || !cache_.ui_model_describe_invoker.valid())
    {
        return "null";
    }
    try
    {
        auto invoker = cache_.ui_model_describe_invoker;
        return invoker(owner, model, max_depth, max_items);
    }
    catch(const dotnet::exception& e)
    {
        log_exception(e);
        return "null";
    }
}

auto script_system::invoke_script_method(const dotnet::object& script,
                                         const std::string& method,
                                         const std::vector<script_argument>& arguments) const -> std::string
{
    if(!domain_ || !cache_.invoke_method.valid())
    {
        return "null";
    }
    std::string encoded;
    for(const auto& argument : arguments)
    {
        if(!encoded.empty())
        {
            encoded += script_value_encoding::VALUE_SEPARATOR;
        }
        encoded += encode_script_argument(argument);
    }
    try
    {
        auto invoker = dotnet::make_method_invoker<std::string(std::string, std::string)>(cache_.invoke_method);
        return invoker(script, method, encoded);
    }
    catch(const dotnet::exception& e)
    {
        log_exception(e);
        return "null";
    }
}

auto script_system::get_type_by_fullname(const std::string& fullname) const -> dotnet::type
{
    // App types take precedence so user code shadows engine-provided types
    // (samples, templates) instead of silently binding to the engine copy.
    dotnet::type type;
    if(app_domain_)
    {
        type = app_domain_->get_type(fullname);
    }
    if(!type.valid() && domain_)
    {
        type = domain_->get_type(fullname);
    }

    return type;
}

auto script_system::get_type(const std::string& name_space, const std::string& name) const -> dotnet::type
{
    dotnet::type type;
    if(app_domain_)
    {
        type = app_domain_->get_type(name_space, name);
    }
    if(!type.valid() && domain_)
    {
        type = domain_->get_type(name_space, name);
    }
    return type;
}

auto script_system::is_create_called() const -> bool
{
    return create_call_ == call_progress::finished;
}
auto script_system::is_update_called() const -> bool
{
    return is_updating_;
}

auto script_system::is_debugger_attached() -> bool
{
    return dotnet::is_debugger_attached();
}

void script_system::check_for_recompile(rtti::context& ctx, delta_t dt, bool emit_callback)
{
    time_since_last_check_ += dt;

    if(time_since_last_check_ >= check_interval || needs_recompile == recompile_command::compile_now)
    {
        time_since_last_check_ = {};

        recompile_command should_recompile = needs_recompile.exchange(recompile_command::none);

        if(should_recompile != recompile_command::none)
        {
            auto container = []()
            {
                std::lock_guard<std::mutex> lock(container_mutex);
                auto result = std::move(needs_to_recompile);
                return result;
            }();

            compilation_version++;

            auto current_version = compilation_version.load();
            for(const auto& protocol : container)
            {
                if(is_compiling(protocol))
                {
                    // The running build may have read the sources before this change: build
                    // once more after it ends instead of queueing builds behind it. The version
                    // bump above already drops its now outdated reload.
                    set_needs_recompile(protocol);
                    continue;
                }

                auto job = create_compilation_job(ctx, protocol, get_script_debug_mode())
                               .then(tpp::this_thread::get_id(),
                                     [this, &ctx, protocol, emit_callback, current_version](auto f)
                                     {
                                         if(!emit_callback)
                                         {
                                             return;
                                         }
                                         auto& play = ctx.get_cached<play_mode>();
                                         auto& ev = ctx.get_cached<events>();
                                         if(play.is_simulation_running())
                                         {
                                             return;
                                         }

                                         if(compilation_version > current_version)
                                         {
                                             return;
                                         }

                                         has_compilation_errors_ = !f.get();
                                         if(!has_compilation_errors_)
                                         {
                                             reloads_pending_.insert(protocol);
                                             ev.on_script_recompile(ctx, protocol, current_version);
                                         }
                                     });

                compilation_jobs_[protocol] = std::move(job);
            }
        }
    }
}

void script_system::wait_for_jobs_to_finish(rtti::context& ctx)
{
    APPLOG_TRACE("Waiting for script compilation...");

    // Changes that arrived during a build stay pending until it ends, so finish the running
    // builds before flushing the pending ones.
    wait_for_compilation_jobs();

    check_for_recompile(ctx, 100s, false);

    wait_for_compilation_jobs();
}

auto script_system::is_build_pending(const std::string& protocol) const -> bool
{
    {
        std::lock_guard<std::mutex> lock(container_mutex);
        if(std::find(needs_to_recompile.begin(), needs_to_recompile.end(), protocol) != needs_to_recompile.end())
        {
            return true;
        }
    }
    return is_compiling(protocol) || reloads_pending_.contains(protocol);
}

auto script_system::is_compiling(const std::string& protocol) const -> bool
{
    auto it = compilation_jobs_.find(protocol);
    return it != compilation_jobs_.end() && it->second.valid() && !it->second.is_ready();
}

void script_system::wait_for_compilation_jobs()
{
    auto jobs = std::exchange(compilation_jobs_, {});

    for(auto& [protocol, job] : jobs)
    {
        job.wait();
    }
}

auto script_system::create_compilation_job(rtti::context& ctx,
                                           const std::string& protocol,
                                           bool debug) -> tpp::job_future<bool>
{
    uint32_t flags = 0;
    if(debug)
    {
        flags |= script_library::compilation_flags::debug;
    }

    auto& thr = ctx.get_cached<threader>();
    auto& am = ctx.get_cached<asset_manager>();

    return thr.pool->schedule(
        tpp::job_desc{"Compiling " + ex::get_type<script_library>(), protocol},
        [&am, flags, protocol, debug, builder = script_library_builder]()
        {
            auto key = get_lib_data_key(protocol);
            auto output = get_lib_temp_compiled_key(protocol);

            std::lock_guard<std::mutex> lock(library_files_mutex);
            // Taken once the build owns the files, just before it reads the sources: a change
            // queued or saved after this point leaves the library out of date.
            const uint64_t generation = get_source_generation(protocol);
            const uint64_t sources_stamp = get_sources_stamp(am, protocol);
            if(!builder)
            {
                APPLOG_ERROR("The {} script library cannot be built: no script compiler is installed.", protocol);
            }
            const bool is_built = builder && builder(am, key, fs::resolve_protocol(output), flags);
            record_library_build(protocol, generation, sources_stamp, debug, is_built);
            return is_built;
        });
}

auto script_system::is_library_current(rtti::context& ctx, const std::string& protocol, bool debug) -> bool
{
    uint64_t sources_stamp = 0;
    {
        std::lock_guard<std::mutex> lock(container_mutex);
        const auto it = library_builds.find(protocol);
        if(it == library_builds.end() || !it->second.is_built || it->second.is_debug != debug ||
           it->second.built_generation != it->second.source_generation)
        {
            return false;
        }
        sources_stamp = it->second.sources_stamp;
    }
    if(get_sources_stamp(ctx.get_cached<asset_manager>(), protocol) != sources_stamp)
    {
        return false;
    }
    // A build waits at its temp key until a domain load moves it to the compiled key.
    fs::error_code ec;
    return fs::exists(fs::resolve_protocol(get_lib_temp_compiled_key(protocol)), ec) ||
           fs::exists(fs::resolve_protocol(get_lib_compiled_key(protocol)), ec);
}

void script_system::set_library_builder(library_builder builder)
{
    script_library_builder = std::move(builder);
}

void script_system::set_needs_recompile(const std::string& protocol, bool now)
{
    if(!initted)
    {
        return;
    }
    // Queue first, then raise the flag: check_for_recompile takes the queue once it sees the flag, so a
    // flag raised first can be taken with the queue still empty, leaving the protocol queued unflagged.
    {
        std::lock_guard<std::mutex> lock(container_mutex);
        mark_sources_changed(protocol);
        if(std::find(std::begin(needs_to_recompile), std::end(needs_to_recompile), protocol) ==
           std::end(needs_to_recompile))
        {
            needs_to_recompile.emplace_back(protocol);
        }
    }
    needs_recompile = now ? recompile_command::compile_now : recompile_command::compile_at_schedule;
}

auto script_system::get_script_debug_mode() -> bool
{
    return debug_mode;
}

void script_system::set_script_debug_mode(bool debug)
{
    debug_mode = debug;
}

auto script_system::get_lib_name(const std::string& protocol) -> std::string
{
    return protocol + "-script.dll";
}

auto script_system::get_lib_data_key(const std::string& protocol) -> std::string
{
    std::string output = get_lib_name(protocol + ex::get_data_directory() + "/" + protocol);
    return output;
}

auto script_system::get_lib_temp_compiled_key(const std::string& protocol) -> std::string
{
    std::string output = get_lib_name(protocol + ex::get_compiled_directory() + "/temp-" + protocol);
    return output;
}

auto script_system::get_lib_compiled_key(const std::string& protocol) -> std::string
{
    std::string output = get_lib_name(protocol + ex::get_compiled_directory() + "/" + protocol);
    return output;
}

void script_system::on_sensor_enter(entt::handle sensor, entt::handle other, const std::vector<manifold_point>& manifolds)
{
    if(!other || !sensor)
    {
        return;
    }
    auto comp = sensor.try_get<script_component>();
    if(!comp)
    {
        return;
    }

    try
    {
        comp->on_sensor_enter(other, manifolds);
    }
    catch(const dotnet::exception& e)
    {
        log_exception(e);
    }
}

void script_system::on_sensor_exit(entt::handle sensor,
                                   entt::handle other,
                                   const std::vector<manifold_point>& manifolds,
                                   contact_end_reason reason)
{
    // Exits synthesized at removal run in the pre-destroy phase, so both sides are
    // still valid here. This guard only catches genuinely stale pairs.
    if(!other || !sensor)
    {
        return;
    }

    auto comp = sensor.try_get<script_component>();
    if(!comp)
    {
        return;
    }

    try
    {
        comp->on_sensor_exit(other, manifolds, reason);
    }
    catch(const dotnet::exception& e)
    {
        log_exception(e);
    }
}

void script_system::on_collision_enter(entt::handle a, entt::handle b, const std::vector<manifold_point>& manifolds)
{
    if(!a || !b)
    {
        return;
    }

    try
    {
        {
            auto comp = a.try_get<script_component>();
            if(comp)
            {
                comp->on_collision_enter(b, manifolds, true);
            }
        }

        {
            auto comp = b.try_get<script_component>();
            if(comp)
            {
                comp->on_collision_enter(a, manifolds, false);
            }
        }
    }
    catch(const dotnet::exception& e)
    {
        log_exception(e);
    }
}

void script_system::on_collision_exit(entt::handle a,
                                      entt::handle b,
                                      const std::vector<manifold_point>& manifolds,
                                      contact_end_reason reason)
{
    if(!a || !b)
    {
        return;
    }

    // The reason is stated from the receiver's point of view, so the side that is
    // going away sees "self", the survivor sees "other".
    const auto reason_for_a = reason;
    const auto reason_for_b = mirror_contact_end_reason(reason);

    try
    {
        {
            auto comp = a.try_get<script_component>();
            if(comp)
            {
                comp->on_collision_exit(b, manifolds, true, reason_for_a);
            }
        }

        {
            auto comp = b.try_get<script_component>();
            if(comp)
            {
                comp->on_collision_exit(a, manifolds, false, reason_for_b);
            }
        }
    }
    catch(const dotnet::exception& e)
    {
        log_exception(e);
    }
}

auto script_system::has_compilation_errors() const -> bool
{
    return has_compilation_errors_;
}

} // namespace unravel
