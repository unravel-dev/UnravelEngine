#pragma once
#include "../panel_base.h"
#include <editor/imgui/integration/imgui.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include "../viewport_stats_overlay.h"
#include "../viewport_toolbar.h"
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
    void set_visible_force(bool visible);

    void draw_ui(rtti::context& ctx) override;
    void on_after_render(rtti::context& ctx) override;
    auto get_window_flags() const -> ImGuiWindowFlags override;

private:
    // Floating toolbar (viewport_toolbar), one bar on the right like the view bar of the scene.
    /// Fades the bar out while the game plays, unless the pointer is at the top edge or a
    /// dropdown is open: the image belongs to the game then, HUD corners included. Only a
    /// passive frame rate readout stays.
    void update_toolbar_visibility(rtti::context& ctx, const ImRect& area);
    void draw_toolbar(rtti::context& ctx, const ImRect& area);
    void draw_resolution_dropdown(rtti::context& ctx);
    void draw_ui_debugger_toggle(rtti::context& ctx);
    auto begin_panel(const char* name, ImGuiWindowFlags flags) -> bool override;

    bool is_visible_force_{};
    int visualize_passes_{-1};
    int m_skip_frames_{0};

    imgui_panels* parent_{};
    viewport_stats_overlay::state stats_overlay_state_{};
    visualization_menu::state visualization_menu_state_{};

    viewport_toolbar::layout_state toolbar_layout_{};
    /// Opacity of the toolbar, 0 while it stays out of the way of a running game.
    float toolbar_alpha_{1.0f};
};
} // namespace unravel
