#pragma once
#include <editor/imgui/integration/imgui.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/settings/settings.h>

namespace unravel
{
class camera_component; // Forward declaration
class game_panel
{
public:
    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    void on_frame_update(rtti::context& ctx, delta_t dt);
    void on_frame_before_render(rtti::context& ctx, delta_t dt);
    void on_frame_render(rtti::context& ctx, delta_t dt);
    void on_frame_ui_render(rtti::context& ctx, const char* name);
    void set_visible(bool visible);

    void set_visible_force(bool visible);

private:
    void draw_ui(rtti::context& ctx);
    void draw_menubar(rtti::context& ctx);

    int current_resolution_index_ = 0;
    void apply_resolution_to_camera(camera_component& camera_comp, const settings::resolution_settings::resolution& res, ImVec2 avail_size);

    bool is_visible_{};
    bool is_visible_force_{};
    int visualize_passes_{-1};
};
} // namespace unravel
