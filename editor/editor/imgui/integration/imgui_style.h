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

//-----------------------------------------------------------------------------
/// <summary>
/// The themes of the editor, in the order the Style window lists them: the neutral ones, the
/// cool ones, the warm ones.
/// </summary>
//-----------------------------------------------------------------------------
enum class theme
{
    unravel_dark,
    graphite,
    charcoal,
    slate,
    midnight,
    amethyst,
    sage,
    amber,
    crimson,
    count
};

//-----------------------------------------------------------------------------
/// <summary>
/// The name the Style window shows for a theme.
/// </summary>
//-----------------------------------------------------------------------------
auto get_theme_name(theme value) -> const char*;

//-----------------------------------------------------------------------------
/// <summary>
/// Replaces every ImGui color and size a theme defines, and takes the accent from it. The
/// result does not depend on the theme that was set before.
/// </summary>
//-----------------------------------------------------------------------------
void set_theme(theme value);

} // namespace imgui_style
