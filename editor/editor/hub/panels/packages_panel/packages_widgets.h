#pragma once

#include <editor/imgui/integration/imgui.h>

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

/// The pieces the Packages window is drawn from: the package tile, cards, pills, counters and list rows.
namespace unravel::packages_widgets
{

// Sizes in units of the font size.
constexpr float CARD_ROUNDING = 0.5f;
constexpr float CARD_PADDING = 0.85f;
constexpr ImU32 CARD_COLOR = IM_COL32(0, 0, 0, 46);
constexpr float TITLE_SCALE = 1.3f;
constexpr float SECTION_GAP = 0.6f;
constexpr float HEADER_TILE_SIZE = 3.4f;
constexpr float HEADER_TILE_GAP = 0.85f;
constexpr float ROW_PADDING_X = 0.45f;
constexpr float LIST_MIN_HEIGHT = 8.0f;
constexpr float README_MAX_HEIGHT = 14.0f;
constexpr float SEARCH_WIDTH = 12.0f;
constexpr float SEARCH_MIN_WIDTH = 5.0f;

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

/// What a filter tile shows and says.
struct filter_tile
{
    const char* label{};
    std::size_t count{};
    ImU32 color{};
    const char* tooltip{};
    bool is_active{};
};

/**
 * @brief A counter that filters a list: a click shows only what it counts, and a click on the active one shows
 * everything again. A counter at zero is dimmed and takes no click unless it is the active one.
 * @return True when the tile was clicked.
 */
auto draw_filter_tile(const char* id, const filter_tile& tile, float width) -> bool;

/// A dropdown drawn as plain text with a caret until hovered, so a list of them stays quiet. Call ImGui::EndCombo
/// when it returns true.
auto begin_row_choice(const char* id, const char* preview, float width) -> bool;
/// A choice of a row's dropdown: its name, and under it what it does, muted. @return True when it was picked.
auto draw_choice(const char* name, const char* description, bool is_selected) -> bool;

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

/// A file of a list grouped by folder.
struct file_list_row
{
    /// Its path below the data folder, '/' separated ("UIKit/Materials/Cell.mat").
    std::string path;
    /// Empty when the size is not known.
    hpp::optional<std::uint64_t> size;
    /// The pill in the label column, such as the file's status; null for none.
    const char* label{};
    ImU32 label_color{COLOR_MUTED_PILL};
    /// Left out or skipped: its name is drawn muted.
    bool is_muted{};
};

/// The parts of a file row a list draws itself. Each is given the row's index in the rows passed to draw_file_list.
struct file_list_parts
{
    /// Width of the part before the type icon, such as a check box, in font units; 0 for none.
    float leading_width{};
    /// Width of the label column, in font units; 0 for none.
    float label_width{};
    /// Width of the part at the row's right end, such as a dropdown, in font units; 0 for none.
    float trailing_width{};
    /// Draws the leading part with the cursor at its place.
    std::function<void(std::size_t)> draw_leading;
    /// Draws the trailing part with the cursor at its place.
    std::function<void(std::size_t)> draw_trailing;
    /// The tooltip of the row's name; without it the tooltip shows the file's path.
    std::function<void(std::size_t)> draw_tooltip;
};

/// What a file list keeps between frames: the folders the user closed.
class file_list_state
{
public:
    auto is_collapsed(hpp::string_view folder) const -> bool;
    void toggle(hpp::string_view folder);
    /// Whether every folder the list showed last is closed.
    auto is_all_collapsed() const -> bool;
    /// Closes every folder the list showed last, or opens them all again when they are all closed.
    void toggle_all();
    /// The folders the list shows this frame, in their order.
    void set_folders(std::vector<std::string> folders);
    void clear();

private:
    std::set<std::string, std::less<>> collapsed_;
    std::vector<std::string> folders_;
};

/// A toolbar strip button that closes every folder of the list, or opens them all again.
void draw_collapse_button(const char* id, file_list_state& state);

/**
 * @brief The files under a header per folder, each folder closable, with a type icon, the name, the size and a
 * label for each file, and the parts the list draws itself. Only the rows in view are drawn.
 * @param max_height The most height the list takes; it takes less when its rows need less.
 * @param empty_hint The line shown when there is no row.
 */
void draw_file_list(const char* id,
                    const std::vector<file_list_row>& rows,
                    file_list_state& state,
                    const file_list_parts& parts,
                    float max_height,
                    const char* empty_hint);

} // namespace unravel::packages_widgets
