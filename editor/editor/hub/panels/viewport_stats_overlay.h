#pragma once
#include <editor/imgui/integration/imgui.h>
#include <engine/rendering/pipeline/pipeline.h>

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
//-----------------------------------------------------------------------------
void draw(const rendering::pipeline_stats& pstats, state& overlay_state, const char* id);

//-----------------------------------------------------------------------------
/// <summary>
/// Draw a right-aligned "Stats" toggle button for the menu bar.
/// Toggles the overlay visibility on click.
/// </summary>
/// <param name="overlay_state">Persistent state to toggle visibility on</param>
//-----------------------------------------------------------------------------
void draw_stats_toggle(state& overlay_state);

} // namespace unravel::viewport_stats_overlay
