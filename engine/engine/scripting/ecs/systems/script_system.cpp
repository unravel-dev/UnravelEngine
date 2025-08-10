#include "script_system.h"
#include "engine/assets/impl/asset_extensions.h"
#include <engine/assets/impl/asset_compiler.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/script.h>
#include <engine/profiler/profiler.h>

#include <monopp/mono_exception.h>
#include <monopp/mono_field_invoker.h>
#include <monopp/mono_internal_call.h>
#include <monopp/mono_method_invoker.h>
#include <monopp/mono_property_invoker.h>

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

auto print_assembly_info(const mono::mono_assembly& assembly)
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

void script_system::log_exception(const mono::mono_exception& e, const hpp::source_location& loc)
{
    auto frame = mono::extract_relevant_stack_frame(e.what());
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

auto script_system::find_mono(const rtti::context& ctx) -> mono::compiler_paths
{
    bool is_deploy_mode = ctx.has<deploy>();

    mono::compiler_paths result;

    if(is_deploy_mode)
    {
        auto mono_dir = fs::resolve_protocol("engine:/mono");
        result.assembly_dir = fs::absolute(mono_dir / "lib").string();
        result.config_dir = fs::absolute(mono_dir / "etc").string();
    }
    else
    {
        const auto& names = mono::get_common_library_names();
        const auto& library_paths = mono::get_common_library_paths();
        const auto& config_paths = mono::get_common_config_paths();

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
        const auto& names = mono::get_common_executable_names();
        const auto& paths = mono::get_common_executable_paths();

        result.msc_executable = fs::find_program(names, paths).make_preferred().string();
    }

    APPLOG_TRACE("MONO_PATHS:");
    APPLOG_TRACE("Assembly path - {}", result.assembly_dir);
    APPLOG_TRACE("Config path - {}", result.config_dir);

    return result;
}

auto validate_paths(const mono::compiler_paths& paths) -> bool
{
    return !paths.assembly_dir.empty() && !paths.config_dir.empty();
}

auto script_system::init(rtti::context& ctx) -> bool
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

    auto mono_paths = find_mono(ctx);

    if(!validate_paths(mono_paths))
    {
        auto& error = ctx.add<init_error>();
        error.category = "Mono C#";
        error.msg = "Failed to locate Mono C#. Please install it from - https://www.mono-project.com/download/stable/";

        return false;
    }

    debug_config_.enable_debugging = true;

    if(mono::init(mono_paths, debug_config_))
    {
        bind_internal_calls(ctx);

        mono::mono_domain::set_assemblies_path(fs::resolve_protocol(ex::get_compiled_directory("engine")).string());

        try
        {
            if(!load_engine_domain(ctx, true))
            {
                return false;
            }
        }
        catch(const mono::mono_exception& e)
        {
            log_exception(e);
            return false;
        }

        initted = true;
        return true;
    }

    auto& error = ctx.add<init_error>();
    error.category = "Mono C#";
    error.msg = "Failed to initialize Mono C#. Please install it from - https://www.mono-project.com/download/stable/";

    return false;
}

auto script_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    unload_app_domain();
    unload_engine_domain();

    mono::shutdown();

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
        debug = true;
#endif

        if(!create_compilation_job(ctx, "engine", debug).get())
        {
            return false;
        }
    }

    domain_ = std::make_unique<mono::mono_domain>("Ace.Engine");
    mono::mono_domain::set_current_domain(domain_.get());

    auto engine_script_lib = fs::resolve_protocol(get_lib_compiled_key("engine"));
    auto engine_script_lib_temp = fs::resolve_protocol(get_lib_temp_compiled_key("engine"));

    copy_compiled_lib(engine_script_lib_temp, engine_script_lib);

    auto assembly = domain_->get_assembly(engine_script_lib.string());
    // print_assembly_info(assembly);

    cache_.update_manager_type = assembly.get_type("Ace.Core", "SystemManager");

    return true;
}
void script_system::unload_engine_domain()
{
    cache_ = {};
    domain_.reset();
    mono::mono_domain::set_current_domain(nullptr);
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

    app_domain_ = std::make_unique<mono::mono_domain>("Ace.App");
    mono::mono_domain::set_current_domain(app_domain_.get());

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

    try
    {
        auto assembly = app_domain_->get_assembly(app_script_lib.string());
        // print_assembly_info(assembly);

        auto engine_script_lib = fs::resolve_protocol(get_lib_compiled_key("engine"));
        auto engine_assembly = domain_->get_assembly(engine_script_lib.string());

        auto comp_type = engine_assembly.get_type("Ace.Core", "ScriptComponent");
        app_cache_.scriptable_component_types = assembly.get_types_derived_from(comp_type);
    }
    catch(const mono::mono_exception& e)
    {
        log_exception(e);
        result = false;
    }

    return result;
}
void script_system::unload_app_domain()
{
    app_cache_ = {};
    app_domain_.reset();
    mono::mono_domain::set_current_domain(domain_.get());
}

