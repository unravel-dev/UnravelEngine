#pragma once

#include <imgui/imgui_internal.h>

/// The folding section header the panels share: a divider, a chevron, the icon of the section in
/// the accent and its title. Used by the statistics overlay and by the profiler.
namespace unravel::panel_section
{

struct header
{
    bool is_open{};
    /// The row the header takes, for a control the caller draws at its right end.
    ImRect row{};
};

//-----------------------------------------------------------------------------
/// <summary>
/// Draws the header and returns whether the section is open. The open state is kept in the
/// current window under the id, so the caller holds no state of its own.
/// </summary>
/// <param name="trailing_width">Width left free of the fold button at the right end of the row,
/// for a control the caller draws there</param>
//-----------------------------------------------------------------------------
auto draw_header(const char* id,
                 const char* icon,
                 const char* title,
                 bool is_default_open,
                 float trailing_width = 0.0f) -> header;

/// Where the rows of a section start: under the icon of its header.
auto get_row_indent() -> float;

} // namespace unravel::panel_section
