#include "renderer.h"
#include "batch_collector.h"
#include "eviction_settings.h"
#include "../events.h"
#include "spdlog/common.h"
#include "gpu_pass_log.h"
#include "gpu_program.h"
#include <engine/engine.h>
#include <engine/profiler/profiler.h>
#include <engine/settings/boot_config.h>
#include <engine/settings/settings.h>
#include <engine/rendering/particles/ps/particle_system_soa.h>

#include <base/assert.hpp>
#include <base/platform/config.hpp>
#include <filesystem/filesystem.h>
#include <graphics/debugdraw.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <logging/logging.h>

#include <ospp/event.h>
#include <ospp/message_box.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace unravel
{
namespace
{
thread_local std::vector<uint32_t> s_bgfx_profiler_scope_stack;
}

renderer::renderer(rtti::context& ctx, cmd_line::parser& parser)
{
    gfx::set_debug_logger(
        [](const std::string& msg, const char* file_path, uint16_t line)
        {
            APPLOG_DEBUG_LOC(file_path, line, "renderer", msg);
        });
    gfx::set_trace_logger(
        [](const std::string& msg, const char* file_path, uint16_t line)
        {
            APPLOG_TRACE_LOC(file_path, line, "renderer", msg);
        });
    gfx::set_info_logger(
        [](const std::string& msg, const char* file_path, uint16_t line)
        {
            APPLOG_INFO_LOC(file_path, line, "renderer", msg);
        });
    gfx::set_warning_logger(
        [](const std::string& msg, const char* file_path, uint16_t line)
        {
            APPLOG_WARNING_LOC(file_path, line, "renderer", msg);
        });
    gfx::set_error_logger(
        [](const std::string& msg, const char* file_path, uint16_t line)
        {
            APPLOG_ERROR_LOC(file_path, line, "renderer", msg);
        });

    auto main_thread_id = std::this_thread::get_id();
    gfx::set_profiler_hooks(
        [main_thread_id](const char* name, uint32_t abgr, const char* file_path, uint16_t line)
        {
            (void)abgr;
            (void)file_path;
            (void)line;
            if(name == nullptr)
            {
                return;
            }
            auto current_id = std::this_thread::get_id();
            if(current_id != main_thread_id)
            {
                s_bgfx_profiler_scope_stack.push_back(
                    profile_begin_owned(hpp::string_view(name), "Render Thread"));
            }
            else
            {
                s_bgfx_profiler_scope_stack.push_back(
                    profile_begin_owned(hpp::string_view(name)));
            }
        },
        [main_thread_id](const char* name, uint32_t abgr, const char* file_path, uint16_t line)
        {
            (void)abgr;
            (void)file_path;
            (void)line;
            if(name == nullptr)
            {
                return;
            }
            auto current_id = std::this_thread::get_id();
            if(current_id != main_thread_id)
            {
                s_bgfx_profiler_scope_stack.push_back(
                    profile_begin_owned(hpp::string_view(name), "Render Thread"));
            }
            else
            {
                s_bgfx_profiler_scope_stack.push_back(profile_begin(name));
            }
        },
        []()
        {
            if(s_bgfx_profiler_scope_stack.empty())
            {
                return;
            }
            const uint32_t token = s_bgfx_profiler_scope_stack.back();
            s_bgfx_profiler_scope_stack.pop_back();
            if(token != UINT32_MAX)
            {
                profile_end(token);
            }
        });
    get_thread_profile_data("Main Thread");

    auto& ev = ctx.get_cached<events>();
    ev.on_os_event.connect(sentinel_, this, &renderer::on_os_event);
    ev.on_app_enter_background.connect(sentinel_, this, &renderer::on_app_enter_background);
    ev.on_frame_begin.connect(sentinel_, this, &renderer::frame_begin);
    ev.on_frame_end.connect(sentinel_, this, &renderer::frame_end);

    parser.set_optional<std::string>("r", "renderer", "auto", "Select preferred renderer.");
    parser.set_optional<bool>("n", "novsync", false, "Disable vsync.");
    parser.set_optional<bool>("", "gfx-stats", false, "Show the graphics backend's statistics over the frame.");
    parser.set_optional<bool>("", "gfx-profile", false, "Log the GPU time of every render pass every few seconds.");
    parser.set_optional<std::string>("W",
                                     "window",
                                     "",
                                     "Main window geometry as x,y,w,h[,maximized] (used across restarts).");
}

auto renderer::init(rtti::context& ctx, const cmd_line::parser& parser, const main_window_factory& create_main_window)
    -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    if(!os::init())
    {
        return false;
    }

    if(!create_main_window || !create_main_window() || !render_window_)
    {
        APPLOG_ERROR("The main window could not be created.");
        return false;
    }

    if(!init_backend(parser))
    {
        return false;
    }

    is_initialized_ = true;

    return true;
}

