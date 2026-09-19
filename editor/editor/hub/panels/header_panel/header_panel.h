#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <editor/imgui/integration/imgui.h>
#include <editor/shortcuts.h>

namespace unravel
{

class imgui_panels;

class header_panel
{
public:
    header_panel(imgui_panels* parent);

    /// Height of the header: the menu bar row plus the play toolbar under it.
    static auto calc_height() -> float;

    void on_frame_ui_render(rtti::context& ctx, float header_size);

private:
    void draw_menubar_child(rtti::context& ctx);
    void draw_play_toolbar(rtti::context& ctx);
    void draw_about_window(rtti::context& ctx);

    void draw_project_badge(rtti::context& ctx);
    // The play toolbar, a panel_toolbar strip: deploy on the left, the play controls in the
    // middle, the simulation and frame pacing on the right.
    void draw_deploy_button(rtti::context& ctx);
    void draw_transport_controls(rtti::context& ctx);
    void draw_play_options(rtti::context& ctx);
    void draw_time_scale(rtti::context& ctx);
    void draw_frame_pacing(rtti::context& ctx);

    imgui_panels* parent_{};
    bool show_about_window_ = false;
    bool play_splash_in_editor_ = false;
};
} // namespace unravel
