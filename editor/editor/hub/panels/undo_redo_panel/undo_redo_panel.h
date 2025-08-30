#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

namespace unravel
{
class imgui_panels;

class undo_redo_panel
{
public:
    undo_redo_panel(imgui_panels* parent);
    void show(bool show = true);

    void on_frame_ui_render(rtti::context& ctx);

private:
    imgui_panels* parent_;
    bool visible_ = false;
};
} // namespace unravel
