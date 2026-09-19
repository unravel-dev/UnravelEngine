#pragma once
#include <editor/imgui/integration/imgui.h>
#include <engine/rendering/pipeline/pipeline.h>

namespace unravel::viewport_toolbar
{
struct bar_placement;
} // namespace unravel::viewport_toolbar

namespace unravel::viewport_stats_overlay
{

struct state
{
    bool is_visible = false;
    bool open_profiler_requested = false;
};

//-----------------------------------------------------------------------------
/// <summary>
/// Draw a statistics overlay child window at the top-right corner of the
/// current ImGui window. Contains multiple collapsible sections showing
/// rendering statistics. Shared between scene and game panels.
/// </summary>
/// <param name="overlay_state">Persistent state for visibility toggle</param>
/// <param name="id">Unique identifier to disambiguate multiple overlays</param>
/// <param name="top_offset">Height kept free at the top of the window, for a host whose
/// floating toolbar shares that edge</param>
//-----------------------------------------------------------------------------
void draw(const rendering::pipeline_stats& pstats, state& overlay_state, const char* id, float top_offset = 0.0f);

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the statistics toggle of a floating viewport toolbar, with the frame rate as its label;
/// call between viewport_toolbar::begin_bar() and end_bar(). Toggles the overlay visibility on
/// click.
/// The frame rate is what the button is read for, so it stays in a compact layout too.
/// </summary>
/// <param name="overlay_state">Persistent state to toggle visibility on</param>
//-----------------------------------------------------------------------------
void draw_toolbar_toggle(state& overlay_state);

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the frame rate alone, as a passive viewport_toolbar::draw_readout: for a host that takes
/// its toolbar away (the game panel while the game plays) but keeps the frame rate on screen. A
/// right anchored placement shows it exactly where draw_toolbar_toggle does as the last item.
/// </summary>
//-----------------------------------------------------------------------------
void draw_toolbar_readout(const viewport_toolbar::bar_placement& placement);

} // namespace unravel::viewport_stats_overlay
