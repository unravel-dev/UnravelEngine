#include "engine.h"
#include <engine/assets/asset_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/loading_screen.h>
#include <engine/play_mode.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/renderer.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/threading/threader.h>
#include <engine/ui/ecs/systems/ui_system.h>

#include <engine/ecs/ecs.h>

#include "events.h"
#include <engine/settings/boot_config.h>
#include <engine/animation/ecs/systems/animation_system.h>
#include <engine/animation/ecs/systems/rig_system.h>
#include <engine/audio/ecs/systems/audio_system.h>
#include <engine/ecs/systems/transform_system.h>
#include <engine/input/input.h>
#include <engine/physics/ecs/systems/physics_system.h>
#include <engine/rendering/ecs/systems/camera_system.h>
#include <engine/rendering/ecs/systems/model_system.h>
#include <engine/rendering/ecs/systems/reflection_probe_system.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/ecs/systems/skylight_system.h>
#include <engine/rendering/ecs/systems/particle_system.h>
#include <ospp/event.h>
#include <ospp/paths.h>

#include <base/platform/config.hpp>
#include <crash/crash.hpp>
#include <cstdlib>
#include <exception>
#include <logging/logging.h>
#include <service/service.h>


#include <seq/seq.h>
#include <simulation/simulation.h>

#include <filesystem/filesystem.h>
#include <raytracing/backend.h>
namespace unravel
{
namespace
{

// Written into the binary folder, never into the launching shell's current directory.
constexpr const char* LOG_FILE_NAME = "Log.txt";
constexpr const char* CRASH_LOG_FILE_NAME = "CrashLog.txt";
/// The folder of user:/ inside binary:/ until a game names its own, and inside the internal storage
/// on Android, where the backup rules (res/xml of the Android player) name it too.
constexpr const char* USER_FOLDER_NAME = "user";
/// The folder of cache:/ inside binary:/ until a game names its own.
constexpr const char* CACHE_FOLDER_NAME = ".cache";

/// The binary folder (binary:/): the application's own files, and where the engine writes its log,
/// crash log and caches. On desktop it is the executable's folder. An Android application runs in
/// the system's app_process, whose folder is read-only, so its internal storage takes that place.
auto get_binary_path(const cmd_line::parser& parser) -> fs::path
{
#if UNRAVEL_PLATFORM_ANDROID
    (void)parser;
    return fs::path(os::paths::get_preferences_path({}, {})).parent_path();
#else
    return fs::executable_path(parser.app_name().c_str()).parent_path();
#endif
}

auto context_ptr() -> rtti::context*&
{
    static rtti::context* ctx{};
    return ctx;
}
std::atomic<bool> is_shutting_down{false};
std::atomic<bool> is_restart_requested{false};

void update_input_zone(const renderer& rend, input_system& input)
{
    const auto& window = rend.get_main_window();
    if(window)
    {
        auto main_pos = window->get_window().get_position();
        auto main_size = window->get_window().get_size();

        input::zone window_zone{};
        window_zone.x = main_pos.x;
        window_zone.y = main_pos.y;
        window_zone.w = int(main_size.w);
        window_zone.h = int(main_size.h);

        input.manager.set_window_zone(window_zone);
    }
}

/// Shows or hides the OS cursor as the game asks. A host that draws its own UI (the editor) sets
/// the cursor every frame and follows the input manager itself.
void update_cursor_visibility(const renderer& rend, input_system& input)
{
    const auto& window = rend.get_main_window();
    if(!window)
    {
        return;
    }
    if(const auto is_shown = input.manager.get_cursor().take_visibility_change())
    {
        window->get_window().show_cursor(*is_shown);
    }
}

// Engine crash handlers
void engine_interrupt_handler(const crash::signal_info& info)
{
    APPLOG_INFO("User interrupt ({}) -> {}", info.signal_number, info.signal_name);

    engine::interrupt();

    APPLOG_FLUSH();
}

void engine_termination_handler(const crash::signal_info& info)
{
    APPLOG_INFO("Termination signal ({}) -> {}", info.signal_number, info.signal_name);

    // Try to gracefully shutdown the engine
    engine::interrupt();

    APPLOG_FLUSH();
}

void engine_crash_handler(const crash::signal_info& info, const crash::trace_info& trace)
{
    // Best-effort only: CrashLog.txt / minidump are already written by the crash module
    // with async-signal/SEH-safe I/O. spdlog may deadlock if the faulting thread holds a lock.
    try
    {
        APPLOG_CRITICAL("Crash ({:#x}) -> {}\n{}",
                        static_cast<unsigned>(info.signal_number),
                        info.signal_name,
                        trace.formatted_trace);
        APPLOG_FLUSH();
    }
    catch(...)
    {
    }
    engine::interrupt();

    APPLOG_FLUSH();
}

void engine_exception_handler(const crash::exception_info& info, const crash::trace_info& trace)
{
    try
    {
        APPLOG_CRITICAL("{}\n{}", info.exception_message, trace.formatted_trace);
        APPLOG_FLUSH();
    }
    catch(...)
    {
    }
    engine::interrupt();

    APPLOG_FLUSH();
}

/// Application lifecycle events, as the platform reports them. On Android the event pump blocks
/// right after the application enters the background, so the systems react here, not in the
/// frame loop, which sees these events only after the application returns.
void on_app_event(os::events type)
{
    auto* ctx = engine::try_context();
    if(ctx == nullptr)
    {
        return;
    }
    auto& ev = ctx->get_cached<events>();
    switch(type)
    {
        case os::events::app_will_enter_background:
            APPLOG_INFO("The application enters the background.");
            ev.on_app_enter_background(*ctx);
            APPLOG_FLUSH();
            break;
        case os::events::app_did_enter_foreground:
            APPLOG_INFO("The application is back in the foreground.");
            // The time spent in the background is not a frame.
            ctx->get_cached<simulation>().reset_delta_clock();
            ev.on_app_enter_foreground(*ctx);
            break;
        case os::events::app_low_memory:
            // Android reports every memory trim request this way, the routine one on entering the
            // background included.
            APPLOG_INFO("The system asks the application to use less memory.");
            ev.on_app_low_memory(*ctx);
            break;
        case os::events::app_terminating:
            APPLOG_INFO("The system ends the application.");
            engine::interrupt();
            APPLOG_FLUSH();
            break;
        default:
            break;
    }
}

} // namespace

auto engine::context() -> rtti::context&
{
    return *context_ptr();
}

void engine::set_context(rtti::context* ctx)
{
    context_ptr() = ctx;
}

auto engine::try_context() -> rtti::context*
{
    return context_ptr();
}

auto engine::create(rtti::context& ctx, cmd_line::parser& parser) -> bool
{

    context_ptr() = &ctx;

    const fs::path binary_path = get_binary_path(parser);
    fs::add_path_protocol("binary", binary_path);

    fs::path engine_data = binary_path / "data" / "engine";
    fs::add_path_protocol("engine", engine_data);
    fs::add_path_protocol("user", binary_path / USER_FOLDER_NAME);
    fs::add_path_protocol("cache", binary_path / CACHE_FOLDER_NAME);

    serialization::init_data data{
        .warning_logger = [](const std::string& log, const hpp::source_location& loc)
        {
            APPLOG_WARNING_LOC(loc.file_name(), int(loc.line()), loc.function_name(), "Serialization {}", log);
        }
    };
    serialization::init();

    ctx.add<logging>((binary_path / LOG_FILE_NAME).string());
    ctx.add<loading_screen>();

    // Install engine crash handlers immediately after logging is available.
    // Windows AVs go through SEH; CrashLog.txt (+ .dmp) are written crash-safely
    // before these callbacks run. install_handlers copies the path.
    const std::string crash_log_path = (binary_path / CRASH_LOG_FILE_NAME).string();
    crash::install_handlers(crash::crash_handlers{
        .interrupt_handler = engine_interrupt_handler,
        .termination_handler = engine_termination_handler,
        .crash_handler = engine_crash_handler,
        .exception_handler = engine_exception_handler,
        .crash_log_path = crash_log_path.c_str(),
        .write_minidump = true,
    });

    APPLOG_INFO("Engine crash handlers installed (CrashLog.txt / minidump enabled)");

    ctx.add<simulation>();
    ctx.add<events>();
    ctx.add<play_mode>();
    ctx.add<threader>();
    ctx.add<renderer>(ctx, parser);
    ctx.add<audio_system>();
    ctx.add<asset_manager>(ctx);
    ctx.add<ecs>();
    ctx.add<rendering_system>();
    ctx.add<surface_cache_system>();
    ctx.add<transform_system>();
    ctx.add<camera_system>();
    ctx.add<reflection_probe_system>();
    ctx.add<skylight_system>();
    ctx.add<model_system>();
    ctx.add<animation_system>();
    ctx.add<rig_system>();
    ctx.add<particle_system>();
    ctx.add<physics_system>();
    ctx.add<input_system>();
    ctx.add<script_system>(ctx, parser);
    ctx.add<ui_system>();

    parser.set_optional<std::string>("B", "physics", "auto", "Select preferred physics backend.");
    // Empty by default, so that without the flag the raytracing library's own default stands.
    parser.set_optional<std::string>("",
                                     "raytracing",
                                     "",
                                     "Ray tracer of the offline bakes: native, or embree in a build that has it.");

    return true;
}

void engine::select_raytracing_backend(const cmd_line::parser& parser)
{
    std::string name;
    if(parser.try_get("raytracing", name) && !name.empty())
    {
        const hpp::optional<raytracing::backend> selected = raytracing::parse_backend(name);
        if(!selected)
        {
            APPLOG_WARNING("Unknown ray tracer '{}', keeping {}",
                           name,
                           raytracing::to_string(raytracing::get_default_backend()));
        }
        else if(!raytracing::set_default_backend(*selected))
        {
            APPLOG_WARNING("Ray tracer '{}' is not part of this build, keeping {}",
                           name,
                           raytracing::to_string(raytracing::get_default_backend()));
        }
    }
    APPLOG_INFO("Ray tracer for bakes: {}", raytracing::to_string(raytracing::get_default_backend()));
}

void engine::set_user_protocols(const std::string& company, const std::string& product)
{
    const fs::path previous_user = fs::resolve_protocol("user:/");
    const fs::path previous_cache = fs::resolve_protocol("cache:/");
    const std::string preferences_folder = os::paths::get_preferences_path(company, product);
    if(!preferences_folder.empty())
    {
#if UNRAVEL_PLATFORM_ANDROID
        // The internal storage also holds the extracted game files and the log.
        fs::add_path_protocol("user", fs::path(preferences_folder) / USER_FOLDER_NAME);
#else
        fs::add_path_protocol("user", preferences_folder);
#endif
    }
    const std::string cache_folder = os::paths::get_cache_path(company, product);
    if(!cache_folder.empty())
    {
        fs::add_path_protocol("cache", cache_folder);
    }
    const fs::path user = fs::resolve_protocol("user:/");
    const fs::path cache = fs::resolve_protocol("cache:/");
    if(user != previous_user || cache != previous_cache)
    {
        APPLOG_INFO("User data in {}, cache in {}", user.generic_string(), cache.generic_string());
    }
}

auto engine::init_core(const cmd_line::parser& parser, const std::function<bool()>& create_main_window) -> bool
{
    auto& ctx = engine::context();
    auto& ls = ctx.get_cached<loading_screen>();

    select_raytracing_backend(parser);
    os::set_app_event_callback(on_app_event);

    ls.begin_module("Threading");
    if(!ls.check(ctx.get_cached<threader>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Renderer");
    if(!ls.check(ctx.get_cached<renderer>().init(ctx, parser, create_main_window)))
    {
        return false;
    }

    ls.begin_module("Audio");
    if(!ls.check(ctx.get_cached<audio_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Assets");
    if(!ls.check(ctx.get_cached<asset_manager>().init(ctx)))
    {
        return false;
    }

    return true;
}

auto engine::init_systems(const cmd_line::parser& parser) -> bool
{
    auto& ctx = engine::context();
    auto& ls = ctx.get_cached<loading_screen>();

    ls.begin_module("ECS");
    if(!ls.check(ctx.get_cached<ecs>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Rendering");
    if(!ls.check(ctx.get_cached<rendering_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Surface Cache GI");
    // A failed init is not fatal: the atlas is unavailable, is_enabled() reports false, and
    // the renderer simply runs without surface cache GI.
    ctx.get_cached<surface_cache_system>().init(ctx);

    ls.begin_module("Transforms");
    if(!ls.check(ctx.get_cached<transform_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Camera");
    if(!ls.check(ctx.get_cached<camera_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Reflection Probes");
    if(!ls.check(ctx.get_cached<reflection_probe_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Sky Lighting");
    if(!ls.check(ctx.get_cached<skylight_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Models");
    if(!ls.check(ctx.get_cached<model_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Animation");
    if(!ls.check(ctx.get_cached<animation_system>().init(ctx)))
    {
        return false;
    }
    if(!ls.check(ctx.get_cached<rig_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Physics");
    if(!ls.check(ctx.get_cached<physics_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Particles");
    if(!ls.check(ctx.get_cached<particle_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Input");
    if(!ls.check(ctx.get_cached<input_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Scripting");
    if(!ls.check(ctx.get_cached<script_system>().init(ctx, parser)))
    {
        return false;
    }

    ls.begin_module("UI");
    if(!ls.check(ctx.get_cached<ui_system>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Play Mode");
    if(!ls.check(ctx.get_cached<play_mode>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Defaults");
    if(!ls.check(defaults::init(ctx)))
    {
        return false;
    }

    return true;
}

auto engine::deinit() -> bool
{
    auto& ctx = engine::context();
    os::set_app_event_callback({});

    if(!defaults::deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<play_mode>().deinit(ctx))
    {
        return false;
    }

    // The scene's entities hold the script instances and their component wrappers. Release them
    // while every system is still up, so their destroy callbacks run, and before the script domains
    // unload, so nothing keeps the domains alive. A host that closed its scene already (the editor
    // closes the project first) makes this a no-op.
    ctx.get_cached<ecs>().unload_scene();

    if(!ctx.get_cached<ui_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<script_system>().deinit(ctx))
    {
        return false;
    }


    if(!ctx.get_cached<input_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<physics_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<rig_system>().deinit(ctx))
    {
        return false;
    }
    if(!ctx.get_cached<animation_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<model_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<skylight_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<reflection_probe_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<camera_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<transform_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<surface_cache_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<rendering_system>().deinit(ctx))
    {
        return false;
    }
    
    if(!ctx.get_cached<ecs>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<particle_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<asset_manager>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<audio_system>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<renderer>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<threader>().deinit(ctx))
    {
        return false;
    }

    return true;
}

auto engine::destroy() -> bool
{
    auto& ctx = engine::context();

    ctx.remove<defaults>();
    ctx.remove<ui_system>();
    ctx.remove<script_system>();
    ctx.remove<particle_system>();
    ctx.remove<input_system>();
    ctx.remove<physics_system>();
    ctx.remove<rig_system>();
    ctx.remove<animation_system>();
    ctx.remove<model_system>();
    ctx.remove<skylight_system>();
    ctx.remove<reflection_probe_system>();
    ctx.remove<camera_system>();
    ctx.remove<transform_system>();
    ctx.remove<surface_cache_system>();
    ctx.remove<rendering_system>();
    ctx.remove<ecs>();

    ctx.remove<asset_manager>();
    ctx.remove<audio_system>();
    ctx.remove<renderer>();
    ctx.remove<play_mode>();
    ctx.remove<events>();
    ctx.remove<simulation>();
    ctx.remove<threader>();
    ctx.remove<logging>();

    ctx.remove<loading_screen>();
    ctx.remove<boot_config>();

    bool empty = ctx.empty();
    if(!empty)
    {
        ctx.print_types();
    }

    context_ptr() = {};
    return empty;
}

auto engine::process() -> int
{
    auto& ctx = engine::context();

    auto& sim = ctx.get_cached<simulation>();
    auto& ev = ctx.get_cached<events>();
    auto& rend = ctx.get_cached<renderer>();
    auto& thr = ctx.get_cached<threader>();
    auto& input = ctx.get_cached<input_system>();

    {
        APP_SCOPE_PERF("Frame Loop");

        {
            APP_SCOPE_PERF("Threader Process");
            thr.process();
        }

        ctx.get_cached<asset_manager>().free_retired_handle_states();

        sim.run_one_frame(true);

        auto dt = sim.get_delta_time();
        auto& play = ctx.get_cached<play_mode>();

        // First simulation frame: zero dt so Start/init work cannot integrate
        // a hitch. Increment frames_running at frame end so consumers can also
        // detect this via frames_running() == 0 (e.g. mid-frame enter_running).
        if(play.is_simulation_running() && play.frames_running() == 0)
        {
            dt = {};
        }

        if(play.is_paused())
        {
            // A requested frame step runs this paused frame like a running one, so every system
            // (scripts, late update, physics, animation, seq) advances once in its usual order.
            dt = play.begin_frame_step() ? delta_t(play_mode::FRAME_STEP_SECONDS) * sim.get_time_scale() : delta_t{};
        }

        update_input_zone(rend, input);

        input.manager.before_events_update();
        update_cursor_visibility(rend, input);

        bool should_quit = is_shutting_down;

        {
            APP_SCOPE_PERF("Poll OS Events");
            os::event e{};
            while(!should_quit && os::poll_event(e))
            {
                ev.on_os_event(ctx, e);

                input.manager.on_os_event(e);

                should_quit |= rend.get_main_window() == nullptr;
            }
        }

        {
            APP_SCOPE_PERF("After Events Update");
            input.manager.after_events_update();
        }

        if(should_quit)
        {
            ctx.get_cached<play_mode>().set_active(ctx, false);
            const bool restart = is_restart_requested.exchange(false);
            is_shutting_down = false;
            if(restart)
            {
                APPLOG_INFO("Engine process returning restart action");
                return SERVICE_RESULT_RESTART;
            }
            return SERVICE_RESULT_EXIT;
        }

        {   
            APP_SCOPE_PERF("Frame Begin");
            ev.on_frame_begin(ctx, dt);
        }

        {
            APP_SCOPE_PERF("Seq Update");
            seq::update(dt);
        }


        {
            APP_SCOPE_PERF("Frame Update");
            ev.on_frame_update(ctx, dt);
        }

        {
            APP_SCOPE_PERF("Seq Update Zero");
            seq::update(delta_t::zero());
        }

        {
            APP_SCOPE_PERF("Frame Before Render");
            ev.on_frame_before_render(ctx, dt);
        }

        {
            APP_SCOPE_PERF("Frame Render");
            ev.on_frame_render(ctx, dt);
        }

        {
            APP_SCOPE_PERF("Frame End");
            ev.on_frame_end(ctx, dt);
        }

        // Before the step ends, so a stepped frame counts as simulated.
        if(play.is_simulation_running())
        {
            play.on_simulation_frame();
        }

        play.end_frame_step();
    }

    get_app_profiler()->swap();

    return 1;
}
auto engine::interrupt() -> bool
{
    is_shutting_down = true;
    return true;
}

auto engine::is_interrupted() -> bool
{
    return is_shutting_down;
}

auto engine::request_restart() -> bool
{
    APPLOG_INFO("Application restart requested");
    is_restart_requested = true;
    is_shutting_down = true;
    return true;
}

} // namespace unravel