void script_system::on_create_component(entt::registry& r, entt::entity e)
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
    if(!app_domain_ || !domain_)
    {
        return;
    }
    try
    {
        create_call_ = call_progress::started;

        for(auto entity : entities)
        {
            if(auto comp = entity.try_get<script_component>())
            {
                comp->create();
            }
        }
        create_call_ = call_progress::finished;

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
    catch(const mono::mono_exception& e)
    {
        log_exception(e);
    }
}

void script_system::on_play_begin(entt::registry& entities)
{
    if(!app_domain_ || !domain_)
    {
        return;
    }
    try
    {
        create_call_ = call_progress::started;

        entities.view<script_component>().each(
            [&](auto e, auto&& comp)
            {
                comp.create();
            });

        create_call_ = call_progress::finished;

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
    catch(const mono::mono_exception& e)
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

    registry.on_construct<active_component>().connect<&on_create_active_component>();
    registry.on_destroy<active_component>().connect<&on_destroy_active_component>();

    on_play_begin(registry);
}

void script_system::on_play_end(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    seq::scope::stop_all("script");

    try
    {
        registry.view<script_component>().each(
            [&](auto e, auto&& comp)
            {
                comp.destroy();
            });
    }
    catch(const mono::mono_exception& e)
    {
        log_exception(e);
    }

    registry.on_construct<active_component>().disconnect<&on_create_active_component>();
    registry.on_destroy<active_component>().disconnect<&on_destroy_active_component>();

    registry.on_construct<script_component>().disconnect<&on_create_component>();
    registry.on_destroy<script_component>().disconnect<&on_destroy_component>();
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

    auto& ev = ctx.get_cached<events>();
    if(!ev.is_playing)
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

                if(ev.is_playing && registry.all_of<active_component>(e))
                {
                    comp.start();
                }
            });

        struct update_data
        {
            float delta_time{};
            float time_scale{};
            uint64_t frame_count{};
        };

        if(ev.is_playing)
        {
            auto& sim = ctx.get_cached<simulation>();
            auto time_scale = sim.get_time_scale();

            update_data data;
            data.delta_time = dt.count();
            data.time_scale = time_scale;
            data.frame_count = sim.get_frame();
            auto method_thunk =
                mono::make_method_invoker<void(update_data)>(cache_.update_manager_type, "internal_n2m_update");
            method_thunk(data);
        }
    }
    catch(const mono::mono_exception& e)
    {
        log_exception(e);
    }
    is_updating_ = false;
}

void script_system::on_frame_fixed_update(rtti::context& ctx, delta_t dt)
{
    //APP_SCOPE_PERF("Script/System Fixed Update");

    auto& ev = ctx.get_cached<events>();

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

        if(ev.is_playing && dt > delta_t::zero())
        {
            auto& sim = ctx.get_cached<simulation>();
            auto time_scale = sim.get_time_scale();

            update_data data;
            data.fixed_delta_time = dt.count();
            auto method_thunk =
                mono::make_method_invoker<void(update_data)>(cache_.update_manager_type, "internal_n2m_fixed_update");
            method_thunk(data);
        }
    }
    catch(const mono::mono_exception& e)
    {
        log_exception(e);
    }
}

void script_system::on_frame_late_update(rtti::context& ctx, delta_t dt)
{
    APP_SCOPE_PERF("Script/System Late Update");

    auto& ev = ctx.get_cached<events>();

    try
    {
        if(!app_domain_ || !domain_)
        {
            return;
        }

        auto& ec = ctx.get_cached<ecs>();
        auto& scn = ec.get_scene();
        auto& registry = *scn.registry;

        if(ev.is_playing && dt > delta_t::zero())
        {
            auto method_thunk =
                mono::make_method_invoker<void()>(cache_.update_manager_type, "internal_n2m_late_update");
            method_thunk();
        }
    }
    catch(const mono::mono_exception& e)
    {
        log_exception(e);
    }
}

auto script_system::get_all_scriptable_components() const -> const std::vector<mono::mono_type>&
{
    return app_cache_.scriptable_component_types;
}

auto script_system::get_engine_assembly() const -> mono::mono_assembly
{
    auto engine_script_lib = fs::resolve_protocol(get_lib_compiled_key("engine"));
    return domain_->get_assembly(engine_script_lib.string());
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
    return mono::is_debugger_attached();
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
                                         auto& ev = ctx.get_cached<events>();

                                         if(ev.is_playing)
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

auto script_system::create_compilation_job(rtti::context& ctx, const std::string& protocol, bool debug)
    -> tpp::job_future<bool>
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

void script_system::on_sensor_enter(entt::handle sensor, entt::handle other)
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
        comp->on_sensor_enter(other);
    }
    catch(const mono::mono_exception& e)
    {
        log_exception(e);
    }
}

void script_system::on_sensor_exit(entt::handle sensor, entt::handle other)
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
        comp->on_sensor_exit(other);
    }
    catch(const mono::mono_exception& e)
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
    catch(const mono::mono_exception& e)
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
    catch(const mono::mono_exception& e)
    {
        log_exception(e);
    }
}

auto script_system::has_compilation_errors() const -> bool
{
    return has_compilation_errors_;
}

} // namespace unravel
