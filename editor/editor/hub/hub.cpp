#include "hub.h"
#include "imgui_widgets/utils.h"
#include <editor/events.h>
#include "panels/panels_defs.h"
#include <editor/editing/editing_manager.h>
#include <editor/project/project_info.h>
#include <editor/system/project_manager.h>
#include <editor/editing/create_scene_modal.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <editor/imgui/integration/imgui_messagebox.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/rendering/renderer.h>
#include <hpp/optional.hpp>
#include <logging/logging.h>
#include <version/version.h>
#include <filedialog/filedialog.h>
#include <imgui/imgui.h>
#include <imgui_widgets/markdown.h>
#include <memory>

namespace unravel
{

hub::hub(rtti::context& ctx)
{
    auto& ui_ev = ctx.get_cached<ui_events>();
    auto& ev = ctx.get_cached<events>();

    ev.on_project_opened.connect(sentinel_, this, &hub::on_project_opened);
    // Drives the scene systems (animation included), so it must stay ahead of
    // the script band - see frame_update_priority in engine/events.h.
    ev.on_frame_update.connect(sentinel_, frame_update_priority::scene_systems, this, &hub::on_frame_update);
    ev.on_frame_before_render.connect(sentinel_, this, &hub::on_frame_before_render);
    ev.on_frame_render.connect(sentinel_, this, &hub::on_frame_render);
    ev.on_play_before_begin.connect(sentinel_, -998, this, &hub::on_play_before_begin);
    ev.on_play_begin.connect(sentinel_, -999, this, &hub::on_play_begin);
    ev.on_play_after_end.connect(sentinel_, -999, this, &hub::on_play_after_end);

    ev.on_script_recompile.connect(sentinel_, 10000, this, &hub::on_script_recompile);
    ev.on_os_event.connect(sentinel_, 10000, this, &hub::on_os_event);

    ui_ev.on_frame_ui_render.connect(sentinel_, this, &hub::on_frame_ui_render);
}

auto hub::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    panels_.init(ctx);

    return true;
}

auto hub::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    panels_.deinit(ctx);

    return true;
}

void hub::open_project_settings(rtti::context& ctx, const std::string& hint)
{
    auto& pm = ctx.get_cached<project_manager>();

    if(!pm.has_open_project())
    {
        return;
    }

    panels_.get_project_settings_panel().show(true, hint);
}

void hub::on_project_opened(rtti::context& ctx)
{
    panels_.get_dockspace().refresh();
    panels_.get_scene_panel().on_project_opened();
    panels_.get_game_panel().on_project_opened();
}

void hub::on_frame_update(rtti::context& ctx, delta_t dt)
{
    auto& pm = ctx.get_cached<project_manager>();

    if(!pm.has_open_project())
    {
        return;
    }
    panels_.on_frame_update(ctx, dt);
}

void hub::on_frame_before_render(rtti::context& ctx, delta_t dt)
{
    auto& pm = ctx.get_cached<project_manager>();

    if(!pm.has_open_project())
    {
        return;
    }
    panels_.on_frame_before_render(ctx, dt);
}

void hub::on_frame_render(rtti::context& ctx, delta_t dt)
{
    APP_SCOPE_PERF("Hub Frame Render");
    auto& pm = ctx.get_cached<project_manager>();

    if(!pm.has_open_project())
    {
        return;
    }
    panels_.on_frame_render(ctx, dt);
}

void hub::on_frame_ui_render(rtti::context& ctx, delta_t dt)
{
    auto& pm = ctx.get_cached<project_manager>();

    if(!pm.has_open_project())
    {
        on_start_page_render(ctx);
    }
    else
    {
        on_opened_project_render(ctx);
    }


    // Render toasts on top of everything, at the end of your code!
    // You should push style vars here
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f); // Round borders
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(43.f / 255.f, 43.f / 255.f, 43.f / 255.f, 100.f / 255.f)); // Background color
    ImGui::RenderNotifications(); // <-- Here we render all notifications
    ImBox::RenderMessageBoxes();
    create_scene_modal::render();
    ImGui::PopStyleVar(1); // Don't forget to Pop()
    ImGui::PopStyleColor(1);

    if(ImGui::IsKeyPressed(ImGuiKey_F11))
    {
        ImGuiToast toast(ImGuiToastType_Info, "Hello, world!");
        ImGui::PushNotification(toast);
    }
}

void hub::on_script_recompile(rtti::context& ctx, const std::string& protocol, uint64_t version)
{
    panels_.get_console_log_panel().on_recompile();
}

void hub::on_play_before_begin(rtti::context& ctx)
{
    (void)ctx;
    if(auto* game_window = ImGui::FindWindowByName(GAME_VIEW))
    {
        ImGui::FocusWindow(game_window);
    }
}

void hub::on_play_begin(rtti::context& ctx)
{
    panels_.get_console_log_panel().on_play();
    ImGui::FocusWindow(ImGui::FindWindowByName(GAME_VIEW));

}

void hub::on_play_after_end(rtti::context& ctx)
{
    auto& ev = ctx.get_cached<events>();
    ImGui::FocusWindow(ImGui::FindWindowByName(SCENE_VIEW));
}

void hub::on_os_event(rtti::context& ctx, os::event& e)
{
    auto& pm = ctx.get_cached<project_manager>();
    if(!pm.has_open_project())
    {
        return;
    }

    if(e.type == os::events::drop_position)
    {
        panels_.set_external_drop_position(ImVec2{e.drop.x, e.drop.y});
    }
    else if(e.type == os::events::drop_begin)
    {
        panels_.set_external_drop_in_progress(true);
    }
    else if(e.type == os::events::drop_file)
    {
        panels_.add_external_drop_file(e.drop.data);
    }
    else if(e.type == os::events::drop_complete)
    {
        panels_.set_external_drop_in_progress(false);
    }
    else if(e.type == os::events::window)
    {
        if(e.window.type == os::window_event_id::close)
        {
            auto window_id = e.window.window_id;

            auto& rend = ctx.get_cached<renderer>();
            auto render_window = rend.get_main_window();
            if(render_window)
            {
                if(render_window->get_window().get_id() == window_id)
                {
                    editor_actions::prompt_save_scene(ctx, []() {
                        engine::interrupt();
                    });

                    e = {};
                }
            }
        }
    }
}

void hub::on_opened_project_render(rtti::context& ctx)
{
    panels_.on_frame_ui_render(ctx);
}

void hub::on_start_page_render(rtti::context& ctx)
{
    start_page_.draw(ctx);
}

} // namespace unravel
