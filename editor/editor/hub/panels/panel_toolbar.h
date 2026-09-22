#pragma once
#include <editor/imgui/integration/imgui.h>

#include <imgui/imgui_internal.h>

#include <string>

/// Toolbars of the editor panels, in one look. Two containers hold the same items:
/// - a bar floats over a viewport (scene, game) instead of a menu bar above it. Every bar is its
///   own child window, so a click on it never reaches the picking, the drag selection or the
///   manipulation gizmo underneath, while the gaps between bars stay part of the viewport;
/// - a strip is docked at the top of a panel (content, console, inspector) or of the editor
///   itself (header), as wide as its host.
namespace unravel::panel_toolbar
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

enum class strip_style
{
    /// A rounded card a shade darker than the panel it sits in.
    card,
    /// No background of its own: for a host that already is a band of chrome (the header).
    flat
};

//-----------------------------------------------------------------------------
/// <summary>
/// Begin a strip: the toolbar docked at the cursor of the current window, across its width. Items
/// follow on one line; always pair with end_strip(), whatever is returned.
/// </summary>
//-----------------------------------------------------------------------------
auto begin_strip(const char* id, strip_style style = strip_style::card) -> bool;
void end_strip();

/// Height a strip takes, for a host that has to reserve it.
auto get_strip_height() -> float;

//-----------------------------------------------------------------------------
/// <summary>
/// Send the items that follow to the middle / to the right end of the strip. A group is placed
/// by the width it measured the frame before, so keep that width steady (see width_text). Each
/// once per strip, the center before the right.
/// </summary>
//-----------------------------------------------------------------------------
void align_center();
void align_right();

//-----------------------------------------------------------------------------
/// <summary>
/// Make the items until end_group() one item for ImGui, so that one tooltip or one
/// ImGui::BeginDisabled() can cover them all.
/// </summary>
//-----------------------------------------------------------------------------
void begin_group();
void end_group();

//-----------------------------------------------------------------------------
/// <summary>
/// Make room on the line for one framed ImGui widget (input, slider) and give it the height and
/// the rounding of the buttons. Submit the widget, then call end_field().
/// </summary>
//-----------------------------------------------------------------------------
void begin_field(float width);
void end_field();

//-----------------------------------------------------------------------------
/// <summary>
/// Width for a field that gives way when the strip gets narrow: max_width while there is room,
/// never under min_width. Before align_right() the room is what the right aligned group (as
/// measured the frame before) leaves; after it, what the left items leave - the field is taken
/// to be the whole right group then.
/// </summary>
//-----------------------------------------------------------------------------
auto calc_flexible_width(float min_width, float max_width) -> float;

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

//-----------------------------------------------------------------------------
/// <summary>
/// Begin a card in the look of the bars that floats over a viewport, for an overlay such as the
/// statistics: width wide with its top right corner at top_right, as tall as its content up to
/// max_height, past which it scrolls. Always pair with end_overlay(), whatever is returned.
/// </summary>
//-----------------------------------------------------------------------------
auto begin_overlay(const char* id, const ImVec2& top_right, float width, float max_height) -> bool;
void end_overlay();

/// Thin vertical divider between groups of items.
void separator();

/// Dim chevron between the steps of a path, for a breadcrumb made of buttons.
void path_separator();

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
/// <param name="active_color">Fill while on, 0 for the theme accent. For a state with a color
/// of its own (playing, paused).</param>
//-----------------------------------------------------------------------------
auto toggle(const char* id,
            const char* text,
            bool is_active,
            const char* tooltip,
            const char* width_text = nullptr,
            ImU32 active_color = 0) -> bool;

//-----------------------------------------------------------------------------
/// <summary>
/// Toggle of a filter: on shows the text in its own color on a soft fill, off dims it. For a row
/// of filters, where the accent fill of toggle() on most of them would shout.
/// </summary>
//-----------------------------------------------------------------------------
auto filter_toggle(const char* id,
                   const char* text,
                   bool is_active,
                   ImU32 color,
                   const char* tooltip,
                   const char* width_text = nullptr) -> bool;

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

} // namespace unravel::panel_toolbar