auto renderer::create_window_for_display(int index, const std::string& title, uint32_t flags)
    -> const std::unique_ptr<render_window>&
{
    auto mode = os::display::get_desktop_mode(index);
    auto bounds = os::display::get_usable_bounds(index);

    os::window window(title,
                      bounds.x,
                      bounds.y,
                      bounds.w * mode.display_scale,
                      bounds.h * mode.display_scale,
                      flags | os::window::hidden);

    if(flags & os::window::resizable)
    {
        // Fit the whole frame, not only the client area, into the usable bounds. The window
        // measures its own borders while it is still hidden.
        auto frame_size = window.get_frame_size();
        bounds.y += static_cast<int>(static_cast<float>(frame_size.top) / mode.display_scale);
        bounds.h -= static_cast<int>(static_cast<float>(frame_size.top + frame_size.bottom) / mode.display_scale);
        bounds.x += static_cast<int>(static_cast<float>(frame_size.left) / mode.display_scale);
        bounds.w -= static_cast<int>(static_cast<float>(frame_size.left + frame_size.right) / mode.display_scale);
        window.set_position(bounds.x, bounds.y);
        window.set_size(bounds.w * mode.display_scale, bounds.h * mode.display_scale);
    }

    set_main_window(std::move(window));
    return render_window_;
}

auto renderer::create_window(const std::string& title,
                             int32_t x,
                             int32_t y,
                             uint32_t width,
                             uint32_t height,
                             uint32_t flags) -> const std::unique_ptr<render_window>&
{
    os::window window(title, x, y, width, height, flags | os::window::hidden);
    set_main_window(std::move(window));
    return render_window_;
}

void renderer::set_main_window(os::window&& window)
{
    if(is_initialized_)
    {
        APPLOG_ERROR("The main window can only be created before the rendering backend.");
        return;
    }
    render_window_ = std::make_unique<render_window>(std::move(window), render_window::surface_kind::backbuffer);
}

namespace
{
constexpr const char* WINDOW_GEOMETRY_PREFIX = "--window=";
constexpr uint32_t MIN_WINDOW_DIMENSION = 64;
/// How long the wait for a surface sleeps when the event pump returns without one.
constexpr auto SURFACE_WAIT_INTERVAL = std::chrono::milliseconds(10);
/// Starts of the graphics backend before giving up, each after the surface went away during the
/// previous one.
constexpr uint32_t MAX_BACKEND_START_ATTEMPTS = 3;
#if UNRAVEL_PLATFORM_ANDROID
/// Shown when Vulkan, the only backend built for Android, does not start.
constexpr const char* NO_VULKAN_MESSAGE = "The game needs Vulkan graphics, which this device could not start.";
#endif
/// The largest scale bgfx draws its debug text at.
constexpr float MAX_DEBUG_TEXT_SCALE = 4.0f;

/// Turns on the graphics backend's statistics overlay and its per-pass GPU timers when the command
/// line asks for them; returns whether the timers are on. The overlay text grows with the display
/// scale, so it stays readable on a phone's dense screen.
auto apply_debug_options(const cmd_line::parser& parser, const os::window& window) -> bool
{
    bool show_stats = false;
    parser.try_get("gfx-stats", show_stats);
    bool log_passes = false;
    parser.try_get("gfx-profile", log_passes);
    if(!show_stats && !log_passes)
    {
        return false;
    }
    const uint32_t flags = (show_stats ? BGFX_DEBUG_STATS : 0u) | (log_passes ? BGFX_DEBUG_PROFILER : 0u);
    const float text_scale = std::clamp(std::floor(window.get_display_scale()), 1.0f, MAX_DEBUG_TEXT_SCALE);
    bgfx::setDebug(flags, BGFX_INVALID_HANDLE, static_cast<uint8_t>(text_scale));
    if(show_stats)
    {
        APPLOG_INFO("Showing the graphics statistics at text scale {}.", text_scale);
    }
    if(log_passes)
    {
        APPLOG_INFO("Logging the GPU time of every render pass.");
    }
    return log_passes;
}

/// The native window to start the graphics backend on. Android can destroy the window's surface
/// between the window's creation and here (a start on a locked phone); the event pump then blocks
/// until the application returns with a new surface.
auto wait_for_native_window(const os::window& window) -> void*
{
    void* native_window = window.get_native_handle();
#if UNRAVEL_PLATFORM_ANDROID
    while(native_window == nullptr && !engine::is_interrupted())
    {
        os::pump_events();
        native_window = window.get_native_handle();
        if(native_window == nullptr)
        {
            std::this_thread::sleep_for(SURFACE_WAIT_INTERVAL);
        }
    }
#endif
    return native_window;
}

void strip_window_geometry_arguments(std::vector<std::string>& arguments)
{
    std::vector<std::string> filtered;
    filtered.reserve(arguments.size());
    for(std::size_t i = 0; i < arguments.size(); ++i)
    {
        const std::string& argument = arguments[i];
        if(argument.rfind(WINDOW_GEOMETRY_PREFIX, 0) == 0)
        {
            continue;
        }
        if(argument == "-W" || argument == "--window")
        {
            if(i + 1 < arguments.size())
            {
                ++i;
            }
            continue;
        }
        filtered.push_back(argument);
    }
    arguments = std::move(filtered);
}
} // namespace

