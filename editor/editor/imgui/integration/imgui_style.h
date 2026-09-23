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

//-----------------------------------------------------------------------------
/// <summary>
/// The text color at the alpha the panels mute their secondary text with: labels beside a value,
/// paths, hints. One definition, so every panel mutes by the same amount.
/// </summary>
//-----------------------------------------------------------------------------
auto get_muted_text_color() -> ImVec4;
auto get_muted_text_color_u32() -> ImU32;

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
