#pragma once

#include <imgui/imgui_internal.h>

#include <cstddef>

/// What the inspectors of arrays and maps share: the controls of the header row, the flat icon
/// buttons of the rows, and the hint of an empty container.
namespace unravel::container_widgets
{

/// Converts a size given in units of the font size to pixels, so the widgets follow the UI scale.
auto to_pixels(float font_units) -> float;

/// A flat icon button: the icon alone, with a fill under the pointer.
struct icon_button
{
    const char* id{};
    const char* icon{};
    const char* tooltip{};
    /// Turns red under the pointer, for a button that deletes.
    bool is_destructive{};
    /// Color of the icon, 0 for the text color.
    ImU32 color{};
};

/// Draws the button over the rectangle; it takes no room of its own. Returns true when clicked.
auto draw_icon_button(const icon_button& button, const ImRect& bb) -> bool;

/// What the header row of a container shows at its right end.
struct header_controls
{
    std::size_t size{};
    /// The count can be typed in; otherwise it is only shown.
    bool is_count_editable{};
    /// An add button follows the count.
    bool can_add{};
    const char* count_tooltip{};
    const char* add_tooltip{};
};

/// What the header controls were asked for this frame.
struct header_request
{
    bool is_add_pressed{};
    /// A typed count was committed, with Enter or by leaving the field.
    bool is_count_entered{};
    int entered_count{};
};

//-----------------------------------------------------------------------------
/// <summary>
/// The count and the add button, right aligned in the value cell of the header row. A typed count
/// is only handed back when it is committed, not on every key: typing 20 over 25 would otherwise
/// cut an array down to 2 on the way.
/// </summary>
//-----------------------------------------------------------------------------
auto draw_header_controls(const header_controls& controls) -> header_request;

/// Width the remove button takes at the right end of a row's label cell. The label leaves it free.
auto get_row_button_width() -> float;

//-----------------------------------------------------------------------------
/// <summary>
/// A remove button at the right end of a row's label cell, shown while the pointer is over the
/// row and no drag is going on. Call after the row, so that it lies over it.
/// </summary>
/// <returns>True when clicked</returns>
//-----------------------------------------------------------------------------
auto draw_row_remove_button(const ImRect& label_rect, const ImRect& row_rect, const char* tooltip) -> bool;

/// The single row of an open container that has nothing in it.
void draw_empty_hint();

} // namespace unravel::container_widgets
