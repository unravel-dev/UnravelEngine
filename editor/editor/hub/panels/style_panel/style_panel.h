#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>


namespace unravel
{
class imgui_panels;

class style_panel
{
public:
    style_panel(imgui_panels* parent);
    void show(bool show = true);

    void init(rtti::context& ctx);
    void on_frame_ui_render();

private:
    imgui_panels* parent_;
    bool visible_ = false;
};
} // namespace unravel