#pragma once

#include <editor/imgui/integration/imgui.h>

#include <cstdint>
#include <string>

/// The pieces the Packages window is drawn from: the package tile, cards, pills, counters and list rows.
namespace unravel::packages_widgets
{

// Sizes in units of the font size.
constexpr float CARD_ROUNDING = 0.5f;
constexpr float CARD_PADDING = 0.85f;
constexpr ImU32 CARD_COLOR = IM_COL32(0, 0, 0, 46);
constexpr float TITLE_SCALE = 1.3f;
constexpr float SECTION_GAP = 0.6f;

constexpr ImU32 COLOR_NEW = IM_COL32(110, 190, 130, 255);
constexpr ImU32 COLOR_UPDATE = IM_COL32(120, 170, 230, 255);
constexpr ImU32 COLOR_EDITED = IM_COL32(255, 190, 60, 255);
constexpr ImU32 COLOR_CLASH = IM_COL32(230, 150, 90, 255);
constexpr ImU32 COLOR_ERROR = IM_COL32(255, 95, 95, 255);
constexpr ImU32 COLOR_MUTED_PILL = IM_COL32(160, 160, 160, 255);

/// A size in font units, in pixels.
auto to_pixels(float font_units) -> float;

/// The color a package is drawn in: made from its name, so it stays the same everywhere.
auto get_package_color(const std::string& name) -> ImU32;

/// The package's tile: a rounded square in its color with the package glyph, `size` pixels wide.
void draw_package_tile(ImDrawList* draw_list, const ImVec2& min, float size, const std::string& name, const char* icon = nullptr);

/// A small rounded label in `color` on a soft fill of it; returns its size. Draws at `min`.
auto draw_pill(ImDrawList* draw_list, const ImVec2& min, const char* text, ImU32 color) -> ImVec2;
/// The size draw_pill takes for `text`.
auto calc_pill_size(const char* text) -> ImVec2;
/// A pill as an item in the layout, with a tooltip.
void pill_item(const char* text, ImU32 color, const char* tooltip = nullptr);

/// Opens a card: a rounded, darker child that grows with its content. Pair with end_card.
auto begin_card(const char* id) -> bool;
void end_card();

/// A counter: its value large, its label small and muted under it, `width` wide.
void draw_tile(const char* label, const std::string& value, ImU32 value_color, float width);
/// Lays `count` tiles across the row: the width each takes.
auto calc_tile_width(int count) -> float;

/// The title of a card, large and bold.
void draw_title(const char* text);
/// Muted text that wraps.
void draw_muted_wrapped(const char* text);

/// A list row's background: the stripe, the hover, or the selection with its accent edge.
void draw_row_background(const ImRect& rect, int row_index, bool is_hovered, bool is_selected);

/// A title and a line under it, centered in what is left of the window, for an empty list.
void draw_centered_hint(const char* title, const char* detail);

/// A path's folder part, muted, then its file name: the name stands out.
void draw_path_text(ImDrawList* draw_list, const ImVec2& pos, float max_x, const std::string& path, ImU32 name_color);

} // namespace unravel::packages_widgets
