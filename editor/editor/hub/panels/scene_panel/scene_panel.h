#pragma once
#include "../entity_panel.h"
#include <editor/imgui/integration/imgui.h>
#include <math/math.h>

#include "gizmos/gizmos_renderer.h"
#include "../viewport_stats_overlay.h"

namespace unravel
{
class scene_panel : public entity_panel

{
public:
    scene_panel(imgui_panels* parent, const char* name);

    void init(rtti::context& ctx);

    auto get_window_name() const -> const char* override;
    void deinit(rtti::context& ctx);

    void on_frame_update(rtti::context& ctx, delta_t dt);
    void on_frame_render(rtti::context& ctx, delta_t dt);
    void on_frame_before_render(rtti::context& ctx, delta_t dt);
    auto get_window_flags() const -> ImGuiWindowFlags override;

    auto get_camera() -> entt::handle;
    auto get_center() -> entt::handle;

    /// Recreate the Scene panel editor camera at the default pose (UI "Reset Camera").
    void reset_camera(rtti::context& ctx);

    auto get_auto_save_prefab() const -> bool;

    void on_project_opened();

    // Drag selection methods
    auto is_drag_selection_active() const -> bool { return is_drag_selecting_; }
    auto get_drag_selection_rect() const -> ImRect { return ImRect(drag_start_pos_, drag_current_pos_); }
    auto get_drag_selection_bounds() const -> std::pair<ImVec2, ImVec2> 
    { 
        return {
            ImVec2(std::min(drag_start_pos_.x, drag_current_pos_.x), std::min(drag_start_pos_.y, drag_current_pos_.y)),
            ImVec2(std::max(drag_start_pos_.x, drag_current_pos_.x), std::max(drag_start_pos_.y, drag_current_pos_.y))
        };
    }

private:
    void draw_scene(rtti::context& ctx, delta_t dt);

    void draw_ui(rtti::context& ctx) override;
    void draw_menubar(rtti::context& ctx);
    void draw_selected_camera(rtti::context& ctx, entt::handle editor_camera, const ImVec2& size);
    auto begin_panel(const char* name, ImGuiWindowFlags flags) -> bool override;

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

      
    // Drag selection helper functions
    void handle_drag_selection(rtti::context& ctx, const camera& camera, editing_manager& em);
    void draw_drag_selection_rect(const ImVec2& start_pos, const ImVec2& current_pos);

    // UI interaction functions
    void handle_viewport_interaction(rtti::context& ctx, const camera& camera, editing_manager& em);
    void handle_keyboard_shortcuts(editing_manager& em);
    void setup_camera_viewport(camera_component& camera_comp, const ImVec2& size, const ImVec2& pos);
    void draw_scene_viewport(rtti::context& ctx, const ImVec2& size, const ImVec2& pos);

    // Handle prefab mode changes
    void handle_prefab_mode_changes(rtti::context& ctx);

    bool is_dragging_{};
    int visualize_passes_{-1};
    int current_resolution_index_{0};
    scene panel_scene_{"scene_panel"};

    bool gizmo_at_center_{true};

    float acceleration_{};
    math::vec3 move_dir_{};

    dd_2d_raii dd_2d_{};
    gizmos_renderer gizmos_{};

    // Track prefab mode state
    bool was_prefab_mode_{false};

    // Auto-save prefabs when exiting prefab mode
    bool auto_save_prefab_{true};

    // Drag selection state
    bool is_drag_selecting_{false};
    ImVec2 drag_start_pos_{};
    ImVec2 drag_current_pos_{};
    bool was_using_gizmo_{false};

    std::string fullscreen_name_{};

    viewport_stats_overlay::state stats_overlay_state_{};

    int m_skip_frames_{0};
};
} // namespace unravel
