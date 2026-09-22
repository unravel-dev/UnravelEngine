#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <editor/editing/editor_actions.h>
#include <editor/hub/panels/settings_view.h>

namespace unravel
{

class imgui_panels;

class project_settings_panel
{
public:
    project_settings_panel(imgui_panels* parent);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

    /// Shows or hides the window. A hint that names a category opens it on that category.
    void show(bool s, const std::string& hint);

private:
    imgui_panels* parent_{};
    bool visible_{};
    settings_view view_;
};
} // namespace unravel
