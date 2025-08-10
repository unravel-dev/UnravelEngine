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

    imgui_panels* parent_{};
    bool show_about_window_ = false;
};
} // namespace unravel
