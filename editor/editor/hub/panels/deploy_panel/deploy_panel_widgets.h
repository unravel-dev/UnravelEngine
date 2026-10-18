#pragma once

#include <editor/imgui/integration/imgui.h>

namespace unravel
{
namespace deploy_widgets
{

// Sizes are in units of the font size, like the start page and the About window.
constexpr float SECTION_GAP = 1.0f;
constexpr float LINE_GAP = 0.35f;
constexpr float ICON_GAP = 0.45f;
constexpr float BUTTON_PADDING_X = 1.0f;
constexpr float BUTTON_ROUNDING = 0.4f;
constexpr ImU32 WARNING_COLOR = IM_COL32(255, 190, 60, 255);

/// A size in font units, in pixels.
auto to_pixels(float font_units) -> float;
auto get_accent_u32() -> ImU32;
auto get_muted_color() -> ImU32;

void draw_muted_text(const char* text);
void draw_gap(float font_units);
/// An icon and a line of text, the icon centered on the line; long text wraps under itself, at
/// `wrap_pos_x` (window coordinates; 0: the window's edge).
void draw_icon_line(const char* icon, ImU32 icon_color, const char* text, ImU32 text_color, float wrap_pos_x = 0.0f);
void draw_section_title(const char* title);
/// A section title with a button at the right end of its line, for what acts on the section.
auto draw_section_title_button(const char* title, const char* label, const char* tooltip, bool is_enabled) -> bool;
auto calc_button_width(const char* label) -> float;
/// A button of the window's size: the primary one takes the accent color.
auto draw_button(const char* label, bool is_primary, bool is_enabled) -> bool;

/// A progress bar across the window with `left_text` at its left end and `right_text` at its right
/// end; ImGui's own text would move with the bar's fill.
void draw_progress_bar(float fraction, const char* left_text, const char* right_text);

/// The theme's fields are darker than a window and all but vanish on the lighter popup, so the
/// window washes them light, as the start page does. Pair with pop_field_colors.
void push_field_colors();
void pop_field_colors();

} // namespace deploy_widgets
} // namespace unravel
