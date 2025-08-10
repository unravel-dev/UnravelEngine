#include "style_panel.h"
#include "../panel.h"
#include <editor/imgui/integration/imgui_notify.h>

namespace unravel
{

style_panel::style_panel(imgui_panels* parent) : parent_(parent)
{
}

void style_panel::init(rtti::context& ctx)
{
    set_unity_theme();
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
        this->set_unity_theme();
    }
    if(ImGui::Button("Unity Inspired"))
    {
        this->set_unity_inspired_theme();
    }
    if(ImGui::Button("Modern Purple"))
    {
        this->set_modern_purple_theme();
    }
    if(ImGui::Button("Warm Amber"))
    {
        this->set_warm_amber_theme();
    }
    if(ImGui::Button("Cool Blue"))
    {
        this->set_cool_blue_theme();
    }
    if(ImGui::Button("Minimalist Green"))
    {
        this->set_minimalist_green_theme();
    }
    if(ImGui::Button("Professional Dark"))
    {
        this->set_professional_dark_theme();
    }
    if(ImGui::Button("Dark Theme"))
    {
        this->set_dark_theme();
    }
    if(ImGui::Button("Dark Red Theme"))
    {
        this->set_dark_theme_red();
    }
    if(ImGui::Button("Photoshop Theme"))
    {
        this->set_photoshop_theme();
    }
    ImGui::End();
}

// THEME IMPLEMENTATIONS

void style_panel::set_photoshop_theme()
{
    // Photoshop style by Derydoca from ImThemes
    ImGuiStyle& style = ImGui::GetStyle();

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.6000000238418579f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.WindowRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowMinSize = ImVec2(32.0f, 32.0f);
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_Left;
    style.ChildRounding = 4.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupRounding = 2.0f;
    style.PopupBorderSize = 1.0f;
    style.FramePadding = ImVec2(4.0f, 3.0f);
    style.FrameRounding = 2.0f;
    style.FrameBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.CellPadding = ImVec2(4.0f, 2.0f);
    style.IndentSpacing = 21.0f;
    style.ColumnsMinSpacing = 6.0f;
    style.ScrollbarSize = 13.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabMinSize = 7.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.TabBorderSize = 1.0f;
    style.TabCloseButtonMinWidthUnselected = 0.0f;
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.4980392158031464f, 0.4980392158031464f, 0.4980392158031464f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1764705926179886f, 0.1764705926179886f, 0.1764705926179886f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.2784313857555389f, 0.2784313857555389f, 0.2784313857555389f, 0.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.2627451121807098f, 0.2627451121807098f, 0.2627451121807098f, 1.0f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1568627506494522f, 0.1568627506494522f, 0.1568627506494522f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.2784313857555389f, 0.2784313857555389f, 0.2784313857555389f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1450980454683304f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1450980454683304f, 1.0f);
    style.Colors[ImGuiCol_TitleBgCollapsed] =
        ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1450980454683304f, 1.0f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.1921568661928177f, 0.1921568661928177f, 0.1921568661928177f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.1568627506494522f, 0.1568627506494522f, 0.1568627506494522f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.2745098173618317f, 0.2745098173618317f, 0.2745098173618317f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] =
        ImVec4(0.2980392277240753f, 0.2980392277240753f, 0.2980392277240753f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.1560000032186508f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.3910000026226044f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.4666666686534882f, 0.4666666686534882f, 0.4666666686534882f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.4666666686534882f, 0.4666666686534882f, 0.4666666686534882f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.2627451121807098f, 0.2627451121807098f, 0.2627451121807098f, 1.0f);
    style.Colors[ImGuiCol_SeparatorHovered] =
        ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 1.0f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.0f, 1.0f, 1.0f, 0.25f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.6700000166893005f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.09411764889955521f, 0.09411764889955521f, 0.09411764889955521f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.3490196168422699f, 0.3490196168422699f, 0.3490196168422699f, 1.0f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.1921568661928177f, 0.1921568661928177f, 0.1921568661928177f, 1.0f);
    style.Colors[ImGuiCol_TabDimmed] = ImVec4(0.09411764889955521f, 0.09411764889955521f, 0.09411764889955521f, 1.0f);
    style.Colors[ImGuiCol_TabDimmedSelected] =
        ImVec4(0.1921568661928177f, 0.1921568661928177f, 0.1921568661928177f, 1.0f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.4666666686534882f, 0.4666666686534882f, 0.4666666686534882f, 1.0f);
    style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.5843137502670288f, 0.5843137502670288f, 0.5843137502670288f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
    style.Colors[ImGuiCol_TableBorderStrong] =
        ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
    style.Colors[ImGuiCol_TableBorderLight] =
        ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.1560000032186508f);
    style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_NavCursor] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5860000252723694f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5860000252723694f);
}