auto renderer::parse_window_geometry(const std::string& value,
                                     int32_t& x,
                                     int32_t& y,
                                     uint32_t& width,
                                     uint32_t& height,
                                     bool& maximized) -> bool
{
    if(value.empty())
    {
        return false;
    }
    int parsed_x = 0;
    int parsed_y = 0;
    int parsed_w = 0;
    int parsed_h = 0;
    int parsed_maximized = 0;
    const int field_count =
        std::sscanf(value.c_str(), "%d,%d,%d,%d,%d", &parsed_x, &parsed_y, &parsed_w, &parsed_h, &parsed_maximized);
    if(field_count != 4 && field_count != 5)
    {
        return false;
    }
    if(parsed_w < static_cast<int>(MIN_WINDOW_DIMENSION) || parsed_h < static_cast<int>(MIN_WINDOW_DIMENSION))
    {
        return false;
    }
    x = parsed_x;
    y = parsed_y;
    width = static_cast<uint32_t>(parsed_w);
    height = static_cast<uint32_t>(parsed_h);
    maximized = field_count == 5 && parsed_maximized != 0;
    return true;
}

void renderer::prepare_restart(std::vector<std::string>& arguments)
{
    strip_window_geometry_arguments(arguments);
    auto* main_window = get_main_window();
    if(!main_window)
    {
        return;
    }
    auto& window = main_window->get_window();
    const auto position = window.get_position();
    const auto size = window.get_size();
    if(size.w < MIN_WINDOW_DIMENSION || size.h < MIN_WINDOW_DIMENSION)
    {
        return;
    }
    const int maximized = window.is_maximized() ? 1 : 0;
    arguments.emplace_back(std::string(WINDOW_GEOMETRY_PREFIX) + std::to_string(position.x) + "," +
                           std::to_string(position.y) + "," + std::to_string(size.w) + "," +
                           std::to_string(size.h) + "," + std::to_string(maximized));
}

auto renderer::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    return true;
}

auto renderer::start_backend(bgfx::Init& init_data) -> bool
{
    const auto& window = render_window_->get_window();
    for(uint32_t attempt = 1;; ++attempt)
    {
        init_data.swapChain.nwh = wait_for_native_window(window);
        if(init_data.swapChain.nwh == nullptr)
        {
            APPLOG_ERROR("The main window has no surface to render to.");
            return false;
        }
        render_window_->prepare_surface();
        const auto size = render_window_->get_surface()->get_size();
        init_data.swapChain.width = size.width;
        init_data.swapChain.height = size.height;
        const uint32_t backgrounds = background_count_;
        if(gfx::init(init_data))
        {
            return true;
        }
        // Android can destroy the surface while the backend starts on it. The pump then takes the
        // pending background event and blocks until the application returns with a new surface.
        os::pump_events();
        if(background_count_ == backgrounds || attempt == MAX_BACKEND_START_ATTEMPTS)
        {
            return false;
        }
        APPLOG_INFO("The surface went away while the graphics backend started. Starting it again.");
    }
}

void renderer::on_app_enter_background(rtti::context& /*ctx*/)
{
    ++background_count_;
}

