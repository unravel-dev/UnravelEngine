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
#include <engine/animation/ecs/systems/animation_system.h>
#include <engine/audio/ecs/systems/audio_system.h>
#include <engine/ecs/systems/transform_system.h>
#include <engine/input/input.h>
#include <engine/physics/ecs/systems/physics_system.h>
#include <engine/rendering/ecs/systems/camera_system.h>
#include <engine/rendering/ecs/systems/model_system.h>
#include <engine/rendering/ecs/systems/reflection_probe_system.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/rendering/ecs/systems/skylight_system.h>
#include <engine/rendering/ecs/systems/particle_system.h>
#include <ospp/event.h>

#include <crash/crash.hpp>
#include <cstdlib>
#include <exception>
#include <logging/logging.h>
#include <service/service.h>


#include <seq/seq.h>
#include <simulation/simulation.h>

#include <filesystem/filesystem.h>
namespace unravel
{
namespace
{

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
    // Log the crash with full details
    APPLOG_CRITICAL("Crash signal ({}) -> {}\n{}", info.signal_number, info.signal_name, trace.formatted_trace);

    // Try emergency cleanup
    engine::interrupt();

    APPLOG_FLUSH();
}

void engine_exception_handler(const crash::exception_info& info, const crash::trace_info& trace)
{
    APPLOG_CRITICAL("{}\n{}", info.exception_message, trace.formatted_trace);

    // Same emergency cleanup as crash handler
    engine::interrupt();

    APPLOG_FLUSH();
}

} // namespace

auto engine::context() -> rtti::context&
{
    return *context_ptr();
}

auto engine::create(rtti::context& ctx, cmd_line::parser& parser) -> bool
{

    context_ptr() = &ctx;

    fs::path binary_path = fs::executable_path(parser.app_name().c_str()).parent_path();
    fs::add_path_protocol("binary", binary_path);

    fs::path engine_data = binary_path / "data" / "engine";
    fs::add_path_protocol("engine", engine_data);


    serialization::init_data data{
        .warning_logger = [](const std::string& log, const hpp::source_location& loc)
        {
            APPLOG_WARNING_LOC(loc.file_name(), int(loc.line()), loc.function_name(), "Serialization {}", log);
        }
    };
    serialization::init();

    ctx.add<logging>();
    ctx.add<loading_screen>();

    // Install engine crash handlers immediately after logging is available
    crash::install_handlers(crash::crash_handlers{
        .interrupt_handler = engine_interrupt_handler,
        .termination_handler = engine_termination_handler,
        .crash_handler = engine_crash_handler,
        .exception_handler = engine_exception_handler,
    });

    APPLOG_INFO("Engine crash handlers installed");

    ctx.add<simulation>();
    ctx.add<events>();
    ctx.add<play_mode>();
    ctx.add<threader>();
    ctx.add<renderer>(ctx, parser);
    ctx.add<audio_system>();
    ctx.add<asset_manager>(ctx);
    ctx.add<ecs>();
    ctx.add<rendering_system>();
    ctx.add<transform_system>();
    ctx.add<camera_system>();
    ctx.add<reflection_probe_system>();
    ctx.add<skylight_system>();
    ctx.add<model_system>();
    ctx.add<animation_system>();
    ctx.add<particle_system>();
    ctx.add<physics_system>();
    ctx.add<input_system>();
    ctx.add<script_system>(ctx, parser);
    ctx.add<ui_system>();
    return true;
}

auto engine::init_core(const cmd_line::parser& parser) -> bool
{
    auto& ctx = engine::context();
    auto& ls = ctx.get_cached<loading_screen>();

    ls.begin_module("Threading");
    if(!ls.check(ctx.get_cached<threader>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Renderer");
    if(!ls.check(ctx.get_cached<renderer>().init(ctx, parser)))
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

    if(!defaults::deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<play_mode>().deinit(ctx))
    {
        return false;
    }


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
    ctx.remove<animation_system>();
    ctx.remove<model_system>();
    ctx.remove<skylight_system>();
    ctx.remove<reflection_probe_system>();
    ctx.remove<camera_system>();
    ctx.remove<transform_system>();
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
            dt = {};
        }

        update_input_zone(rend, input);

        input.manager.before_events_update();

        bool should_quit = false;

        {
            APP_SCOPE_PERF("Poll OS Events");
            os::event e{};
            while(os::poll_event(e))
            {
                ev.on_os_event(ctx, e);

                input.manager.on_os_event(e);

                should_quit = rend.get_main_window() == nullptr || is_shutting_down;
                if(should_quit)
                {
                    break;
                }
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

        if(play.is_simulation_running())
        {
            play.on_simulation_frame();
        }
    }

    get_app_profiler()->swap();

    return 1;
}
auto engine::interrupt() -> bool
{
    is_shutting_down = true;
    return true;
}

auto engine::request_restart() -> bool
{
    APPLOG_INFO("Application restart requested");
    is_restart_requested = true;
    is_shutting_down = true;
    return true;
}

} // namespace unravel
