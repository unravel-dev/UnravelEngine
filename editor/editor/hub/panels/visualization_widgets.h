#pragma once

/// The controls of the debug view popover (visualization_menu): switches, list rows, section headings, option labels
/// and the tab bar, drawn in the look of the editor's settings windows. Sizes follow the UI scale.
namespace unravel::visualization_widgets
{

//-----------------------------------------------------------------------------
/// <summary>
/// An on / off switch: a pill with a knob, filled with the theme accent while on, centered on the height of a frame.
/// Returns true on the click that flipped @p value.
/// </summary>
//-----------------------------------------------------------------------------
auto draw_switch(const char* id, bool& value) -> bool;

//-----------------------------------------------------------------------------
/// <summary>
/// A row across the content region that flips @p value: its label at the left and a switch at the right end; a click
/// anywhere on the row flips it. Returns true when @p value changed.
/// </summary>
/// <param name="tooltip">Shown while the row is hovered; nullptr for none</param>
//-----------------------------------------------------------------------------
auto draw_switch_row(const char* label, const char* tooltip, bool& value) -> bool;

//-----------------------------------------------------------------------------
/// <summary>
/// A row of a list across the content region: an optional icon and the label, a fill while hovered and the accent
/// fill while selected. Returns true on the click.
/// </summary>
/// <param name="icon">ICON_MDI_* literal, or nullptr for a row without one</param>
/// <param name="has_marker">Draws an accent dot at the right end: what the list picks lies under this row</param>
//-----------------------------------------------------------------------------
auto draw_list_row(const char* id, const char* icon, const char* label, bool is_selected, bool has_marker) -> bool;

/// A short muted heading over a group of rows.
void draw_section_header(const char* label);

//-----------------------------------------------------------------------------
/// <summary>
/// The label of an option under a switch row, indented and muted, followed on the same line by the control the caller
/// draws next, which gets the rest of the row as its width.
/// </summary>
/// <param name="tooltip">Shown while the label is hovered; nullptr for none</param>
//-----------------------------------------------------------------------------
void draw_option_label(const char* label, const char* tooltip);

//-----------------------------------------------------------------------------
/// <summary>
/// A segmented control @p width wide: one segment per label, the selected one filled with the accent. Returns true
/// when @p index changed.
/// </summary>
/// <param name="badges">Per segment, a count drawn in a pill after its label when above zero; nullptr for none</param>
//-----------------------------------------------------------------------------
auto draw_segmented(const char* id, const char* const* labels, const int* badges, int count, float width, int& index)
    -> bool;

/// The field colours of the popover's sliders and combos: a light wash, so that they stand out on the dark popover as
/// the settings windows' search field does. Pair every push with a pop.
void push_field_style();
void pop_field_style();

} // namespace unravel::visualization_widgets
