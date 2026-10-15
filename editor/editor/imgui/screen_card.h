#pragma once

#include "integration/imgui.h"

/**
 * @brief The card the editor shows on an empty backdrop while there is no docked UI: the loading
 * screen and the start page. One module, so the two are the same surface and the editor does
 * not change its face between them.
 *
 * Sizes are in units of the font size, so a card follows the UI scale and never depends on the
 * paddings of a theme.
 */
namespace unravel::screen_card
{
struct card_layout
{
    ImVec2 size{};
    /// Height the card is centered by, 0 for its own. Given the height a growing card may reach,
    /// the card keeps its top edge while it grows.
    float centered_height{};
    /// False for content that lays out its own regions from the edges of the card.
    bool has_padding{true};
};

auto to_pixels(float font_units) -> float;
/// Padding between the edge of a card and its content.
auto get_padding() -> ImVec2;
/// Radius of the corners of a card.
auto get_rounding() -> float;
/// The wanted size, or what a viewport smaller than it has room for.
auto fit_to_viewport(const ImVec2& wanted_size) -> ImVec2;

/// Covers the main viewport with the backdrop and begins the card on it. The backdrop takes
/// every click, so nothing behind the card can be reached. end() follows whatever is returned.
auto begin(const char* id, const card_layout& layout) -> bool;
void end();
} // namespace unravel::screen_card
