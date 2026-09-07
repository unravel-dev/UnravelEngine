#pragma once
#include "visualization_modes.h"

namespace unravel::visualization_menu
{

/// Per-viewport UI state for the debug view picker. Not serialized: a debug view is a
/// session-scoped diagnostic, and reopening the editor should come up on the full render.
struct state
{
    /// Draw the active view's color legend over the viewport.
    bool show_legend{true};
};

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the menu-bar debug view picker: a single menu holding "Full" plus one submenu per
/// visualization_group. Every entry carries a tooltip with its description and legend, and
/// the menu-bar label names the active view so a left-on debug pass cannot be mistaken for
/// a rendering bug. Shared between the Scene and Game panels.
/// </summary>
/// <param name="mode">Raw pipeline debug pass id, read and written in place</param>
/// <param name="menu_state">Persistent per-viewport state</param>
//-----------------------------------------------------------------------------
void draw_menu(int& mode, state& menu_state);

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the active view's legend card in the bottom-left of the current window's content
/// rect. No-op for visualization_mode::full and while the card is hidden.
/// </summary>
/// <param name="mode">Raw pipeline debug pass id</param>
/// <param name="menu_state">Persistent per-viewport state; the close button clears it</param>
/// <param name="id">Unique identifier to disambiguate multiple viewports</param>
//-----------------------------------------------------------------------------
void draw_legend_overlay(int mode, state& menu_state, const char* id);

} // namespace unravel::visualization_menu
