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
    hierarchy_panel(imgui_panels* parent, const char* name);

    void init(rtti::context& ctx);

    void draw_ui(rtti::context& ctx) override;
    void on_after_render(rtti::context& ctx) override;

    auto get_window_flags() const -> ImGuiWindowFlags override;

private:
    // UI drawing functions
    void draw_prefab_mode_header(rtti::context& ctx) const;
    auto get_scene_display_name(const editing_manager& em, scene* target_scene) const -> std::string;
    void draw_scene_hierarchy(rtti::context& ctx) const;
    void handle_window_empty_click(rtti::context& ctx) const;
};
} // namespace unravel
