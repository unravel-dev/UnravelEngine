#pragma once
#include "../entity_panel.h"
#include <editor/imgui/integration/imgui.h>
#include <math/math.h>

#include "gizmos/gizmos_renderer.h"

namespace unravel
{
class scene_panel : public entity_panel

{
public:
    scene_panel(imgui_panels* parent);

    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    void on_frame_update(rtti::context& ctx, delta_t dt);
    void on_frame_render(rtti::context& ctx, delta_t dt);
    void on_frame_before_render(rtti::context& ctx, delta_t dt);
    void on_frame_ui_render(rtti::context& ctx, const char* name);

    auto get_camera() -> entt::handle;
    auto get_center() -> entt::handle;
    void set_visible(bool visible);
    auto is_focused() const -> bool;

    auto get_auto_save_prefab() const -> bool;

private:
    void draw_scene(rtti::context& ctx, delta_t dt);

    void draw_ui(rtti::context& ctx);
    void draw_menubar(rtti::context& ctx);
    void draw_selected_camera(rtti::context& ctx, entt::handle editor_camera, const ImVec2& size);

    // Menu bar drawing functions
    void draw_prefab_mode_header(rtti::context& ctx);
    void draw_transform_tools(editing_manager& em);
    void draw_gizmo_pivot_mode_menu(bool& gizmo_at_center);
    void draw_coordinate_system_menu(editing_manager& em);
    void draw_grid_settings_menu(editing_manager& em);
    void draw_gizmos_settings_menu(editing_manager& em);
    void draw_visualization_menu();
    void draw_snapping_menu(editing_manager& em);
    void draw_inverse_kinematics_menu(editing_manager& em);
    void draw_camera_settings_menu(rtti::context& ctx);
    void draw_framerate_display();

    // UI interaction functions
    void handle_viewport_interaction(rtti::context& ctx, const camera& camera, editing_manager& em);
    void handle_keyboard_shortcuts(editing_manager& em);
    void setup_camera_viewport(camera_component& camera_comp, const ImVec2& size, const ImVec2& pos);
    void draw_scene_viewport(rtti::context& ctx, const ImVec2& size);

    // Handle prefab mode changes
    void handle_prefab_mode_changes(rtti::context& ctx);

    bool is_visible_{};
    bool is_focused_{};
    bool is_dragging_{};
    int visualize_passes_{-1};
    scene panel_scene_{"scene_panel"};

    bool gizmo_at_center_{true};

    float acceleration_{};
    math::vec3 move_dir_{};

    gizmos_renderer gizmos_{};

    // Track prefab mode state
    bool was_prefab_mode_{false};

    // Auto-save prefabs when exiting prefab mode
    bool auto_save_prefab_{true};
};
} // namespace unravel
