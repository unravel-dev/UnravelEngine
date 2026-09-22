#pragma once
#include <context/context.hpp>
#include <editor/imgui/integration/imgui.h>
#include <engine/rendering/pipeline/pipeline.h>

#include <array>
#include <cstddef>

namespace unravel::panel_toolbar
{
struct bar_placement;
} // namespace unravel::panel_toolbar

namespace unravel::viewport_stats_overlay
{

/// Frames the frame time graph spans.
constexpr std::size_t FRAME_HISTORY_SIZE = 120;

struct state
{
    bool is_visible = false;
    bool open_profiler_requested = false;
    /// Frame times in milliseconds, a ring written at frame_cursor.
    std::array<float, FRAME_HISTORY_SIZE> frame_times_ms{};
    std::size_t frame_cursor{};
    std::size_t frame_count{};
    /// ImGui frame of the last sample: a gap means the overlay was hidden, and the history restarts.
    int last_sample_frame{-1};
};

//-----------------------------------------------------------------------------
/// <summary>
/// Draw a statistics overlay child window at the top-right corner of the
/// current ImGui window: the frame rate with a frame time graph, then collapsible sections of
/// rendering statistics, and the switch of the project's static mesh batching. Shared between
/// scene and game panels.
/// </summary>
/// <param name="overlay_state">Persistent state for visibility toggle and the frame history</param>
/// <param name="id">Unique identifier to disambiguate multiple overlays</param>
/// <param name="top_offset">Height kept free at the top of the window, for a host whose
/// floating toolbar shares that edge</param>
//-----------------------------------------------------------------------------
void draw(rtti::context& ctx,
          const rendering::pipeline_stats& pstats,
          state& overlay_state,
          const char* id,
          float top_offset = 0.0f);

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the statistics toggle of a floating viewport toolbar, with the frame rate as its label;
/// call between panel_toolbar::begin_bar() and end_bar(). Toggles the overlay visibility on
/// click.
/// The frame rate is what the button is read for, so it stays in a compact layout too.
/// </summary>
/// <param name="overlay_state">Persistent state to toggle visibility on</param>
//-----------------------------------------------------------------------------
void draw_toolbar_toggle(state& overlay_state);

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the frame rate alone, as a passive panel_toolbar::draw_readout: for a host that takes
/// its toolbar away (the game panel while the game plays) but keeps the frame rate on screen. A
/// right anchored placement shows it exactly where draw_toolbar_toggle does as the last item.
/// </summary>
//-----------------------------------------------------------------------------
void draw_toolbar_readout(const panel_toolbar::bar_placement& placement);

} // namespace unravel::viewport_stats_overlay
