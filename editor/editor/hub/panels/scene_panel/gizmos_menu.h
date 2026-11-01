#pragma once

#include <context/context.hpp>

#include <hpp/optional.hpp>

#include <cstddef>

struct ImVec2;

namespace unravel
{
struct editing_manager;
class camera;
} // namespace unravel

namespace unravel::gizmos_menu
{

/// Per-viewport UI state of the popup. Not serialized.
struct state
{
    /// The row under the pointer: draw_find_markers takes it and marks its objects in the view.
    hpp::optional<size_t> hovered_row;
};

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the gizmos split button of the Scene panel's toolbar: the master switch and the caret of its
/// popup. The popup is one table of the object types against Icons (every object in the scene) and
/// Gizmos (the selected objects), with the details of a type unfolding under its row. The caret takes
/// a warning tint while any setting differs from the defaults, so a left-on setting cannot be mistaken
/// for a bug. What the user changes here becomes the project's setting. Call between
/// panel_toolbar::begin_bar() and end_bar().
/// </summary>
/// <param name="popup_max_height">Tallest the popup may get before it scrolls</param>
//-----------------------------------------------------------------------------
void draw_toolbar_controls(rtti::context& ctx, editing_manager& em, state& menu_state, float popup_max_height);

//-----------------------------------------------------------------------------
/// <summary>
/// Draw a ring around every object of the row under the pointer, into the current window, so that a
/// row can be traced to what it controls. Call inside the viewport's window.
/// </summary>
/// <param name="cam">The camera of the view the markers go on</param>
/// <param name="image_min">Top left of the view's image on screen</param>
/// <param name="image_max">Bottom right of the view's image on screen</param>
//-----------------------------------------------------------------------------
void draw_find_markers(rtti::context& ctx,
                       state& menu_state,
                       const camera& cam,
                       const ImVec2& image_min,
                       const ImVec2& image_max);

} // namespace unravel::gizmos_menu