auto renderer::init_backend(const cmd_line::parser& parser) -> bool
{
    // bgfx is initialized on the main window: its backbuffer is the window's surface.
    const auto& window = render_window_->get_window();

    bgfx::Init init_data;
    init_data.type = get_renderer_type(parser);
    init_data.reset = get_reset_flags(parser);
    init_data.swapChain.flags = render_window::get_swap_chain_flags();
    init_data.swapChain.ndt = window.get_native_display();
#if UNRAVEL_PLATFORM_ANDROID
    // Vulkan is the only backend built for Android. Without the fallback a failed start fails,
    // where bgfx would otherwise run on its no-op renderer.
    init_data.type = bgfx::RendererType::Vulkan;
    init_data.fallback = false;
    // Android's Vulkan surfaces present RGBA8, not bgfx's default BGRA8: a BGRA8 swap chain is
    // created, but every frame it shows is black. Later resizes keep the main swap chain's format.
    init_data.swapChain.formatColor = bgfx::TextureFormat::RGBA8;
#endif
    std::string video_driver = os::window::get_current_video_driver();
    if(video_driver == "wayland")
    {
        init_data.platformData.type = bgfx::NativeWindowHandleType::Wayland;
    }
    reset_flags_ = init_data.reset;

    if(!start_backend(init_data))
    {
        APPLOG_ERROR("Could not initialize rendering backend!");
#if UNRAVEL_PLATFORM_ANDROID
        // The application closes next; a phone shows no log, so say why.
        if(!engine::is_interrupted())
        {
            os::message_box::show(os::message_box::kind::error, window.get_title(), NO_VULKAN_MESSAGE);
        }
#endif
        return false;
    }
    backbuffer_size_ = {init_data.swapChain.width, init_data.swapChain.height};
    backbuffer_window_ = init_data.swapChain.nwh;
    APPLOG_TRACE("Using {0} rendering backend.", bgfx::getRendererName(bgfx::getRendererType()));
    if(apply_debug_options(parser, window))
    {
        gpu_pass_log_ = std::make_unique<gpu_pass_log>();
    }

    // Driver-compiled pipeline binaries persist here across runs. Without them, D3D12
    // recompiles every pipeline at first use each launch - the GI compute chain alone was a
    // single ~30 s frame the moment GI came on. Stale entries are verified and rebuilt by the
    // backend. One subdirectory per backend: the cache ids are 32-bit hashes of backend-
    // specific inputs, so a shared directory allows (rare) cross-backend id collisions where
    // two backends keep overwriting each other's entry - and a per-backend split also makes
    // invalidation after a driver update a matter of deleting one folder. Set AFTER init, so
    // the auto-detected backend names the folder; no backend touches the cache before its
    // first pipeline/program creation, which happens well after this point.
    const auto renderer_slug = [](bgfx::RendererType::Enum type) -> const char*
    {
        switch(type)
        {
            case bgfx::RendererType::Direct3D11:
                return "d3d11";
            case bgfx::RendererType::Direct3D12:
                return "d3d12";
            case bgfx::RendererType::Vulkan:
                return "vulkan";
            case bgfx::RendererType::OpenGL:
                return "opengl";
            case bgfx::RendererType::OpenGLES:
                return "opengles";
            case bgfx::RendererType::Metal:
                return "metal";
            default:
                return "other";
        }
    };
    const auto cache_dir =
        fs::resolve_protocol("binary:/.cache/gfx") / renderer_slug(bgfx::getRendererType());
    gfx::set_cache_directory(cache_dir.string());

    APPLOG_TRACE("DebugDraw Init.");
    ddInit();

    const bgfx::Caps* caps = bgfx::getCaps();
    if (0 != (caps->supported & BGFX_CAPS_GRAPHICS_DEBUGGER) )
    {
        APPLOG_TRACE("Graphics debugger is supported.");
    }
    else
    {
        APPLOG_TRACE("Graphics debugger is not supported.");
    }

    return true;
}

void renderer::on_os_event(rtti::context& ctx, os::event& e)
{
    if(e.type == os::events::window)
    {
        if(e.window.type == os::window_event_id::close)
        {
            auto window_id = e.window.window_id;
            if(render_window_)
            {
                if(render_window_->get_window().get_id() == window_id)
                {
                    close_main_window();
                }
            }
        }

        if(e.window.type == os::window_event_id::resized)
        {
            auto window_id = e.window.window_id;

            if(render_window_)
            {
                if(render_window_->get_window().get_id() == window_id)
                {
                    sync_main_surface();
                }
            }
        }
    }
}

auto renderer::get_renderer_type(const cmd_line::parser& parser) const -> bgfx::RendererType::Enum
{
    auto& ctx = engine::context();
    if(ctx.has<boot_config>())
    {
        return preferred_renderer_to_gfx_type(ctx.get<boot_config>().renderer);
    }
    std::string preferred_renderer_arg;
    if(parser.try_get("renderer", preferred_renderer_arg))
    {
        return preferred_renderer_to_gfx_type(preferred_renderer_from_string(preferred_renderer_arg));
    }
    return bgfx::RendererType::Count;
}

auto renderer::get_reset_flags(const cmd_line::parser& parser) const -> uint32_t
{
    bool novsync = false;
    parser.try_get("novsync", novsync);
    return get_reset_flags(!novsync);
}

