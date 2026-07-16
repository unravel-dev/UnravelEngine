#include "script_system.h"
#include "engine/assets/impl/asset_extensions.h"
#include <engine/assets/impl/asset_compiler.h>
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


#include <dotnetpp/dotnetpp.h>


#include <core/base/platform/config.hpp>
#include <filesystem/filesystem.h>
#include <logging/logging.h>
#include <seq/seq.h>
#include <simulation/simulation.h>

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

std::atomic<recompile_command> needs_recompile{};
std::mutex container_mutex;
std::vector<std::string> needs_to_recompile;
std::atomic<uint64_t> compilation_version{};

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
    auto from_debug_info = from;
    from_debug_info.concat(".mdb");
    auto from_comments_xml = from;
    from_comments_xml.replace_extension(".xml");

    auto to_debug_info = to;
    to_debug_info.concat(".mdb");
    auto to_comments_xml = to;
    to_comments_xml.replace_extension(".xml");

    fs::error_code er;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, er);
    fs::copy_file(from_debug_info, to_debug_info, fs::copy_options::overwrite_existing, er);
    fs::copy_file(from_comments_xml, to_comments_xml, fs::copy_options::overwrite_existing, er);

    fs::remove(from, er);
    fs::remove(from_debug_info, er);
    fs::remove(from_comments_xml, er);
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
 * UNRAVEL_FORCE_CORECLR_INTERPRETER).
 */
auto select_interpreter_config(const cmd_line::parser& parser) -> dotnet::interpreter_config
{
    dotnet::interpreter_config config;
#if defined(UNRAVEL_FORCE_CORECLR_INTERPRETER)
    config.interp_mode = dotnet::interpreter_config::mode::forced;
#endif
    std::string mode;
    if(parser.try_get("interpreter", mode) && mode == "forced")
    {
        config.interp_mode = dotnet::interpreter_config::mode::forced;
    }
    return config;
}

