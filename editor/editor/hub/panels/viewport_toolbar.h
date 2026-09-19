#pragma once
#include <editor/imgui/integration/imgui.h>

#include <imgui/imgui_internal.h>

#include <string>

/// Floating toolbars for a viewport panel: rounded, translucent bars that sit over the rendered
/// image instead of a menu bar above it. Every bar is its own child window, so a click on it never
/// reaches the picking, the drag selection or the manipulation gizmo underneath, while the gaps
/// between bars stay part of the viewport.
namespace unravel::viewport_toolbar
{

enum class bar_anchor
{
    left,
    center,
    right
};

struct bar_placement
{
    /// Screen-space rectangle of the viewport the bar floats over.
    ImRect area{};
    bar_anchor anchor{bar_anchor::left};
    /// Rows stack downwards from the top edge of the area.
    int row{0};
    /// Opacity of the whole bar, for a host that fades its bars in and out.
    float alpha{1.0f};
};

/// How much of a toolbar fits its viewport. The widths are what the bars of the host take side
/// by side, measured while that layout was shown; 0 until then, which counts as fitting.
struct layout_state
{
    float full_width{};
    float compact_width{};
    /// Compact drops the text labels.
    bool is_compact{};
    /// Compact is still too wide: a host with two bars moves one of them to a second row.
    bool is_stacked{};
};

//-----------------------------------------------------------------------------
/// <summary>
/// Pick the layout for this frame.
/// </summary>
/// <param name="bars_width">Summed get_bar_width() of the bars that share the first row</param>
/// <param name="bar_count">How many bars that is, for the margins between them</param>
//-----------------------------------------------------------------------------
void update_layout(layout_state& state, float bars_width, int bar_count, float area_width);

/// Icon plus label, or the icon alone in a compact layout.
auto make_text(const char* icon, const char* name, bool is_compact) -> std::string;

//-----------------------------------------------------------------------------
/// <summary>
/// Distance from the top edge of the viewport area to the bottom of the given number of bar
/// rows, margins included. Overlays that share the top edge start below it.
/// </summary>
//-----------------------------------------------------------------------------
auto get_rows_extent(int row_count) -> float;

/// Distance a bar keeps to the edges of the viewport area, and at least to the next bar.
auto get_bar_margin() -> float;

//-----------------------------------------------------------------------------
/// <summary>
/// Begin a bar. Items follow on one line; always pair with end_bar(), whatever is returned.
/// Centered and right anchored bars are placed by the width they measured the frame before, and
/// stay invisible for the one frame they have no measurement.
/// </summary>
//-----------------------------------------------------------------------------
auto begin_bar(const char* id, const bar_placement& placement) -> bool;
void end_bar();

//-----------------------------------------------------------------------------
/// <summary>
/// Width of the bar in the frame before, 0 until it was drawn once. Call from the window that
/// hosts the bar.
/// </summary>
//-----------------------------------------------------------------------------
auto get_bar_width(const char* id) -> float;

/// True while a dropdown of a bar in the current window is open. Call from the window that
/// hosts the bars.
auto is_dropdown_open() -> bool;

//-----------------------------------------------------------------------------
/// <summary>
/// Draw a bar that only shows a text. It has no window and no item, so every click goes through
/// to the viewport. It is sized and placed like a bar whose single item is that text, which puts
/// the text exactly where the last item of a right anchored bar shows it - a host can fade from
/// one to the other. Call from the window that hosts the bars.
/// </summary>
/// <param name="width_text">Optional text the width is measured from instead</param>
//-----------------------------------------------------------------------------
void draw_readout(const bar_placement& placement, const char* text, const char* width_text = nullptr);

/// Thin vertical divider between groups of items.
void separator();

/// Plain text, vertically centered on the bar.
void label(const char* text);

//-----------------------------------------------------------------------------
/// <summary>
/// Momentary button. is_accented fills it with the theme accent, for the one action a bar is
/// about.
/// </summary>
/// <param name="width_text">Optional text the width is measured from instead, for a button whose
/// label changes with the state it flips</param>
//-----------------------------------------------------------------------------
auto button(const char* id,
            const char* text,
            const char* tooltip,
            bool is_accented = false,
            const char* width_text = nullptr) -> bool;

//-----------------------------------------------------------------------------
/// <summary>
/// Button that shows an on / off state. Returns true on the click; the caller owns the state.
/// </summary>
/// <param name="width_text">Optional text the width is measured from instead. A readout that
/// changes every frame would otherwise resize its bar, and an anchored bar would jitter.</param>
//-----------------------------------------------------------------------------
auto toggle(const char* id, const char* text, bool is_active, const char* tooltip, const char* width_text = nullptr)
    -> bool;

//-----------------------------------------------------------------------------
/// <summary>
/// Button that opens a popup under the bar. A null text draws the caret alone, attached to the
/// item before it - the settings half of a split button. While one dropdown is open, hovering
/// another one switches to it, like a menu bar. Returns true while the popup is open: submit
/// its content, then call end_dropdown(). A SetNextWindowSize / SetNextWindowSizeConstraints
/// right before the call applies to the popup.
/// </summary>
/// <param name="text_color">Optional tint of the text, 0 for the default</param>
//-----------------------------------------------------------------------------
auto begin_dropdown(const char* id, const char* text, const char* tooltip, ImU32 text_color = 0) -> bool;
void end_dropdown();

} // namespace unravel::viewport_toolbar
