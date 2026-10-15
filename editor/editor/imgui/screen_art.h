#pragma once

#include "integration/imgui.h"

#include <imgui/imgui_internal.h>

#include <string>

/**
 * @brief The pictures the start page and the loading screen share: an image cropped to fill its
 * frame, the stand-in of something that has no image, and the spinner of work in progress.
 */
namespace unravel::screen_art
{
/// The color a name always gets, so a project looks the same wherever it shows up.
auto get_name_color(const std::string& name) -> ImU32;

/// Covers the rect with the texture, cropped to the aspect of the rect (never stretched).
void draw_image_cover(ImDrawList* draw_list,
                      const gfx::texture::ptr& texture,
                      const ImRect& rect,
                      float rounding,
                      ImDrawFlags corners = ImDrawFlags_RoundCornersAll,
                      ImU32 tint = IM_COL32_WHITE);

/// Fills the rect with a gradient of the color from top left to bottom right.
void draw_gradient(ImDrawList* draw_list,
                   const ImRect& rect,
                   ImU32 color,
                   float rounding,
                   ImDrawFlags corners = ImDrawFlags_RoundCornersAll);

/// What stands in for the image of something without one: a gradient of the color of its name
/// with the initials of the name, or the glyph when one is given, in the middle.
void draw_placeholder(ImDrawList* draw_list,
                      const ImRect& rect,
                      const std::string& name,
                      float rounding,
                      ImDrawFlags corners = ImDrawFlags_RoundCornersAll,
                      const char* glyph = nullptr);

/// A turning arc on a faint ring: work in progress of unknown length.
void draw_spinner(ImDrawList* draw_list, const ImVec2& center, float radius, float thickness, ImU32 color);
} // namespace unravel::screen_art
