#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <editor/editing/editor_actions.h>

namespace unravel
{

class imgui_panels;

class project_settings_panel
{
public:
    project_settings_panel(imgui_panels* parent);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

    void show(bool s, const std::string& hint);

private:
    void draw_ui(rtti::context& ctx);

    imgui_panels* parent_{};
    bool show_request_{};
    std::string hint_{};

    using callback_t = std::function<void(rtti::context&)>;

    struct setting_entry
    {
        std::string id;
        callback_t callback;
    };

    setting_entry selected_entry_{};
};
} // namespace unravel
