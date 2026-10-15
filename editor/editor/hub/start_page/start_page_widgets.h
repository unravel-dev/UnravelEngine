#pragma once

#include <editor/imgui/integration/imgui.h>
#include <imgui/imgui_internal.h>

/**
 * @brief The controls and pieces of the start page: buttons of a few kinds, fields that show on
 * the dark page, pills, the frame of a card, titles and empty states. Sizes are in units of the
 * font size (see screen_card), so the page follows the UI scale.
 */
namespace unravel::start_page_widgets
{
enum class button_kind
{
    /// The one action a page is about.
    primary,
    secondary,
    /// No fill until hovered: actions beside the main ones.
    ghost,
    /// The action that destroys something.
    danger
};

auto get_button_height() -> float;
/// The width a button needs for its label.
auto calc_button_width(const char* label) -> float;
/// A button of the page; width 0 fits the label.
auto draw_button(const char* label, button_kind kind, float width = 0.0f, bool is_enabled = true) -> bool;
/// A square button with an icon; is_on keeps it lit.
auto draw_icon_toggle(const char* id, const char* icon, bool is_on, const char* tooltip) -> bool;

/// The theme draws fields darker than a window, which makes them vanish on the dark page: these
/// wash them light.
void push_field_style();
void pop_field_style();

/// The title of a page over its subtitle, from the cursor; wrap_width 0 wraps the subtitle at the
/// edge of the window.
void draw_page_title(const char* title, const char* subtitle, float wrap_width = 0.0f);
/// Dimmed, wrapped text; wrap_width 0 wraps at the edge of the window.
void draw_muted_text(const char* text, float wrap_width = 0.0f);
/// An icon, a headline and a line of text centered in the room left in the window.
void draw_empty_state(const char* icon, const char* headline, const char* text);

auto get_accent_color(float alpha = 1.0f) -> ImU32;
auto get_text_color(float alpha) -> ImU32;
auto get_muted_color() -> ImU32;

/// The size of a pill holding the text.
auto calc_pill_size(const char* text) -> ImVec2;
/// A small rounded label: text on a tinted fill.
void draw_pill(ImDrawList* draw_list, const ImVec2& pos, const char* text, ImU32 fill_color, ImU32 text_color);

/// The background and border of a card: lit when hovered, in the accent when selected.
void draw_card_frame(ImDrawList* draw_list, const ImRect& rect, float rounding, bool is_hovered, bool is_selected);

/// Text from pos that ends in an ellipsis when it would run past max_x.
void draw_text_clipped(ImDrawList* draw_list, const ImVec2& pos, float max_x, ImU32 color, const char* text);
/// Text wrapped at max_x into at most max_lines lines, the last one ending in an ellipsis when
/// the text goes on.
void draw_text_wrapped(ImDrawList* draw_list, const ImVec2& pos, float max_x, ImU32 color, const char* text, int max_lines);
} // namespace unravel::start_page_widgets
