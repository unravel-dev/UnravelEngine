#pragma once

#include <math/math.h>

#include <array>
#include <cstdint>
#include <vector>

namespace unravel
{
namespace ps_soa
{

/// Vertices of a cutout polygon: the cutout primitive's fan.
constexpr uint32_t cutout_vertex_count = 8;

/// A frame's cutout: cutout_vertex_count points around its opaque texels, in order around it, in the frame's
/// texture coordinates (0..1, v down). Every point is inside the frame, so the polygon never shows a neighbour.
using cutout_polygon = std::array<math::vec2, cutout_vertex_count>;

/// How a texture sheet's frames follow each other (the shaders' frame order, particle_sheet).
struct cutout_sheet
{
    uint32_t tiles_x = 1;
    uint32_t tiles_y = 1;
    /// Texels whose alpha is above this are kept inside the polygon.
    float alpha_threshold = 0.05f;
    /// Frame blending draws each frame crossfaded into the next: its polygon covers both.
    bool is_blended = false;
    /// The frames run along each row (random row), not through the whole sheet.
    bool is_random_row = false;
    /// The last frame holds instead of wrapping to the first.
    bool is_single_pass = false;
};

/// The convex hull of @p points, counter-clockwise (y up), without collinear points. Fewer than 3 distinct points
/// come back as they are.
auto get_convex_hull(std::vector<math::vec2> points) -> std::vector<math::vec2>;

/**
 * @brief A polygon of at most @p vertex_count vertices around the convex polygon @p hull (counter-clockwise),
 * inside [@p low, @p high]: an edge at a time is folded away by extending its neighbours to where they meet, the
 * one that adds the least area first. Empty when no such polygon exists inside the box.
 */
auto fit_polygon(std::vector<math::vec2> hull, uint32_t vertex_count, const math::vec2& low, const math::vec2& high)
    -> std::vector<math::vec2>;

/// The frame drawn next to @p frame when the sheet blends frames (itself without blending).
auto get_next_cutout_frame(uint32_t frame, const cutout_sheet& sheet) -> uint32_t;

/**
 * @brief Each frame's cutout of a sheet of @p width x @p height RGBA8 texels: the polygon around its texels above
 * the alpha threshold (and the next frame's when blended). A frame with none collapses to a point; a frame whose
 * texels no polygon of cutout_vertex_count vertices fits inside it keeps the whole frame.
 */
auto bake_cutouts(const uint8_t* rgba, uint32_t width, uint32_t height, const cutout_sheet& sheet)
    -> std::vector<cutout_polygon>;

} // namespace ps_soa
} // namespace unravel
