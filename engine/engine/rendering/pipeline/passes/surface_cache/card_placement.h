#pragma once

#include <math/math.h>

#include <cstdint>

namespace unravel
{
namespace surface_cache
{

/**
 * @brief Mesh-fitted card placement from static submesh AABB faces.
 *
 * Cards are inset toward the mesh interior so shells sit closer to real
 * surfaces. Orthographic page capture / G-buffer refine supplies albedo.
 */
struct card_frame
{
    math::vec3 origin{0.0f, 0.0f, 0.0f};
    math::vec3 normal{0.0f, 1.0f, 0.0f};
    math::vec3 tangent{1.0f, 0.0f, 0.0f};
    math::vec3 bitangent{0.0f, 0.0f, 1.0f};
    math::vec2 half_extents{1.0f, 1.0f};
};

void face_axes(uint8_t face, math::vec3& out_normal, math::vec3& out_tangent, math::vec3& out_bitangent);
auto face_half_extents(uint8_t face, const math::vec3& extents) -> math::vec2;
auto face_center_offset(uint8_t face, const math::vec3& extents) -> float;

/**
 * @brief Build a tiled card frame for one AABB face tile.
 */
auto make_face_tile_card(const math::bbox& world_bounds,
                         uint8_t face,
                         uint8_t tile_u,
                         uint8_t tile_v,
                         uint8_t tiles_u,
                         uint8_t tiles_v) -> card_frame;

/**
 * @brief Tile counts along U/V for a face given max edge length.
 */
void compute_face_tile_counts(uint8_t face,
                              const math::vec3& extents,
                              float max_card_extent,
                              uint8_t max_tiles_per_axis,
                              uint8_t& out_tiles_u,
                              uint8_t& out_tiles_v);

} // namespace surface_cache
} // namespace unravel