auto renderer::get_reset_flags(bool vsync) const -> uint32_t
{
    uint32_t flags = BGFX_RESET_MAXANISOTROPY;

    if(vsync)
    {
        flags |= BGFX_RESET_VSYNC;
    }
    else
    {
        flags |= BGFX_RESET_NONE;
    }

    return flags;
}

renderer::~renderer()
{
    // Constructed by engine::create, initialised separately - and a headless run never does.
    // Everything below talks to bgfx or the OS layer, which would assert or crash if init
    // never ran.
    if(!is_initialized_)
    {
        return;
    }

    gfx::frames(2, BGFX_FRAME_FLUSH);

    gfx::set_trace_logger(nullptr);
    gfx::set_info_logger(nullptr);
    gfx::set_warning_logger(nullptr);
    gfx::set_error_logger(nullptr);

    ddShutdown();
    gfx::shutdown();

    // bgfx presents to the main window until it shuts down.
    render_window_.reset();
    closed_main_window_.reset();
    os::shutdown();
}

auto renderer::get_main_window() const -> render_window*
{
    return render_window_.get();
}

void renderer::close_main_window()
{
    if(!render_window_)
    {
        return;
    }
    render_window_->get_window().hide();
    closed_main_window_ = std::move(render_window_);
}

void renderer::request_screenshot(const std::string& file)
{
    request_screenshot_ = file;
}

auto renderer::get_vsync() const -> bool
{
    return (reset_flags_ & BGFX_RESET_VSYNC) != 0;
}
void renderer::set_vsync(bool vsync)
{
    if(vsync)
    {
        reset_flags_ |= BGFX_RESET_VSYNC;
    }
    else
    {
        reset_flags_ &= ~BGFX_RESET_VSYNC;
    }

    // No swap chain description: the main window keeps its backbuffer, only the device-wide
    // flags change.
    bgfx::reset(reset_flags_);
}

void renderer::sync_main_surface()
{
    if(!render_window_)
    {
        return;
    }
    render_window_->prepare_surface();
    const auto size = render_window_->get_surface()->get_size();
    auto* native_window = render_window_->get_window().get_native_handle();
    // A minimized window reports no area, and a window without a surface no native window; the
    // backbuffer stays as it is until they come back.
    if(size.width == 0 || size.height == 0 || native_window == nullptr)
    {
        return;
    }
    if(size == backbuffer_size_ && native_window == backbuffer_window_)
    {
        return;
    }
    bgfx::SwapChain desc;
    desc.nwh = native_window;
    desc.width = size.width;
    desc.height = size.height;
    desc.flags = render_window::get_swap_chain_flags();
    bgfx::reset(reset_flags_, &desc);
    backbuffer_size_ = size;
    backbuffer_window_ = native_window;
}

void renderer::frame_begin(rtti::context& ctx, delta_t /*dt*/)
{
    // Drive GPU eviction at the start of the frame, before this frame's resource creates are
    // submitted, so freed memory precedes new allocations in bgfx's command stream (lowers peak
    // usage). Runs on the render thread, independently of the editor, using the policy persisted in
    // the project's graphics settings (a no-op until a settings instance exists).
    if(ctx.has<settings>())
    {
        const settings::graphics_settings& graphics = ctx.get<settings>().graphics;
        update_eviction(graphics.eviction);
        batch_collector::set_static_mesh_batching_enabled(graphics.static_mesh_batching);
        const auto& particles = graphics.particles;
        ps_soa::set_quality({particles.spawn_scale,
                             particles.sorting,
                             particles.force_half_resolution,
                             particles.min_screen_size,
                             particles.max_drawn_particles,
                             particles.max_particles_scale,
                             particles.max_lighting});
    }

    sync_main_surface();
    auto window = get_main_window();
    if(window)
    {
        auto& pass = window->begin_present_pass();
        pass.clear();
    }
}

void renderer::frame_end(rtti::context& /*ctx*/, delta_t /*dt*/)
{
    APP_SCOPE_PERF("Graphics Frame Submit");

    gfx::render_pass pass(gfx::render_pass::get_max_pass_id(), "Backbuffer/Update Pass");
    pass.bind();

    gfx::frame();
    if(gpu_pass_log_)
    {
        gpu_pass_log_->collect();
    }

    if(!request_screenshot_.empty())
    {
        bgfx::requestScreenShot(get_main_window()->get_surface()->native_handle(), request_screenshot_.c_str());
        request_screenshot_ = {};
    }

    gfx::render_pass::reset();
}

} // namespace unravel
