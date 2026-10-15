#pragma once

#include <context/context.hpp>
#include <editor/imgui/integration/imgui.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace unravel
{

class panel_base
{
public:
    explicit panel_base(const char* name);
    virtual ~panel_base() = default;

    auto is_visible() const -> bool { return is_visible_; }
    void set_visible(bool visible) { is_visible_ = visible; }

    /// Renders the panel's view on the next frame even while its tab is hidden (a camera preview,
    /// a capture). Cleared once the panel has rendered.
    void set_visible_force(bool visible) { is_visible_force_ = visible; }

    /// Keeps the panel's view rendering for the next @p frames frames even while its tab is hidden, so a
    /// view that builds on earlier frames (temporal AA, GI) is not torn down between two requests. Extends
    /// a shorter hold, never shortens one.
    void hold_visible_force(uint32_t frames)
    {
        visible_force_frames_ = std::max(visible_force_frames_, frames);
        is_visible_force_ = is_visible_force_ || frames > 0;
    }

    auto is_focused() const -> bool { return is_focused_; }

    auto is_fullscreen() const -> bool { return is_fullscreen_; }
    void set_fullscreen(bool fullscreen) { is_fullscreen_ = fullscreen; }
    void toggle_fullscreen() { is_fullscreen_ = !is_fullscreen_; }

    void focus();
    void on_frame_ui_render(rtti::context& ctx);

    auto get_name() const -> const std::string& { return name_; }

protected:
    virtual auto get_window_name() const -> const char*;
    void render_panel(rtti::context& ctx, const char* name, ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar);

    virtual void draw_ui(rtti::context& ctx) = 0;
    virtual auto get_window_flags() const -> ImGuiWindowFlags { return ImGuiWindowFlags_MenuBar; }
    virtual void on_before_render(rtti::context& ctx);
    virtual void on_after_render(rtti::context& ctx);

    virtual bool begin_panel(const char* name, ImGuiWindowFlags flags);
    virtual void end_panel(const char* name);

    /// Called once the panel rendered its view: a one-frame force ends, a hold counts down.
    void end_forced_frame()
    {
        visible_force_frames_ = visible_force_frames_ > 0 ? visible_force_frames_ - 1 : 0;
        is_visible_force_ = visible_force_frames_ > 0;
    }

    std::string name_;
    std::string name_fullscreen_;
    bool is_visible_{};
    bool is_visible_force_{};
    uint32_t visible_force_frames_{};
    bool is_focused_{};
    bool is_fullscreen_{};
    bool request_focus_{};
};

} // namespace unravel