void style_panel::set_dark_theme()
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    auto& colors = ImGui::GetStyle().Colors;

    //========================================================
    /// Colours

    auto accent = ImColor(236, 158, 36, 255);
    auto highlight = ImColor(39, 185, 242, 255);
    auto niceBlue = ImColor(83, 232, 254, 255);
    auto compliment = ImColor(78, 151, 166, 255);
    auto background = ImColor(36, 36, 36, 255);
    auto backgroundDark = ImColor(26, 26, 26, 255);
    auto titlebar = ImColor(21, 21, 21, 255);
    auto titlebarOrange = ImColor(186, 66, 30, 255);
    auto titlebarGreen = ImColor(18, 88, 30, 255);
    auto titlebarRed = ImColor(185, 30, 30, 255);
    auto propertyField = ImColor(15, 15, 15, 255);
    auto text = ImColor(255, 255, 255, 255);
    auto textBrighter = ImColor(210, 210, 210, 255);
    auto textDarker = ImColor(128, 128, 128, 255);
    auto textError = ImColor(230, 51, 51, 255);
    auto muted = ImColor(77, 77, 77, 255);
    auto groupHeader = ImColor(47, 47, 47, 255);
    auto selection = ImColor(237, 192, 119, 255);
    auto selectionMuted = ImColor(237, 201, 142, 23);
    auto backgroundPopup = ImColor(50, 50, 50, 255);
    auto validPrefab = ImColor(82, 179, 222, 255);
    auto invalidPrefab = ImColor(222, 43, 43, 255);
    auto missingMesh = ImColor(230, 102, 76, 255);
    auto meshNotSet = ImColor(250, 101, 23, 255);

    // Headers
    colors[ImGuiCol_Header] = groupHeader;
    colors[ImGuiCol_HeaderHovered] = groupHeader;
    colors[ImGuiCol_HeaderActive] = groupHeader;

    // Buttons
    colors[ImGuiCol_Button] = ImColor(56, 56, 56, 200);
    colors[ImGuiCol_ButtonHovered] = ImColor(70, 70, 70, 255);
    colors[ImGuiCol_ButtonActive] = ImColor(56, 56, 56, 150);

    // Frame BG
    colors[ImGuiCol_FrameBg] = propertyField;
    colors[ImGuiCol_FrameBgHovered] = propertyField;
    colors[ImGuiCol_FrameBgActive] = propertyField;

    // Tabs
    colors[ImGuiCol_Tab] = titlebar;
    colors[ImGuiCol_TabHovered] = ImColor(255, 225, 135, 30);
    colors[ImGuiCol_TabSelected] = ImColor(255, 225, 135, 60);
    colors[ImGuiCol_TabDimmed] = titlebar;
    colors[ImGuiCol_TabDimmedSelected] = colors[ImGuiCol_TabHovered];

    // Title
    colors[ImGuiCol_TitleBg] = titlebar;
    colors[ImGuiCol_TitleBgActive] = titlebar;
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

    // Resize Grip
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.0f);

    // Check Mark
    colors[ImGuiCol_CheckMark] = ImColor(200, 200, 200, 255);

    // Slider
    colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.51f, 0.51f, 0.7f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.66f, 0.66f, 0.66f, 1.0f);

    // Text
    colors[ImGuiCol_Text] = text;

    // Checkbox
    colors[ImGuiCol_CheckMark] = text;

    // Separator
    colors[ImGuiCol_Separator] = backgroundDark;
    colors[ImGuiCol_SeparatorActive] = highlight;
    colors[ImGuiCol_SeparatorHovered] = ImColor(39, 185, 242, 150);

    // Window Background
    colors[ImGuiCol_WindowBg] = titlebar;
    colors[ImGuiCol_ChildBg] = background;
    colors[ImGuiCol_PopupBg] = backgroundPopup;
    colors[ImGuiCol_Border] = backgroundDark;

    // Tables
    colors[ImGuiCol_TableHeaderBg] = groupHeader;
    colors[ImGuiCol_TableBorderLight] = backgroundDark;

    // Menubar
    colors[ImGuiCol_MenuBarBg] = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};

    //========================================================
    /// Style
    style.FrameRounding = 2.5f;
    style.FrameBorderSize = 1.0f;
    style.IndentSpacing = 11.0f;

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular
    // ones.
    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, style.Colors[ImGuiCol_WindowBg].w);
}

