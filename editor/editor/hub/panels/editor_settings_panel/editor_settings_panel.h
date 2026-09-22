#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <editor/editing/editor_actions.h>
#include <editor/hub/panels/settings_view.h>

namespace unravel
{

class imgui_panels;

class editor_settings_panel
{
public:
    editor_settings_panel(imgui_panels* parent);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

    void show(bool s);

private:
    imgui_panels* parent_{};
    bool show_request_{};
    settings_view view_;
};
} // namespace unravel
