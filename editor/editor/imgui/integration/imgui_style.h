#pragma once

#include <imgui/imgui.h>

namespace imgui_style
{

//-----------------------------------------------------------------------------
/// <summary>
/// The accent of the active theme: the one color that marks what is on, selected or focused.
/// Widgets that draw themselves (panel_toolbar, log rows, component icons) take it from here
/// rather than from whichever ImGuiCol happens to carry it in one theme.
/// </summary>
//-----------------------------------------------------------------------------
auto get_accent_color() -> ImVec4;

void set_unity_theme();
void set_unity_inspired_theme();
void set_modern_purple_theme();
void set_warm_amber_theme();
void set_cool_blue_theme();
void set_minimalist_green_theme();
void set_professional_dark_theme();
void set_dark_theme();
void set_dark_theme_red();
void set_photoshop_theme();

} // namespace imgui_style
