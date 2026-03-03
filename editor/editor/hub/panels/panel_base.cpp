#include "panel_base.h"
#include "imgui/imgui_internal.h"

#include <imgui/imgui.h>

namespace unravel
{

panel_base::panel_base(const char* name) : name_(name)
{
    name_fullscreen_ = name_ + " Fullscreen";
}

void panel_base::focus()
{
    request_focus_ = true;
}

auto panel_base::get_window_name() const -> const char*
{
    if(is_fullscreen())
    {
        return name_fullscreen_.c_str();
    }
    return name_.c_str();
}

void panel_base::on_frame_ui_render(rtti::context& ctx)
{
    on_before_render(ctx);
    render_panel(ctx, get_window_name(), get_window_flags());
    on_after_render(ctx);
}

void panel_base::on_before_render(rtti::context& ctx)
{
    (void)ctx;
}

void panel_base::on_after_render(rtti::context& ctx)
{
    (void)ctx;
}

void panel_base::render_panel(rtti::context& ctx, const char* name, ImGuiWindowFlags flags)
{
    if(begin_panel(name, flags))
    {
        draw_ui(ctx);
    }
    end_panel(name);
}

bool panel_base::begin_panel(const char* name, ImGuiWindowFlags flags)
{

    if(request_focus_)
    {
        ImGui::SetNextWindowFocus();
        request_focus_ = false;
    }
    bool open = ImGui::Begin(name, nullptr, flags | ImGuiWindowFlags_NoFocusOnAppearing);
    if(open)
    {
        is_focused_ = ImGui::IsWindowFocused();
        set_visible(true);
    }
    else
    {
        is_focused_ = false;
        set_visible(false);
    }

    return open;
}

void panel_base::end_panel(const char* name)
{
    ImGui::End();
}

} // namespace unravel