script_system::script_system(rtti::context& ctx, cmd_line::parser& parser)
{
    (void)ctx;
    parser.set_optional<std::string>("",
                                     "interpreter",
                                     "auto",
                                     "CoreCLR interpreter mode (auto|forced).");
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
    ev.on_frame_update.connect(sentinel_, this, &script_system::on_frame_update);
    ev.on_frame_fixed_update.connect(sentinel_, this, &script_system::on_frame_fixed_update);
    ev.on_frame_update.connect(sentinel_, -100000, this, &script_system::on_frame_late_update);
    ev.on_play_begin.connect(sentinel_, -1000, this, &script_system::on_play_begin);
    ev.on_play_end.connect(sentinel_, 1000, this, &script_system::on_play_end);
    ev.on_pause.connect(sentinel_, 100, this, &script_system::on_pause);
    ev.on_resume.connect(sentinel_, -100, this, &script_system::on_resume);
    ev.on_skip_next_frame.connect(sentinel_, -100, this, &script_system::on_skip_next_frame);

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

    if(dotnet::init(mono_paths, debug_config_, select_interpreter_config(parser)))
    {
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
    bool is_deploy_mode = ctx.has<deploy>();

    if(!is_deploy_mode && recompile)
    {
        bool debug = false;
#ifndef NDEBUG
        debug = get_script_debug_mode();
#endif

        if(!create_compilation_job(ctx, "engine", debug).get())
        {
            return false;
        }
    }

    domain_ = std::make_unique<dotnet::domain>("Unravel.Engine");
    dotnet::domain::set_current_domain(domain_.get());

    auto engine_script_lib = fs::resolve_protocol(get_lib_compiled_key("engine"));
    auto engine_script_lib_temp = fs::resolve_protocol(get_lib_temp_compiled_key("engine"));

    copy_compiled_lib(engine_script_lib_temp, engine_script_lib);

    auto assembly = domain_->get_assembly(engine_script_lib.string());
    // print_assembly_info(assembly);

    APPLOG_TRACE("------------------------------------------------");
    APPLOG_TRACE("Loading domain {} with version: {}", domain_->get_name(), reinterpret_cast<intptr_t>(domain_->get_internal_ptr()));
    APPLOG_TRACE("------------------------------------------------");

    cache_.update_manager_type = assembly.get_type("Unravel.Core", "SystemManager");

    // Cache methods to avoid repeated allocations every frame
    cache_.update_method = cache_.update_manager_type.get_method("internal_n2m_update", 1);
    cache_.fixed_update_method = cache_.update_manager_type.get_method("internal_n2m_fixed_update", 1);
    cache_.late_update_method = cache_.update_manager_type.get_method("internal_n2m_late_update", 0);

    return true;
}
void script_system::unload_engine_domain()
{
    cache_ = {};
    if(domain_)
    {
        auto domain_version = reinterpret_cast<intptr_t>(domain_->get_internal_ptr());
        APPLOG_TRACE("-------------------------------------------------------");
        APPLOG_TRACE("Unloading domain {} with version: {}", domain_->get_name(), domain_version);
        APPLOG_TRACE("-------------------------------------------------------");
    }
    domain_.reset();
    dotnet::domain::set_current_domain(nullptr);
}

auto script_system::load_app_domain(rtti::context& ctx, bool recompile) -> bool
{
    bool is_deploy_mode = ctx.has<deploy>();

    bool result = true;

    if(!is_deploy_mode && recompile)
    {
        result &= create_compilation_job(ctx, "app", get_script_debug_mode()).get();

        has_compilation_errors_ = !result;
    }

    app_domain_ = std::make_unique<dotnet::domain>("Unravel.App");
    dotnet::domain::set_current_domain(app_domain_.get());

    auto app_script_lib = fs::resolve_protocol(get_lib_compiled_key("app"));
    auto app_script_lib_temp = fs::resolve_protocol(get_lib_temp_compiled_key("app"));

    copy_compiled_lib(app_script_lib_temp, app_script_lib);

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
        APPLOG_TRACE("------------------------------------------------");
        APPLOG_TRACE("Loading domain {} with version: {}", app_domain_->get_name(), reinterpret_cast<intptr_t>(app_domain_->get_internal_ptr()));
        APPLOG_TRACE("------------------------------------------------");
        try
        {
            auto assembly = app_domain_->get_assembly(app_script_lib.string());
            // print_assembly_info(assembly);
    
            app_cache_.scriptable_component_types.clear();
            if(!has_compilation_errors_)
            {
                app_cache_.scriptable_component_types = assembly.get_types_derived_from(get_scriptable_component_base_type());

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
    app_cache_ = {};

    if(app_domain_)
    {
        auto domain_version = reinterpret_cast<intptr_t>(app_domain_->get_internal_ptr());
        APPLOG_TRACE("------------------------------------------------");
        APPLOG_TRACE("Unloading domain {} with version: {}", app_domain_->get_name(), domain_version);
        APPLOG_TRACE("------------------------------------------------");
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

    on_play_begin(registry);

    elapsed_time_ = {};
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

void script_system::on_skip_next_frame(rtti::context& ctx)
{
    delta_t step(1.0f / 60.0f);
    on_frame_update(ctx, step);
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

        registry.view<script_component>().each(
            [&](auto e, auto&& comp)
            {
                comp.process_pending_deletions();

                if(play.is_simulation_running() && registry.all_of<active_component>(e))
                {
                    comp.start();
                }
            });

        struct update_data
        {
            float time{};
            float delta_time{};
            float time_scale{};
            uint64_t frame_count{};
        };

        if(play.is_simulation_running() && !play.is_paused())
        {
            auto& sim = ctx.get_cached<simulation>();
            auto time_scale = sim.get_time_scale();

            update_data data;
            data.time = elapsed_time_.count();
            data.delta_time = dt.count();
            data.time_scale = time_scale;
            data.frame_count = sim.get_frame();

            {
                APP_SCOPE_PERF("Script/System Update Managed");
                // Use cached method to avoid repeated allocations
                auto method_thunk = dotnet::make_method_invoker<void(update_data)>(cache_.update_method);
                method_thunk(data);
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

        struct update_data
        {
            float fixed_delta_time{};
        };

        if(play.is_simulation_running() && dt > delta_t::zero())
        {
            auto& sim = ctx.get_cached<simulation>();
            auto time_scale = sim.get_time_scale();

            update_data data;
            data.fixed_delta_time = dt.count();

            {
                APP_SCOPE_PERF("Script/System Fixed Update Managed");
                // Use cached method to avoid repeated allocations
                auto method_thunk = dotnet::make_method_invoker<void(update_data)>(cache_.fixed_update_method);
                method_thunk(data);
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
    
            if(play.is_simulation_running() && dt > delta_t::zero())
            {
                APP_SCOPE_PERF("Script/System Late Update Managed");
                // Use cached method to avoid repeated allocations
                auto method_thunk = dotnet::make_method_invoker<void()>(cache_.late_update_method);
                method_thunk();
            }
        }
        catch(const dotnet::exception& e)
        {
            log_exception(e);
        }
    
    }
    
    {
        APP_SCOPE_PERF("Script/System Cleanup");
        dt = std::max(delta_t::zero(), dt);

        delta_t secs(dt);
        seq::update(secs);
    }

    
}

auto script_system::get_all_scriptable_components() const -> const std::vector<dotnet::type>&
{
    return app_cache_.scriptable_component_types;
}

auto script_system::get_scriptable_component_base_type() const -> dotnet::type
{
    auto comp_type = get_engine_assembly().get_type("Unravel.Core", "ScriptComponent");
    return comp_type;
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

            compilation_jobs_.clear();

            compilation_version++;

            auto current_version = compilation_version.load();
            for(const auto& protocol : container)
            {
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
                                             ev.on_script_recompile(ctx, protocol, current_version);
                                         }
                                     });

                compilation_jobs_.emplace_back(std::move(job));
            }
        }
    }
}

void script_system::wait_for_jobs_to_finish(rtti::context& ctx)
{
    APPLOG_TRACE("Waiting for script compilation...");

    check_for_recompile(ctx, 100s, false);

    auto jobs = std::move(compilation_jobs_);

    for(auto& job : jobs)
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
        "Compiling " + ex::get_type<script_library>(),
        [&am, flags, protocol]()
        {
            auto key = get_lib_data_key(protocol);
            auto output = get_lib_temp_compiled_key(protocol);

            return asset_compiler::compile<script_library>(am, key, fs::resolve_protocol(output), flags);
        });
}
void script_system::set_needs_recompile(const std::string& protocol, bool now)
{
    if(!initted)
    {
        return;
    }
    needs_recompile = now ? recompile_command::compile_now : recompile_command::compile_at_schedule;
    {
        std::lock_guard<std::mutex> lock(container_mutex);
        if(std::find(std::begin(needs_to_recompile), std::end(needs_to_recompile), protocol) ==
           std::end(needs_to_recompile))
        {
            needs_to_recompile.emplace_back(protocol);
        }
    }
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

void script_system::on_sensor_exit(entt::handle sensor, entt::handle other, const std::vector<manifold_point>& manifolds)
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
        comp->on_sensor_exit(other, manifolds);
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

void script_system::on_collision_exit(entt::handle a, entt::handle b, const std::vector<manifold_point>& manifolds)
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
                comp->on_collision_exit(b, manifolds, true);
            }
        }

        {
            auto comp = b.try_get<script_component>();
            if(comp)
            {
                comp->on_collision_exit(a, manifolds, false);
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
