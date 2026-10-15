#pragma once
#include "../panel_base.h"
#include <editor/imgui/integration/imgui.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/ecs.h>
#include <graphics/frame_buffer.h>
#include "../viewport_stats_overlay.h"
#include "../panel_toolbar.h"
#include "../visualization_menu.h"

namespace unravel
{
class imgui_panels;
class camera_component;
class game_panel : public panel_base
{
public:
    game_panel(imgui_panels* parent, const char* name);

    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    void on_frame_update(rtti::context& ctx, delta_t dt);
    void on_frame_before_render(rtti::context& ctx, delta_t dt);
    void on_frame_render(rtti::context& ctx, delta_t dt);
    void on_project_opened();

    void draw_ui(rtti::context& ctx) override;
    void on_after_render(rtti::context& ctx) override;
    auto get_window_flags() const -> ImGuiWindowFlags override;

    /**
     * @brief The camera the panel shows: the first camera of the game scene that renders to an
     * output buffer. Captures and tools take the game view from it, so they show what the panel shows.
     * @return The camera entity, or an empty handle while no camera renders.
     */
    auto get_displayed_camera(rtti::context& ctx) const -> entt::handle;
    /// What the displayed camera rendered last, null while no camera renders.
    auto get_output(rtti::context& ctx) const -> gfx::frame_buffer::ptr;

    /**
     * @brief Renders the game view at exactly @p width x @p height pixels, shown scaled into the
     * panel; 0 x 0 follows the resolution setting and the panel again. Input maps to the rendered
     * pixels either way.
     */
    void set_forced_render_size(uint32_t width, uint32_t height);
    auto get_forced_render_size() const -> std::pair<uint32_t, uint32_t>;

private:
    /**
     * @brief Sizes the game view and every camera of @p scn from the panel area of the last draw.
     *
     * Runs in the frame update ahead of the scripts, where the player sizes them too, so a
     * frame's scripts read the size that frame renders at. The area is only known once the
     * panel has been drawn; until then a free-aspect preset leaves the sizes as they are.
     */
    void apply_view_size(rtti::context& ctx, scene& scn) const;
    /// Draws the displayed camera's image and points the input, debug view and labels at it.
    void draw_camera_image(rtti::context& ctx, entt::handle camera, const ImVec2& area_size);

    // Floating toolbar (panel_toolbar), one bar on the right like the view bar of the scene.
    /// Fades the bar out while the game plays, unless the pointer is at the top edge or a
    /// dropdown is open: the image belongs to the game then, HUD corners included. Only a
    /// passive frame rate readout stays.
    void update_toolbar_visibility(rtti::context& ctx, const ImRect& area);
    void draw_toolbar(rtti::context& ctx, const ImRect& area);
    void draw_resolution_dropdown(rtti::context& ctx);
    void draw_ui_debugger_toggle(rtti::context& ctx);
    auto begin_panel(const char* name, ImGuiWindowFlags flags) -> bool override;

    int visualize_passes_{-1};
    int m_skip_frames_{0};

    imgui_panels* parent_{};
    viewport_stats_overlay::state stats_overlay_state_{};
    visualization_menu::state visualization_menu_state_{};

    panel_toolbar::layout_state toolbar_layout_{};
    /// Opacity of the toolbar, 0 while it stays out of the way of a running game.
    float toolbar_alpha_{1.0f};
    /// Content area of the panel at its last draw, the area the game view is sized against.
    ImVec2 view_area_size_{};
    /// Render size set by set_forced_render_size; 0 x 0 when not forced.
    uint32_t forced_render_width_{0};
    uint32_t forced_render_height_{0};
};
} // namespace unravel
