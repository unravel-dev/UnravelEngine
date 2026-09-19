#include "imgui_interface.h"
#include "imgui/imgui.h"
#include "integration/imgui_style.h"
#include "loading_page.h"
#include <editor/events.h>
#include <engine/profiler/profiler.h>

#include <engine/events.h>
#include <engine/rendering/renderer.h>
#include <graphics/graphics.h>

#include <logging/logging.h>

#include <string>

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

auto imgui_interface::init_basic(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    const auto& rend = ctx.get_cached<renderer>();
    const auto& main_window = rend.get_main_window();
    imguiCreate(main_window, 14.0f);

    imgui_style::set_unity_theme();

    inited_ = true;
    return true;
}

auto imgui_interface::init_finalize(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    imguiCreateCubemapProgram();
    return true;
}

auto imgui_interface::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void imgui_interface::render_loading_frame(rtti::context& ctx,
                                           const std::string& stage,
                                           size_t completed,
                                           size_t total,
                                           const std::string& current_job)
{


    auto now = std::chrono::steady_clock::now();
    auto dt = now - last_frame_time_;

    if(dt < std::chrono::milliseconds(32) && completed != total)
    {
        return;
    }

    last_frame_time_ = now;

    const auto& rend = ctx.get_cached<renderer>();
    auto window = rend.get_main_window();
    if(!window)
    {
        return;
    }

    os::event e{};
    while(os::poll_event(e))
    {
        imguiProcessEvent(e);
    }

    auto& present_pass = window->begin_present_pass();
    present_pass.clear();

    for(int i = 0; i < 1; ++i)
    {
        imguiBeginFrame(1.0f / 60.0f);
        loading_page::draw({stage, completed, total, current_job});
    
        auto& main_surface = window->get_surface();
        gfx::render_pass pass("ImGui/Loading Pass");
        pass.bind(main_surface.get());
        imguiEndFrame(pass.id);
    
        gfx::render_pass end_pass(gfx::render_pass::get_max_pass_id(), "Backbuffer/Loading Present");
        end_pass.bind();
        gfx::frame();
    }

    gfx::render_pass::reset();

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

    APP_SCOPE_PERF("ImGui Frame");
    imguiBeginFrame(dt.count());

    ev.on_frame_ui_render(ctx, dt);

    gfx::render_pass pass("ImGui/Pass");
    pass.bind(main_surface.get());
    imguiEndFrame(pass.id);
}

} // namespace unravel
