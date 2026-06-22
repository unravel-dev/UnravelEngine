#pragma once
#include "../panel_base.h"
#include <editor/imgui/integration/imgui.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include "../viewport_stats_overlay.h"

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

private:
    void draw_menubar(rtti::context& ctx);
    auto begin_panel(const char* name, ImGuiWindowFlags flags) -> bool override;

    bool is_visible_force_{};
    int visualize_passes_{-1};
    int m_skip_frames_{0};

    imgui_panels* parent_{};
    viewport_stats_overlay::state stats_overlay_state_{};
};
} // namespace unravel
