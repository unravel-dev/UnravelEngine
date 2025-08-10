#pragma once
#include <editor/imgui/integration/imgui.h>

#include "../entity_panel.h"
#include <base/basetypes.hpp>
#include <context/context.hpp>


namespace unravel
{
// Forward declarations
class editing_manager;
class scene;
class hierarchy_panel : public entity_panel
{
public:
    hierarchy_panel(imgui_panels* parent);

    void init(rtti::context& ctx);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

private:
    // UI drawing functions
    void draw_prefab_mode_header(rtti::context& ctx) const;
    auto get_scene_display_name(const editing_manager& em, scene* target_scene) const -> std::string;
    void draw_scene_hierarchy(rtti::context& ctx) const;
    void handle_window_empty_click(rtti::context& ctx) const;
};
} // namespace unravel
