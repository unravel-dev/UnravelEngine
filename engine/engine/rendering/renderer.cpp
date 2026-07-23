#include "renderer.h"
#include "eviction_settings.h"
#include "../events.h"
#include "spdlog/common.h"
#include "gpu_program.h"
#include <engine/engine.h>
#include <engine/profiler/profiler.h>
#include <engine/settings/boot_config.h>
#include <engine/settings/settings.h>

#include <base/assert.hpp>
#include <graphics/debugdraw.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <logging/logging.h>

#include <cstdint>
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
    ev.on_frame_begin.connect(sentinel_, this, &renderer::frame_begin);
    ev.on_frame_end.connect(sentinel_, this, &renderer::frame_end);

    parser.set_optional<std::string>("r", "renderer", "auto", "Select preferred renderer.");
    parser.set_optional<bool>("n", "novsync", false, "Disable vsync.");
}

auto renderer::init(rtti::context& ctx, const cmd_line::parser& parser) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    if(!os::init())
    {
        return false;
    }

    if(!init_backend(parser))
    {
        return false;
    }

    return true;
}

auto renderer::create_window_for_display(int index, const std::string& title, uint32_t flags)
    -> const std::unique_ptr<render_window>&
{
    auto mode = os::display::get_desktop_mode(index);
    auto bounds = os::display::get_usable_bounds(index);

    if(flags & os::window::resizable)
    {
        auto frame_size = init_window_->get_frame_size();
        bounds.y += static_cast<int>(static_cast<float>(frame_size.top) / mode.display_scale);
        bounds.h -= static_cast<int>(static_cast<float>(frame_size.top + frame_size.bottom) / mode.display_scale);
        bounds.x += static_cast<int>(static_cast<float>(frame_size.left) / mode.display_scale);
        bounds.w -= static_cast<int>(static_cast<float>(frame_size.left + frame_size.right) / mode.display_scale);
    }

    os::window window(title, bounds.x, bounds.y, bounds.w * mode.display_scale, bounds.h * mode.display_scale, flags);
    
    set_main_window(std::move(window));
    return render_window_;
}

void renderer::set_main_window(os::window&& window)
{
    render_window_ = std::make_unique<render_window>(std::move(window));
}

auto renderer::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    return true;
}

auto renderer::init_backend(const cmd_line::parser& parser) -> bool
{
    init_window_.reset();
    init_window_ =
        std::make_unique<os::window>("INIT", os::window::centered, os::window::centered, 64, 64, os::window::hidden);
    const auto sz = init_window_->get_size();

    gfx::init_type init_data;
    init_data.type = get_renderer_type(parser);
    init_data.resolution.width = sz.w;
    init_data.resolution.height = sz.h;
    init_data.resolution.reset = get_reset_flags(parser);
    init_data.platformData.ndt = init_window_->get_native_display();
    init_data.platformData.nwh = init_window_->get_native_handle();
    std::string video_driver = os::window::get_current_video_driver();
    if(video_driver == "wayland")
    {
        init_data.platformData.type = bgfx::NativeWindowHandleType::Wayland;
    }
    reset_flags_ = init_data.resolution.reset;

    init_data.limits.numDrawCalls = 65536;
    init_data.limits.numDrawCallPeakFrames = 0;

    if(!gfx::init(init_data))
    {
        APPLOG_ERROR("Could not initialize rendering backend!");
        return false;
    }
    APPLOG_TRACE("Using {0} rendering backend.", gfx::get_renderer_name(gfx::get_renderer_type()));

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
                    render_window_->prepare_surface();
                }
            }
        }
    }
}

auto renderer::get_renderer_type(const cmd_line::parser& parser) const -> gfx::renderer_type
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
    return gfx::renderer_type::Count;
}

auto renderer::get_reset_flags(const cmd_line::parser& parser) const -> uint32_t
{
    bool novsync = false;
    parser.try_get("novsync", novsync);
    return get_reset_flags(!novsync);
}

auto renderer::get_reset_flags(bool vsync) const -> uint32_t
{
    uint32_t flags = BGFX_RESET_MAXANISOTROPY | BGFX_RESET_HIDPI;

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
    gfx::frames(2, BGFX_FRAME_FLUSH);
    render_window_.reset();

    gfx::set_trace_logger(nullptr);
    gfx::set_info_logger(nullptr);
    gfx::set_warning_logger(nullptr);
    gfx::set_error_logger(nullptr);

    ddShutdown();
    gfx::shutdown();

    init_window_.reset();
    os::shutdown();
}

auto renderer::get_main_window() const -> render_window*
{
    return render_window_.get();
}

void renderer::close_main_window()
{
    render_window_.reset();
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

    const auto sz = init_window_->get_size();

    gfx::reset(sz.w, sz.h, reset_flags_);
}

void renderer::frame_begin(rtti::context& ctx, delta_t /*dt*/)
{
    // Drive GPU eviction at the start of the frame, before this frame's resource creates are
    // submitted, so freed memory precedes new allocations in bgfx's command stream (lowers peak
    // usage). Runs on the render thread, independently of the editor, using the policy persisted in
    // the project's graphics settings (a no-op until a settings instance exists).
    if(ctx.has<settings>())
    {
        update_eviction(ctx.get<settings>().graphics.eviction);
    }

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

    if(!request_screenshot_.empty())
    {
        gfx::request_screen_shot(get_main_window()->get_surface()->native_handle(), request_screenshot_.c_str());
        request_screenshot_ = {};
    }

    gfx::render_pass::reset();
}

} // namespace unravel
