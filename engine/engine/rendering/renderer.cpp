#include "renderer.h"
#include "../events.h"
#include "spdlog/common.h"

#include <base/assert.hpp>
#include <graphics/debugdraw.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <logging/logging.h>

namespace unravel
{
renderer::renderer(rtti::context& ctx, cmd_line::parser& parser)
{
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
        uint32_t window_header = 38 / mode.display_scale;
        bounds.y += window_header;
        bounds.h -= window_header;
    }

    os::window window(title, bounds.x, bounds.y, bounds.w * mode.display_scale, bounds.h * mode.display_scale, flags);
    set_main_window(std::move(window));
    return render_window_;
}

auto renderer::set_main_window(os::window&& window) -> const std::unique_ptr<render_window>&
{
    render_window_ = std::make_unique<render_window>(std::move(window));
    return render_window_;
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
    reset_flags_ = init_data.resolution.reset;
    if(!gfx::init(init_data))
    {
        APPLOG_ERROR("Could not initialize rendering backend!");
        return false;
    }
    APPLOG_TRACE("Using {0} rendering backend.", gfx::get_renderer_name(gfx::get_renderer_type()));

    APPLOG_TRACE("DebugDraw Init.");
    ddInit();

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
    // auto detect
    auto preferred_renderer_type = gfx::renderer_type::Count;

    std::string preferred_renderer;
    if(parser.try_get("renderer", preferred_renderer))
    {
        if(preferred_renderer == "opengl")
        {
            preferred_renderer_type = gfx::renderer_type::OpenGL;
        }
        else if(preferred_renderer == "vulkan")
        {
            preferred_renderer_type = gfx::renderer_type::Vulkan;
        }
        else if(preferred_renderer == "directx11")
        {
            preferred_renderer_type = gfx::renderer_type::Direct3D11;
        }
        else if(preferred_renderer == "directx12")
        {
            preferred_renderer_type = gfx::renderer_type::Direct3D12;
        }
    }

    return preferred_renderer_type;
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

auto renderer::get_main_window() const -> const std::unique_ptr<render_window>&
{
    return render_window_;
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

void renderer::frame_begin(rtti::context& /*ctx*/, delta_t /*dt*/)
{
    auto& window = get_main_window();
    auto& pass = window->begin_present_pass();
    pass.clear();
}

void renderer::frame_end(rtti::context& /*ctx*/, delta_t /*dt*/)
{
    gfx::render_pass pass(gfx::render_pass::get_max_pass_id(), "backbuffer_update");
    pass.bind();

    gfx::frame();

    // if(!request_screenshot_.empty())
    // {
    //     gfx::request_screen_shot(get_main_window()->get_surface()->native_handle(), request_screenshot_.c_str());
    //     request_screenshot_ = {};
    // }

    gfx::render_pass::reset();
}

} // namespace unravel
