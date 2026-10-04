#pragma once

namespace unravel::visualization_menu
{
struct state;
} // namespace unravel::visualization_menu

/// The overlays of the debug view popover (visualization_menu): the world-space visualizations of the global
/// illumination, drawn over whichever view is active, each a switch row with its options under it while it is on.
namespace unravel::visualization_overlays
{

/// How many overlays of @p menu_state are on.
auto count_active(const visualization_menu::state& menu_state) -> int;

/// Turns every overlay off. Their options keep their values.
void turn_off(visualization_menu::state& menu_state);

/// The popover's Overlays tab: the overlays by section.
void draw_tab(visualization_menu::state& menu_state);

} // namespace unravel::visualization_overlays
