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

    void on_frame_ui_render(rtti::context& ctx, float header_size);

private:
    void draw_menubar_child(rtti::context& ctx);
    void draw_play_toolbar(rtti::context& ctx, float header_size);
    void draw_about_window(rtti::context& ctx);

    void draw_project_badge(rtti::context& ctx,
                            const ImVec2& window_pos,
                            const ImVec2& window_size,
                            float header_size,
                            const ImVec2& item_spacing);
    void draw_left_zone(rtti::context& ctx);
    void draw_center_zone(rtti::context& ctx);
    void draw_right_zone(rtti::context& ctx);
    auto calc_right_zone_width(const ImVec2& frame_padding, const ImVec2& item_spacing) -> float;
    auto calc_center_zone_width(const ImVec2& frame_padding, const ImVec2& item_spacing) -> float;

    imgui_panels* parent_{};
    bool show_about_window_ = false;
    bool play_splash_in_editor_ = false;
};
} // namespace unravel