void style_panel::set_dark_theme_red()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = ImGui::GetStyle().Colors;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    // style.AntiAliasedFill = false;
    // style.WindowRounding = 0.0f;
    style.TabRounding = 3.0f;
    // style.ChildRounding = 0.0f;
    style.PopupRounding = 3.0f;
    style.FrameRounding = 3.0f;
    // style.ScrollbarRounding = 5.0f;
    style.FramePadding = ImVec2(8, 2);
    style.WindowPadding = ImVec2(8, 8);
    style.CellPadding = ImVec2(9, 2);
    // style.ItemInnerSpacing = ImVec2(8, 4);
    // style.ItemInnerSpacing = ImVec2(5, 4);
    // style.GrabRounding = 6.0f;
    // style.GrabMinSize     = 6.0f;
    style.ChildBorderSize = 1.0f;
    // style.TabBorderSize = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.04f, 0.04f, 0.04f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.44f, 0.26f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.47f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.47f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.47f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.74f, 0.74f, 0.74f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.49f, 0.62f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.37f, 0.53f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.47f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.43f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.49f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.44f, 0.44f, 0.44f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.58f, 0.58f, 0.58f, 1.00f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.73f, 0.73f, 0.73f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.24f, 0.25f, 0.26f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.47f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.69f, 0.15f, 0.29f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.47f, 0.20f, 0.20f, 0.71f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.58f, 0.23f, 0.23f, 0.71f);
    colors[ImGuiCol_NavCursor] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.61f);

    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void style_panel::set_professional_dark_theme()
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    auto& colors = ImGui::GetStyle().Colors;

    //========================================================
    /// Professional Color Palette
    /// Designed for reduced eye strain and enhanced visual hierarchy

    // Base colors - Deep slate blue-grays for sophisticated appearance
    auto backgroundDeepest = ImColor(16, 20, 26, 255);  // Main window backgrounds
    auto backgroundDeep = ImColor(22, 27, 34, 255);     // Panel backgrounds
    auto backgroundMedium = ImColor(32, 39, 49, 255);   // Input fields, content areas
    auto backgroundElevated = ImColor(42, 51, 64, 255); // Headers, elevated surfaces
    auto backgroundPopup = ImColor(26, 32, 41, 255);    // Popups, tooltips

    // Primary accent - Sophisticated teal for selections and interactive elements
    auto primaryAccent = ImColor(78, 205, 196, 255);       // Main accent color
    auto primaryAccentHover = ImColor(88, 215, 206, 255);  // Hover state
    auto primaryAccentActive = ImColor(68, 185, 176, 255); // Active/pressed state
    auto primaryAccentMuted = ImColor(78, 205, 196, 80);   // Subtle backgrounds

    // Secondary accent - Warm amber for warnings and important elements
    auto secondaryAccent = ImColor(249, 202, 36, 255); // Warning/important elements
    auto secondaryAccentHover = ImColor(255, 212, 46, 255);
    auto secondaryAccentMuted = ImColor(249, 202, 36, 60);

    // Status colors
    auto successColor = ImColor(38, 208, 206, 255); // Success states
    auto errorColor = ImColor(255, 107, 107, 255);  // Error states
    auto warningColor = ImColor(255, 159, 67, 255); // Warning states

    // Text colors - High contrast for readability
    auto textPrimary = ImColor(248, 248, 248, 255);   // Primary text
    auto textSecondary = ImColor(200, 200, 200, 255); // Secondary text
    auto textMuted = ImColor(140, 140, 140, 255);     // Muted/disabled text
    auto textAccent = ImColor(78, 205, 196, 255);     // Accent text

    // Border and separator colors
    auto borderLight = ImColor(52, 61, 74, 255);  // Light borders
    auto borderMedium = ImColor(42, 51, 64, 255); // Medium borders
    auto borderDark = ImColor(22, 27, 34, 255);   // Dark borders/separators

    //========================================================
    /// Window and background colors

    colors[ImGuiCol_WindowBg] = backgroundDeep;
    colors[ImGuiCol_ChildBg] = backgroundMedium;
    colors[ImGuiCol_PopupBg] = backgroundPopup;
    colors[ImGuiCol_Border] = borderMedium;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_MenuBarBg] = backgroundDeepest;

    //========================================================
    /// Text colors

    colors[ImGuiCol_Text] = textPrimary;
    colors[ImGuiCol_TextDisabled] = textMuted;
    colors[ImGuiCol_TextSelectedBg] = primaryAccentMuted;

    //========================================================
    /// Interactive elements

    // Buttons
    colors[ImGuiCol_Button] = backgroundElevated;
    colors[ImGuiCol_ButtonHovered] = primaryAccentMuted;
    colors[ImGuiCol_ButtonActive] = primaryAccentActive;

    // Headers and collapsible sections
    colors[ImGuiCol_Header] = backgroundElevated;
    colors[ImGuiCol_HeaderHovered] = primaryAccentMuted;
    colors[ImGuiCol_HeaderActive] = primaryAccent;

    // Frame backgrounds (input fields, etc.)
    colors[ImGuiCol_FrameBg] = backgroundMedium;
    colors[ImGuiCol_FrameBgHovered] = backgroundElevated;
    colors[ImGuiCol_FrameBgActive] = primaryAccentMuted;

    //========================================================
    /// Title bars and tabs

    colors[ImGuiCol_TitleBg] = backgroundDeepest;
    colors[ImGuiCol_TitleBgActive] = backgroundDeep;
    colors[ImGuiCol_TitleBgCollapsed] = backgroundDeepest;

    colors[ImGuiCol_Tab] = backgroundDeep;
    colors[ImGuiCol_TabHovered] = primaryAccentMuted;
    colors[ImGuiCol_TabSelected] = primaryAccent;
    colors[ImGuiCol_TabDimmed] = backgroundDeepest;
    colors[ImGuiCol_TabDimmedSelected] = backgroundElevated;

    //========================================================
    /// Scrollbars

    colors[ImGuiCol_ScrollbarBg] = backgroundDeep;
    colors[ImGuiCol_ScrollbarGrab] = backgroundElevated;
    colors[ImGuiCol_ScrollbarGrabHovered] = primaryAccentMuted;
    colors[ImGuiCol_ScrollbarGrabActive] = primaryAccent;

    //========================================================
    /// Sliders and progress bars

    colors[ImGuiCol_SliderGrab] = primaryAccent;
    colors[ImGuiCol_SliderGrabActive] = primaryAccentHover;

    //========================================================
    /// Check marks and selection

    colors[ImGuiCol_CheckMark] = primaryAccent;

    //========================================================
    /// Separators

    colors[ImGuiCol_Separator] = borderDark;
    colors[ImGuiCol_SeparatorHovered] = primaryAccentMuted;
    colors[ImGuiCol_SeparatorActive] = primaryAccent;

    //========================================================
    /// Resize grips

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // Invisible by default
    colors[ImGuiCol_ResizeGripHovered] = primaryAccentMuted;
    colors[ImGuiCol_ResizeGripActive] = primaryAccent;

    //========================================================
    /// Tables

    colors[ImGuiCol_TableHeaderBg] = backgroundElevated;
    colors[ImGuiCol_TableBorderStrong] = borderMedium;
    colors[ImGuiCol_TableBorderLight] = borderLight;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);

    //========================================================
    /// Docking and plotting

    colors[ImGuiCol_DockingPreview] = primaryAccentMuted;
    colors[ImGuiCol_DockingEmptyBg] = backgroundDeepest;

    colors[ImGuiCol_PlotLines] = primaryAccent;
    colors[ImGuiCol_PlotLinesHovered] = primaryAccentHover;
    colors[ImGuiCol_PlotHistogram] = secondaryAccent;
    colors[ImGuiCol_PlotHistogramHovered] = secondaryAccentHover;

    //========================================================
    /// Modal and tooltips

    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);

    //========================================================
    /// Style settings for modern, professional appearance

    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 16.0f;
    style.GrabMinSize = 12.0f;

    // Disable window menu button for cleaner appearance
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // Handle viewport-specific settings
    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void style_panel::set_unity_inspired_theme()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    auto& colors = ImGui::GetStyle().Colors;

    // Unity-inspired color palette - sophisticated grays with subtle blue accents
    auto backgroundDarkest = ImColor(32, 32, 32, 255); // Main backgrounds
    auto backgroundDark = ImColor(42, 42, 42, 255);    // Panel backgrounds
    auto backgroundMedium = ImColor(48, 48, 48, 255);  // Input fields
    auto backgroundLight = ImColor(56, 56, 56, 255);   // Headers, elevated
    auto backgroundPopup = ImColor(38, 38, 38, 255);   // Popups

    // Unity's signature blue accent
    auto primaryAccent = ImColor(58, 121, 187, 255); // Unity blue
    auto primaryAccentHover = ImColor(68, 131, 197, 255);
    auto primaryAccentActive = ImColor(48, 111, 177, 255);
    auto primaryAccentMuted = ImColor(58, 121, 187, 80);

    // Text colors - Unity style
    auto textPrimary = ImColor(210, 210, 210, 255);
    auto textSecondary = ImColor(180, 180, 180, 255);
    auto textMuted = ImColor(128, 128, 128, 255);

    // Borders
    auto borderColor = ImColor(28, 28, 28, 255);
    auto borderLight = ImColor(68, 68, 68, 255);

    // Apply colors
    colors[ImGuiCol_WindowBg] = backgroundDark;
    colors[ImGuiCol_ChildBg] = backgroundMedium;
    colors[ImGuiCol_PopupBg] = backgroundPopup;
    colors[ImGuiCol_Border] = borderColor;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_MenuBarBg] = backgroundDarkest;

    colors[ImGuiCol_Text] = textPrimary;
    colors[ImGuiCol_TextDisabled] = textMuted;
    colors[ImGuiCol_TextSelectedBg] = primaryAccentMuted;

    colors[ImGuiCol_Button] = backgroundLight;
    colors[ImGuiCol_ButtonHovered] = primaryAccentMuted;
    colors[ImGuiCol_ButtonActive] = primaryAccentActive;

    colors[ImGuiCol_Header] = backgroundLight;
    colors[ImGuiCol_HeaderHovered] = primaryAccentMuted;
    colors[ImGuiCol_HeaderActive] = primaryAccent;

    colors[ImGuiCol_FrameBg] = backgroundMedium;
    colors[ImGuiCol_FrameBgHovered] = backgroundLight;
    colors[ImGuiCol_FrameBgActive] = primaryAccentMuted;

    colors[ImGuiCol_TitleBg] = backgroundDarkest;
    colors[ImGuiCol_TitleBgActive] = backgroundDark;
    colors[ImGuiCol_TitleBgCollapsed] = backgroundDarkest;

    colors[ImGuiCol_Tab] = backgroundDark;
    colors[ImGuiCol_TabHovered] = primaryAccentMuted;
    colors[ImGuiCol_TabSelected] = primaryAccent;
    colors[ImGuiCol_TabDimmed] = backgroundDarkest;
    colors[ImGuiCol_TabDimmedSelected] = backgroundLight;

    colors[ImGuiCol_ScrollbarBg] = backgroundDark;
    colors[ImGuiCol_ScrollbarGrab] = backgroundLight;
    colors[ImGuiCol_ScrollbarGrabHovered] = primaryAccentMuted;
    colors[ImGuiCol_ScrollbarGrabActive] = primaryAccent;

    colors[ImGuiCol_CheckMark] = primaryAccent;
    colors[ImGuiCol_SliderGrab] = primaryAccent;
    colors[ImGuiCol_SliderGrabActive] = primaryAccentHover;

    colors[ImGuiCol_Separator] = borderColor;
    colors[ImGuiCol_SeparatorHovered] = borderLight;
    colors[ImGuiCol_SeparatorActive] = primaryAccent;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered] = primaryAccentMuted;
    colors[ImGuiCol_ResizeGripActive] = primaryAccent;

    colors[ImGuiCol_TableHeaderBg] = backgroundLight;
    colors[ImGuiCol_TableBorderStrong] = borderColor;
    colors[ImGuiCol_TableBorderLight] = borderLight;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

    colors[ImGuiCol_DockingPreview] = primaryAccentMuted;
    colors[ImGuiCol_DockingEmptyBg] = backgroundDarkest;

    colors[ImGuiCol_PlotLines] = primaryAccent;
    colors[ImGuiCol_PlotLinesHovered] = primaryAccentHover;
    colors[ImGuiCol_PlotHistogram] = primaryAccent;
    colors[ImGuiCol_PlotHistogramHovered] = primaryAccentHover;

    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);

    // Style settings - Unity-like
    style.WindowRounding = 4.0f;
    style.ChildRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 3.0f);
    style.IndentSpacing = 16.0f;

    style.WindowMenuButtonPosition = ImGuiDir_None;

    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void style_panel::set_modern_purple_theme()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    auto& colors = ImGui::GetStyle().Colors;

    // Modern purple theme - deep purples with violet accents
    auto backgroundDarkest = ImColor(20, 15, 25, 255); // Main backgrounds
    auto backgroundDark = ImColor(28, 22, 35, 255);    // Panel backgrounds
    auto backgroundMedium = ImColor(38, 30, 48, 255);  // Input fields
    auto backgroundLight = ImColor(48, 38, 58, 255);   // Headers, elevated
    auto backgroundPopup = ImColor(25, 20, 30, 255);   // Popups

    // Purple/violet accent scheme
    auto primaryAccent = ImColor(147, 112, 219, 255); // Medium slate blue
    auto primaryAccentHover = ImColor(157, 122, 229, 255);
    auto primaryAccentActive = ImColor(137, 102, 209, 255);
    auto primaryAccentMuted = ImColor(147, 112, 219, 90);

    auto secondaryAccent = ImColor(255, 182, 193, 255); // Light pink
    auto secondaryAccentMuted = ImColor(255, 182, 193, 60);

    // Text colors
    auto textPrimary = ImColor(245, 240, 250, 255);
    auto textSecondary = ImColor(200, 190, 210, 255);
    auto textMuted = ImColor(140, 130, 150, 255);

    // Borders
    auto borderColor = ImColor(15, 10, 20, 255);
    auto borderLight = ImColor(58, 48, 68, 255);

    // Apply colors
    colors[ImGuiCol_WindowBg] = backgroundDark;
    colors[ImGuiCol_ChildBg] = backgroundMedium;
    colors[ImGuiCol_PopupBg] = backgroundPopup;
    colors[ImGuiCol_Border] = borderColor;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_MenuBarBg] = backgroundDarkest;

    colors[ImGuiCol_Text] = textPrimary;
    colors[ImGuiCol_TextDisabled] = textMuted;
    colors[ImGuiCol_TextSelectedBg] = primaryAccentMuted;

    colors[ImGuiCol_Button] = backgroundLight;
    colors[ImGuiCol_ButtonHovered] = primaryAccentMuted;
    colors[ImGuiCol_ButtonActive] = primaryAccentActive;

    colors[ImGuiCol_Header] = backgroundLight;
    colors[ImGuiCol_HeaderHovered] = primaryAccentMuted;
    colors[ImGuiCol_HeaderActive] = primaryAccent;

    colors[ImGuiCol_FrameBg] = backgroundMedium;
    colors[ImGuiCol_FrameBgHovered] = backgroundLight;
    colors[ImGuiCol_FrameBgActive] = primaryAccentMuted;

    colors[ImGuiCol_TitleBg] = backgroundDarkest;
    colors[ImGuiCol_TitleBgActive] = backgroundDark;
    colors[ImGuiCol_TitleBgCollapsed] = backgroundDarkest;

    colors[ImGuiCol_Tab] = backgroundDark;
    colors[ImGuiCol_TabHovered] = primaryAccentMuted;
    colors[ImGuiCol_TabSelected] = primaryAccent;
    colors[ImGuiCol_TabDimmed] = backgroundDarkest;
    colors[ImGuiCol_TabDimmedSelected] = backgroundLight;

    colors[ImGuiCol_ScrollbarBg] = backgroundDark;
    colors[ImGuiCol_ScrollbarGrab] = backgroundLight;
    colors[ImGuiCol_ScrollbarGrabHovered] = primaryAccentMuted;
    colors[ImGuiCol_ScrollbarGrabActive] = primaryAccent;

    colors[ImGuiCol_CheckMark] = primaryAccent;
    colors[ImGuiCol_SliderGrab] = primaryAccent;
    colors[ImGuiCol_SliderGrabActive] = primaryAccentHover;

    colors[ImGuiCol_Separator] = borderColor;
    colors[ImGuiCol_SeparatorHovered] = borderLight;
    colors[ImGuiCol_SeparatorActive] = primaryAccent;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered] = primaryAccentMuted;
    colors[ImGuiCol_ResizeGripActive] = primaryAccent;

    colors[ImGuiCol_TableHeaderBg] = backgroundLight;
    colors[ImGuiCol_TableBorderStrong] = borderColor;
    colors[ImGuiCol_TableBorderLight] = borderLight;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);

    colors[ImGuiCol_DockingPreview] = primaryAccentMuted;
    colors[ImGuiCol_DockingEmptyBg] = backgroundDarkest;

    colors[ImGuiCol_PlotLines] = primaryAccent;
    colors[ImGuiCol_PlotLinesHovered] = primaryAccentHover;
    colors[ImGuiCol_PlotHistogram] = secondaryAccent;
    colors[ImGuiCol_PlotHistogramHovered] = secondaryAccentMuted;

    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);

    // Modern rounded style
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 18.0f;

    style.WindowMenuButtonPosition = ImGuiDir_None;

    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void style_panel::set_warm_amber_theme()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    auto& colors = ImGui::GetStyle().Colors;

    // Warm amber theme - deep browns with golden accents
    auto backgroundDarkest = ImColor(25, 20, 15, 255); // Main backgrounds
    auto backgroundDark = ImColor(35, 28, 20, 255);    // Panel backgrounds
    auto backgroundMedium = ImColor(45, 35, 25, 255);  // Input fields
    auto backgroundLight = ImColor(55, 43, 30, 255);   // Headers, elevated
    auto backgroundPopup = ImColor(30, 24, 18, 255);   // Popups

    // Warm amber/orange accent scheme
    auto primaryAccent = ImColor(255, 165, 0, 255); // Golden orange
    auto primaryAccentHover = ImColor(255, 180, 30, 255);
    auto primaryAccentActive = ImColor(235, 145, 0, 255);
    auto primaryAccentMuted = ImColor(255, 165, 0, 90);

    auto secondaryAccent = ImColor(255, 215, 0, 255); // Gold
    auto secondaryAccentMuted = ImColor(255, 215, 0, 60);

    // Text colors
    auto textPrimary = ImColor(250, 245, 235, 255);
    auto textSecondary = ImColor(210, 200, 185, 255);
    auto textMuted = ImColor(150, 140, 125, 255);

    // Borders
    auto borderColor = ImColor(20, 15, 10, 255);
    auto borderLight = ImColor(65, 53, 38, 255);

    // Apply colors
    colors[ImGuiCol_WindowBg] = backgroundDark;
    colors[ImGuiCol_ChildBg] = backgroundMedium;
    colors[ImGuiCol_PopupBg] = backgroundPopup;
    colors[ImGuiCol_Border] = borderColor;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_MenuBarBg] = backgroundDarkest;

    colors[ImGuiCol_Text] = textPrimary;
    colors[ImGuiCol_TextDisabled] = textMuted;
    colors[ImGuiCol_TextSelectedBg] = primaryAccentMuted;

    colors[ImGuiCol_Button] = backgroundLight;
    colors[ImGuiCol_ButtonHovered] = primaryAccentMuted;
    colors[ImGuiCol_ButtonActive] = primaryAccentActive;

    colors[ImGuiCol_Header] = backgroundLight;
    colors[ImGuiCol_HeaderHovered] = primaryAccentMuted;
    colors[ImGuiCol_HeaderActive] = primaryAccent;

    colors[ImGuiCol_FrameBg] = backgroundMedium;
    colors[ImGuiCol_FrameBgHovered] = backgroundLight;
    colors[ImGuiCol_FrameBgActive] = primaryAccentMuted;

    colors[ImGuiCol_TitleBg] = backgroundDarkest;
    colors[ImGuiCol_TitleBgActive] = backgroundDark;
    colors[ImGuiCol_TitleBgCollapsed] = backgroundDarkest;

    colors[ImGuiCol_Tab] = backgroundDark;
    colors[ImGuiCol_TabHovered] = primaryAccentMuted;
    colors[ImGuiCol_TabSelected] = primaryAccent;
    colors[ImGuiCol_TabDimmed] = backgroundDarkest;
    colors[ImGuiCol_TabDimmedSelected] = backgroundLight;

    colors[ImGuiCol_ScrollbarBg] = backgroundDark;
    colors[ImGuiCol_ScrollbarGrab] = backgroundLight;
    colors[ImGuiCol_ScrollbarGrabHovered] = primaryAccentMuted;
    colors[ImGuiCol_ScrollbarGrabActive] = primaryAccent;

    colors[ImGuiCol_CheckMark] = primaryAccent;
    colors[ImGuiCol_SliderGrab] = primaryAccent;
    colors[ImGuiCol_SliderGrabActive] = primaryAccentHover;

    colors[ImGuiCol_Separator] = borderColor;
    colors[ImGuiCol_SeparatorHovered] = borderLight;
    colors[ImGuiCol_SeparatorActive] = primaryAccent;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered] = primaryAccentMuted;
    colors[ImGuiCol_ResizeGripActive] = primaryAccent;

    colors[ImGuiCol_TableHeaderBg] = backgroundLight;
    colors[ImGuiCol_TableBorderStrong] = borderColor;
    colors[ImGuiCol_TableBorderLight] = borderLight;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);

    colors[ImGuiCol_DockingPreview] = primaryAccentMuted;
    colors[ImGuiCol_DockingEmptyBg] = backgroundDarkest;

    colors[ImGuiCol_PlotLines] = primaryAccent;
    colors[ImGuiCol_PlotLinesHovered] = primaryAccentHover;
    colors[ImGuiCol_PlotHistogram] = secondaryAccent;
    colors[ImGuiCol_PlotHistogramHovered] = secondaryAccentMuted;

    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);

    // Slightly rounded, warm style
    style.WindowRounding = 4.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.CellPadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
    style.IndentSpacing = 17.0f;

    style.WindowMenuButtonPosition = ImGuiDir_None;

    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void style_panel::set_cool_blue_theme()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    auto& colors = ImGui::GetStyle().Colors;

    // Cool blue theme - deep blues with cyan accents
    auto backgroundDarkest = ImColor(15, 20, 30, 255); // Main backgrounds
    auto backgroundDark = ImColor(20, 28, 40, 255);    // Panel backgrounds
    auto backgroundMedium = ImColor(25, 35, 50, 255);  // Input fields
    auto backgroundLight = ImColor(30, 43, 60, 255);   // Headers, elevated
    auto backgroundPopup = ImColor(18, 25, 35, 255);   // Popups

    // Cool blue/cyan accent scheme
    auto primaryAccent = ImColor(0, 174, 239, 255); // Bright cyan
    auto primaryAccentHover = ImColor(30, 184, 249, 255);
    auto primaryAccentActive = ImColor(0, 154, 219, 255);
    auto primaryAccentMuted = ImColor(0, 174, 239, 90);

    auto secondaryAccent = ImColor(64, 224, 255, 255); // Sky blue
    auto secondaryAccentMuted = ImColor(64, 224, 255, 60);

    // Text colors
    auto textPrimary = ImColor(235, 245, 255, 255);
    auto textSecondary = ImColor(185, 200, 220, 255);
    auto textMuted = ImColor(125, 140, 160, 255);

    // Borders
    auto borderColor = ImColor(10, 15, 25, 255);
    auto borderLight = ImColor(40, 53, 70, 255);

    // Apply colors
    colors[ImGuiCol_WindowBg] = backgroundDark;
    colors[ImGuiCol_ChildBg] = backgroundMedium;
    colors[ImGuiCol_PopupBg] = backgroundPopup;
    colors[ImGuiCol_Border] = borderColor;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_MenuBarBg] = backgroundDarkest;

    colors[ImGuiCol_Text] = textPrimary;
    colors[ImGuiCol_TextDisabled] = textMuted;
    colors[ImGuiCol_TextSelectedBg] = primaryAccentMuted;

    colors[ImGuiCol_Button] = backgroundLight;
    colors[ImGuiCol_ButtonHovered] = primaryAccentMuted;
    colors[ImGuiCol_ButtonActive] = primaryAccentActive;

    colors[ImGuiCol_Header] = backgroundLight;
    colors[ImGuiCol_HeaderHovered] = primaryAccentMuted;
    colors[ImGuiCol_HeaderActive] = primaryAccent;

    colors[ImGuiCol_FrameBg] = backgroundMedium;
    colors[ImGuiCol_FrameBgHovered] = backgroundLight;
    colors[ImGuiCol_FrameBgActive] = primaryAccentMuted;

    colors[ImGuiCol_TitleBg] = backgroundDarkest;
    colors[ImGuiCol_TitleBgActive] = backgroundDark;
    colors[ImGuiCol_TitleBgCollapsed] = backgroundDarkest;

    colors[ImGuiCol_Tab] = backgroundDark;
    colors[ImGuiCol_TabHovered] = primaryAccentMuted;
    colors[ImGuiCol_TabSelected] = primaryAccent;
    colors[ImGuiCol_TabDimmed] = backgroundDarkest;
    colors[ImGuiCol_TabDimmedSelected] = backgroundLight;

    colors[ImGuiCol_ScrollbarBg] = backgroundDark;
    colors[ImGuiCol_ScrollbarGrab] = backgroundLight;
    colors[ImGuiCol_ScrollbarGrabHovered] = primaryAccentMuted;
    colors[ImGuiCol_ScrollbarGrabActive] = primaryAccent;

    colors[ImGuiCol_CheckMark] = primaryAccent;
    colors[ImGuiCol_SliderGrab] = primaryAccent;
    colors[ImGuiCol_SliderGrabActive] = primaryAccentHover;

    colors[ImGuiCol_Separator] = borderColor;
    colors[ImGuiCol_SeparatorHovered] = borderLight;
    colors[ImGuiCol_SeparatorActive] = primaryAccent;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered] = primaryAccentMuted;
    colors[ImGuiCol_ResizeGripActive] = primaryAccent;

    colors[ImGuiCol_TableHeaderBg] = backgroundLight;
    colors[ImGuiCol_TableBorderStrong] = borderColor;
    colors[ImGuiCol_TableBorderLight] = borderLight;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);

    colors[ImGuiCol_DockingPreview] = primaryAccentMuted;
    colors[ImGuiCol_DockingEmptyBg] = backgroundDarkest;

    colors[ImGuiCol_PlotLines] = primaryAccent;
    colors[ImGuiCol_PlotLinesHovered] = primaryAccentHover;
    colors[ImGuiCol_PlotHistogram] = secondaryAccent;
    colors[ImGuiCol_PlotHistogramHovered] = secondaryAccentMuted;

    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);

    // Clean, modern style
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    style.WindowPadding = ImVec2(11.0f, 11.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.CellPadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
    style.IndentSpacing = 18.0f;

    style.WindowMenuButtonPosition = ImGuiDir_None;

    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void style_panel::set_minimalist_green_theme()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    auto& colors = ImGui::GetStyle().Colors;

    // Minimalist green theme - very dark grays with subtle green accents
    auto backgroundDarkest = ImColor(18, 20, 18, 255); // Main backgrounds
    auto backgroundDark = ImColor(26, 28, 26, 255);    // Panel backgrounds
    auto backgroundMedium = ImColor(34, 36, 34, 255);  // Input fields
    auto backgroundLight = ImColor(42, 44, 42, 255);   // Headers, elevated
    auto backgroundPopup = ImColor(22, 24, 22, 255);   // Popups

    // Subtle green accent scheme
    auto primaryAccent = ImColor(76, 175, 80, 255); // Material green
    auto primaryAccentHover = ImColor(86, 185, 90, 255);
    auto primaryAccentActive = ImColor(66, 165, 70, 255);
    auto primaryAccentMuted = ImColor(76, 175, 80, 75);

    auto secondaryAccent = ImColor(129, 199, 132, 255); // Light green
    auto secondaryAccentMuted = ImColor(129, 199, 132, 50);

    // Text colors - very high contrast
    auto textPrimary = ImColor(248, 248, 248, 255);
    auto textSecondary = ImColor(200, 200, 200, 255);
    auto textMuted = ImColor(130, 130, 130, 255);

    // Borders - very subtle
    auto borderColor = ImColor(16, 18, 16, 255);
    auto borderLight = ImColor(52, 54, 52, 255);

    // Apply colors
    colors[ImGuiCol_WindowBg] = backgroundDark;
    colors[ImGuiCol_ChildBg] = backgroundMedium;
    colors[ImGuiCol_PopupBg] = backgroundPopup;
    colors[ImGuiCol_Border] = borderColor;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_MenuBarBg] = backgroundDarkest;

    colors[ImGuiCol_Text] = textPrimary;
    colors[ImGuiCol_TextDisabled] = textMuted;
    colors[ImGuiCol_TextSelectedBg] = primaryAccentMuted;

    colors[ImGuiCol_Button] = backgroundLight;
    colors[ImGuiCol_ButtonHovered] = primaryAccentMuted;
    colors[ImGuiCol_ButtonActive] = primaryAccentActive;

    colors[ImGuiCol_Header] = backgroundLight;
    colors[ImGuiCol_HeaderHovered] = primaryAccentMuted;
    colors[ImGuiCol_HeaderActive] = primaryAccent;

    colors[ImGuiCol_FrameBg] = backgroundMedium;
    colors[ImGuiCol_FrameBgHovered] = backgroundLight;
    colors[ImGuiCol_FrameBgActive] = primaryAccentMuted;

    colors[ImGuiCol_TitleBg] = backgroundDarkest;
    colors[ImGuiCol_TitleBgActive] = backgroundDark;
    colors[ImGuiCol_TitleBgCollapsed] = backgroundDarkest;

    colors[ImGuiCol_Tab] = backgroundDark;
    colors[ImGuiCol_TabHovered] = primaryAccentMuted;
    colors[ImGuiCol_TabSelected] = primaryAccent;
    colors[ImGuiCol_TabDimmed] = backgroundDarkest;
    colors[ImGuiCol_TabDimmedSelected] = backgroundLight;

    colors[ImGuiCol_ScrollbarBg] = backgroundDark;
    colors[ImGuiCol_ScrollbarGrab] = backgroundLight;
    colors[ImGuiCol_ScrollbarGrabHovered] = primaryAccentMuted;
    colors[ImGuiCol_ScrollbarGrabActive] = primaryAccent;

    colors[ImGuiCol_CheckMark] = primaryAccent;
    colors[ImGuiCol_SliderGrab] = primaryAccent;
    colors[ImGuiCol_SliderGrabActive] = primaryAccentHover;

    colors[ImGuiCol_Separator] = borderColor;
    colors[ImGuiCol_SeparatorHovered] = borderLight;
    colors[ImGuiCol_SeparatorActive] = primaryAccent;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered] = primaryAccentMuted;
    colors[ImGuiCol_ResizeGripActive] = primaryAccent;

    colors[ImGuiCol_TableHeaderBg] = backgroundLight;
    colors[ImGuiCol_TableBorderStrong] = borderColor;
    colors[ImGuiCol_TableBorderLight] = borderLight;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

    colors[ImGuiCol_DockingPreview] = primaryAccentMuted;
    colors[ImGuiCol_DockingEmptyBg] = backgroundDarkest;

    colors[ImGuiCol_PlotLines] = primaryAccent;
    colors[ImGuiCol_PlotLinesHovered] = primaryAccentHover;
    colors[ImGuiCol_PlotHistogram] = secondaryAccent;
    colors[ImGuiCol_PlotHistogramHovered] = secondaryAccentMuted;

    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);

    // Minimal, clean style
    style.WindowRounding = 2.0f;
    style.ChildRounding = 1.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 1.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 3.0f);
    style.IndentSpacing = 16.0f;

    style.WindowMenuButtonPosition = ImGuiDir_None;

    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void style_panel::set_unity_theme()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    auto& colors = ImGui::GetStyle().Colors;

    // Unity's EXACT color palette - sampled from Unity 2022.3+ screenshots
    auto backgroundDarkest = ImColor(48, 48, 48, 255); // Unity's darkest backgrounds (#303030)
    auto backgroundDark = ImColor(56, 56, 56, 255);    // Unity's main panel color (#383838)
    auto backgroundMedium = ImColor(62, 62, 62, 255);  // Unity's input fields (#3E3E3E)
    auto backgroundLight = ImColor(72, 72, 72, 255);   // Unity's headers/elevated (#484848)
    auto backgroundPopup = ImColor(52, 52, 52, 255);   // Unity's popups (#343434)

    // Unity's classic blue - vibrant and professional
    auto primaryAccent = ImColor(58, 121, 187, 255);       // Unity's signature blue (#3A79BB)
    auto primaryAccentHover = ImColor(78, 141, 207, 255);  // Lighter hover state
    auto primaryAccentActive = ImColor(48, 101, 167, 255); // Darker active state
    auto primaryAccentStrong = ImColor(58, 121, 187, 180); // Strong selection background
    auto primaryAccentMuted = ImColor(58, 121, 187, 60);   // Subtle hover background

    // Unity's high-contrast text - very readable
    auto textPrimary = ImColor(220, 220, 220, 255);   // Unity's bright text (#DCDCDC)
    auto textSecondary = ImColor(180, 180, 180, 255); // Unity's secondary text (#B4B4B4)
    auto textMuted = ImColor(128, 128, 128, 255);     // Unity's disabled text (#808080)
    auto textSelected = ImColor(255, 255, 255, 255);  // Unity's selected text (white)

    // Unity's very subtle borders - barely visible
    auto borderDark = ImColor(35, 35, 35, 255);   // Very dark borders (#232323)
    auto borderMedium = ImColor(80, 80, 80, 255); // Medium borders (#505050)

    // Apply Unity's exact color scheme
    colors[ImGuiCol_WindowBg] = backgroundDark;
    colors[ImGuiCol_ChildBg] = backgroundMedium;
    colors[ImGuiCol_PopupBg] = backgroundPopup;
    colors[ImGuiCol_Border] = borderDark;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_MenuBarBg] = backgroundDarkest;

    // Unity's text styling - high contrast
    colors[ImGuiCol_Text] = textPrimary;
    colors[ImGuiCol_TextDisabled] = textMuted;
    colors[ImGuiCol_TextSelectedBg] = primaryAccentStrong; // Strong blue selection like Unity

    // Unity's button styling
    colors[ImGuiCol_Button] = backgroundLight;
    colors[ImGuiCol_ButtonHovered] = primaryAccentMuted;
    colors[ImGuiCol_ButtonActive] = primaryAccentStrong;

    // Unity's header styling - prominent when active
    colors[ImGuiCol_Header] = backgroundLight;
    colors[ImGuiCol_HeaderHovered] = primaryAccentMuted;
    colors[ImGuiCol_HeaderActive] = primaryAccentStrong; // Strong blue for active headers

    // Unity's input field styling (now with darker backgrounds like Unity)
    colors[ImGuiCol_FrameBg] = ImColor(35, 35, 35, 255);        // #232323, very dark
    colors[ImGuiCol_FrameBgHovered] = ImColor(45, 45, 45, 255); // #2D2D2D, slightly lighter
    colors[ImGuiCol_FrameBgActive] = ImColor(55, 55, 55, 255);  // #373737, still darker than panel

    // Unity's title bar styling
    colors[ImGuiCol_TitleBg] = backgroundDarkest;
    colors[ImGuiCol_TitleBgActive] = backgroundDark;
    colors[ImGuiCol_TitleBgCollapsed] = backgroundDarkest;

    // Unity's tab styling - strong selection
    colors[ImGuiCol_Tab] = backgroundDark;
    colors[ImGuiCol_TabHovered] = primaryAccentMuted;
    colors[ImGuiCol_TabSelected] = primaryAccentStrong; // Very prominent selected tabs
    colors[ImGuiCol_TabDimmed] = backgroundDarkest;
    colors[ImGuiCol_TabDimmedSelected] = backgroundLight;

    // Unity's scrollbar styling
    colors[ImGuiCol_ScrollbarBg] = backgroundDark;
    colors[ImGuiCol_ScrollbarGrab] = backgroundLight;
    colors[ImGuiCol_ScrollbarGrabHovered] = borderMedium;
    colors[ImGuiCol_ScrollbarGrabActive] = primaryAccent;

    // Unity's interactive elements
    colors[ImGuiCol_CheckMark] = primaryAccent;
    colors[ImGuiCol_SliderGrab] = primaryAccent;
    colors[ImGuiCol_SliderGrabActive] = primaryAccentHover;

    // Unity's separator styling - very subtle
    colors[ImGuiCol_Separator] = borderDark;
    colors[ImGuiCol_SeparatorHovered] = borderMedium;
    colors[ImGuiCol_SeparatorActive] = primaryAccent;

    // Unity's resize grips - invisible until interaction
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered] = primaryAccentMuted;
    colors[ImGuiCol_ResizeGripActive] = primaryAccent;

    // Unity's table styling - inspector panels
    colors[ImGuiCol_TableHeaderBg] = backgroundLight;
    colors[ImGuiCol_TableBorderStrong] = borderDark;
    colors[ImGuiCol_TableBorderLight] = borderMedium;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.01f); // Extremely subtle alternating

    // Unity's docking styling
    colors[ImGuiCol_DockingPreview] = primaryAccentStrong;
    colors[ImGuiCol_DockingEmptyBg] = backgroundDarkest;

    // Unity's plotting colors
    colors[ImGuiCol_PlotLines] = primaryAccent;
    colors[ImGuiCol_PlotLinesHovered] = primaryAccentHover;
    colors[ImGuiCol_PlotHistogram] = primaryAccent;
    colors[ImGuiCol_PlotHistogramHovered] = primaryAccentHover;

    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.65f);

    // Unity's EXACT style parameters - completely square, tight spacing
    style.WindowRounding = 0.0f;    // Unity is completely square
    style.ChildRounding = 0.0f;     // No rounding anywhere
    style.FrameRounding = 2.0f;     // Tiny rounding on input fields only
    style.PopupRounding = 0.0f;     // Square popups
    style.ScrollbarRounding = 0.0f; // Square scrollbars
    style.GrabRounding = 0.0f;      // Square grab handles
    style.TabRounding = 0.0f;       // Square tabs

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 0.0f; // Unity has no child borders
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f; // Unity has no frame borders

    // Unity's tight, professional spacing
    style.WindowPadding = ImVec2(6.0f, 6.0f);    // Unity's window padding
    style.FramePadding = ImVec2(8.0f, 3.0f);     // Unity's frame padding
    style.CellPadding = ImVec2(4.0f, 2.0f);      // Unity's cell padding
    style.ItemSpacing = ImVec2(4.0f, 3.0f);      // Unity's item spacing
    style.ItemInnerSpacing = ImVec2(4.0f, 2.0f); // Unity's inner spacing
    style.IndentSpacing = 15.0f;                 // Unity's indent
    style.ScrollbarSize = 16.0f;                 // Unity's scrollbar width
    style.GrabMinSize = 10.0f;                   // Unity's minimum grab size

    style.WindowMenuButtonPosition = ImGuiDir_None;

    // Unity is always square in viewport mode
    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

} // namespace unravel