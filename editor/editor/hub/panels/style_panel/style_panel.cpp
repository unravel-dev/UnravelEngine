#include "style_panel.h"
#include "../panel.h"
#include <editor/imgui/integration/imgui_style.h>

namespace unravel
{

style_panel::style_panel(imgui_panels* parent) : parent_(parent)
{
}

void style_panel::init(rtti::context& ctx)
{
}

void style_panel::show(bool show)
{
    visible_ = show;
}

void style_panel::on_frame_ui_render()
{
    if(!visible_)
        return;
    ImGui::Begin("Style", &visible_, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Select a theme:");
    ImGui::Separator();
    for(int index = 0; index < static_cast<int>(imgui_style::theme::count); ++index)
    {
        const auto value = static_cast<imgui_style::theme>(index);
        if(ImGui::Button(imgui_style::get_theme_name(value)))
        {
            imgui_style::set_theme(value);
        }
    }
    ImGui::End();
}

} // namespace unravel
