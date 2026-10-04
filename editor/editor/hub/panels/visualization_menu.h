#pragma once
#include "visualization_modes.h"

#include <engine/rendering/pipeline/passes/lumen_visualize_pass.h>

#include <optional>
#include <vector>

struct ImVec2;

namespace unravel::visualization_menu
{

/// The tabs of the debug view popover.
enum class popover_tab : int
{
    views,
    overlays,
};

/// What code (an MCP tool) asks of the popover; served and cleared on the next frame.
enum class popover_request : int
{
    none,
    open,
    close,
};

/// Per-viewport UI state for the debug view picker. Not serialized: a debug view is a
/// session-scoped diagnostic, and reopening the editor should come up on the full render.
struct state
{
    /// Draw the active view's color legend over the viewport.
    bool show_legend{true};
    /// The popover's open tab.
    popover_tab tab{popover_tab::views};
    /// Opens or closes the popover from code on the next frame.
    popover_request request{popover_request::none};
    /// The category the Views tab lists (none = Lit). It follows the active view each time the popover opens.
    visualization_group views_group{visualization_group::none};
    /// The card generation overlay shows the clusters rather than the surfels.
    bool is_card_generation_cluster{false};
    /// The placement view the screen probes overlay comes on with (world_settings::screen_probe_placement).
    int screen_probe_placement{1};
    /// The world-space visualizations of the global illumination, drawn over whichever view is active.
    lumen_visualize_pass::world_settings lumen{};
    /// The cursor those visualizations use in place of the mouse's (an MCP hook for repeatable captures);
    /// negative for the view's centre.
    std::optional<math::vec2> lumen_cursor_override{};
};

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the debug view button of a floating viewport toolbar and its popover: a Views tab that
/// picks the view (Lit or a mode of a visualization_group, with its description and legend) and
/// an Overlays tab of switches for the world-space visualizations, drawn over whichever view is
/// active. The button names the active view and counts the overlays in a warning tint, so a
/// left-on debug pass cannot be mistaken for a rendering bug. Shared between the Scene and Game
/// panels; call between panel_toolbar::begin_bar() and end_bar().
/// </summary>
/// <param name="mode">Raw pipeline debug pass id, read and written in place</param>
/// <param name="menu_state">Persistent per-viewport state</param>
//-----------------------------------------------------------------------------
void draw_toolbar_dropdown(int& mode, state& menu_state);

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

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the text the active debug view puts on the image over the image drawn at
/// [image_min, image_max], in yellow with a drop shadow: the
/// overview tiles' names always, a single view's name only while the legend card - which sits in
/// the same corner and names the view too - is hidden.
/// </summary>
/// <param name="mode">Raw pipeline debug pass id</param>
/// <param name="menu_state">Persistent per-viewport state</param>
/// <param name="labels">The pipeline's labels, placed by fractions of the image</param>
/// <param name="image_min">Top-left corner of the drawn image, in screen coordinates</param>
/// <param name="image_max">Bottom-right corner of the drawn image, in screen coordinates</param>
//-----------------------------------------------------------------------------
void draw_view_labels(int mode,
                      const state& menu_state,
                      const std::vector<debug_view_label>& labels,
                      const ImVec2& image_min,
                      const ImVec2& image_max);

//-----------------------------------------------------------------------------
/// <summary>
/// The world settings a viewport's pipeline draws the overlays with: the popover's, with the
/// cursor - the render pixel under the mouse while the view's image, the last item, is hovered,
/// negative otherwise; or the state's cursor override. Call right after drawing the image.
/// </summary>
/// <param name="menu_state">Persistent per-viewport state</param>
/// <param name="image_min">Top-left corner of the drawn image, in screen coordinates</param>
/// <param name="image_max">Bottom-right corner of the drawn image, in screen coordinates</param>
/// <param name="render_size">The size the view renders at</param>
//-----------------------------------------------------------------------------
auto make_lumen_visualize(const state& menu_state,
                          const ImVec2& image_min,
                          const ImVec2& image_max,
                          const usize32_t& render_size) -> lumen_visualize_pass::world_settings;

} // namespace unravel::visualization_menu
