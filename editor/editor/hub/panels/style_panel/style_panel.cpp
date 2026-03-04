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
    if(ImGui::Button("Unity"))
    {
        imgui_style::set_unity_theme();
    }
    if(ImGui::Button("Unity Inspired"))
    {
        imgui_style::set_unity_inspired_theme();
    }
    if(ImGui::Button("Modern Purple"))
    {
        imgui_style::set_modern_purple_theme();
    }
    if(ImGui::Button("Warm Amber"))
    {
        imgui_style::set_warm_amber_theme();
    }
    if(ImGui::Button("Cool Blue"))
    {
        imgui_style::set_cool_blue_theme();
    }
    if(ImGui::Button("Minimalist Green"))
    {
        imgui_style::set_minimalist_green_theme();
    }
    if(ImGui::Button("Professional Dark"))
    {
        imgui_style::set_professional_dark_theme();
    }
    if(ImGui::Button("Dark Theme"))
    {
        imgui_style::set_dark_theme();
    }
    if(ImGui::Button("Dark Red Theme"))
    {
        imgui_style::set_dark_theme_red();
    }
    if(ImGui::Button("Photoshop Theme"))
    {
        imgui_style::set_photoshop_theme();
    }
    ImGui::End();
}

} // namespace unravel
