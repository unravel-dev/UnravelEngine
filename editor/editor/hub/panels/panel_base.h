#pragma once

#include <context/context.hpp>
#include <editor/imgui/integration/imgui.h>

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

    std::string name_;
    std::string name_fullscreen_;
    bool is_visible_{};
    bool is_focused_{};
    bool is_fullscreen_{};
    bool request_focus_{};
};

} // namespace unravel
