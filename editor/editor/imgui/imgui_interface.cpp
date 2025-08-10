#include "imgui_interface.h"
#include <editor/events.h>

#include <engine/events.h>
#include <engine/rendering/renderer.h>

namespace unravel
{

imgui_interface::imgui_interface(rtti::context& ctx)
{
    auto& ev = ctx.get_cached<events>();

    ev.on_os_event.connect(sentinel_, 1000, this, &imgui_interface::on_os_event);
    ev.on_frame_render.connect(sentinel_, -100000, this, &imgui_interface::on_frame_ui_render);
}

imgui_interface::~imgui_interface()
{
    if(inited_)
    {
        imguiDestroy();
    }
}

auto imgui_interface::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    const auto& rend = ctx.get_cached<renderer>();
    const auto& main_window = rend.get_main_window();
    imguiCreate(main_window.get(), 18.0f);

    inited_ = true;
    return true;
}

auto imgui_interface::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void imgui_interface::on_os_event(rtti::context& ctx, os::event& e)
{
    imguiProcessEvent(e);
}

void imgui_interface::on_frame_ui_render(rtti::context& ctx, delta_t dt)
{
    const auto& ev = ctx.get_cached<ui_events>();

    const auto& rend = ctx.get_cached<renderer>();
    const auto& main_window = rend.get_main_window();
    const auto& main_surface = main_window->get_surface();

    imguiBeginFrame(dt.count());

    ev.on_frame_ui_render(ctx, dt);

    gfx::render_pass pass("imgui_pass");
    pass.bind(main_surface.get());
    imguiEndFrame(pass.id);
}

} // namespace unravel
