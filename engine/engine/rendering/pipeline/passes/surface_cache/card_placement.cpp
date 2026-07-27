#include "card_placement.h"

#include <algorithm>
#include <cmath>

namespace unravel
{
namespace surface_cache
{

void face_axes(uint8_t face, math::vec3& out_normal, math::vec3& out_tangent, math::vec3& out_bitangent)
{
    switch(face)
    {
        case 0:
            out_normal = {1.0f, 0.0f, 0.0f};
            out_tangent = {0.0f, 0.0f, 1.0f};
            out_bitangent = {0.0f, 1.0f, 0.0f};
            break;
        case 1:
            out_normal = {-1.0f, 0.0f, 0.0f};
            out_tangent = {0.0f, 0.0f, -1.0f};
            out_bitangent = {0.0f, 1.0f, 0.0f};
            break;
        case 2:
            out_normal = {0.0f, 1.0f, 0.0f};
            out_tangent = {1.0f, 0.0f, 0.0f};
            out_bitangent = {0.0f, 0.0f, 1.0f};
            break;
        case 3:
            out_normal = {0.0f, -1.0f, 0.0f};
            out_tangent = {1.0f, 0.0f, 0.0f};
            out_bitangent = {0.0f, 0.0f, -1.0f};
            break;
        case 4:
            out_normal = {0.0f, 0.0f, 1.0f};
            out_tangent = {-1.0f, 0.0f, 0.0f};
            out_bitangent = {0.0f, 1.0f, 0.0f};
            break;
        default:
            out_normal = {0.0f, 0.0f, -1.0f};
            out_tangent = {1.0f, 0.0f, 0.0f};
            out_bitangent = {0.0f, 1.0f, 0.0f};
            break;
    }
}

auto face_half_extents(uint8_t face, const math::vec3& extents) -> math::vec2
{
    if(face <= 1)
    {
        return {extents.z, extents.y};
    }
    if(face <= 3)
    {
        return {extents.x, extents.z};
    }
    return {extents.x, extents.y};
}

auto face_center_offset(uint8_t face, const math::vec3& extents) -> float
{
    if(face <= 1)
    {
        return extents.x;
    }
    if(face <= 3)
    {
        return extents.y;
    }
    return extents.z;
}

void compute_face_tile_counts(uint8_t face,
                              const math::vec3& extents,
                              float max_card_extent,
                              uint8_t max_tiles_per_axis,
                              uint8_t& out_tiles_u,
                              uint8_t& out_tiles_v)
{
    const math::vec2 face_he = face_half_extents(face, extents);
    const float edge_u = std::max(face_he.x * 2.0f, 1e-3f);
    const float edge_v = std::max(face_he.y * 2.0f, 1e-3f);
    const float max_ext = std::max(max_card_extent, 0.25f);
    out_tiles_u = uint8_t(std::clamp(int(std::ceil(edge_u / max_ext)), 1, int(max_tiles_per_axis)));
    out_tiles_v = uint8_t(std::clamp(int(std::ceil(edge_v / max_ext)), 1, int(max_tiles_per_axis)));
}

auto make_face_tile_card(const math::bbox& world_bounds,
                         uint8_t face,
                         uint8_t tile_u,
                         uint8_t tile_v,
                         uint8_t tiles_u,
                         uint8_t tiles_v) -> card_frame
{
    card_frame frame{};
    face_axes(face, frame.normal, frame.tangent, frame.bitangent);
    const math::vec3 center = world_bounds.get_center();
    const math::vec3 extents = world_bounds.get_extents();
    const math::vec2 face_he = face_half_extents(face, extents);
    const float offset = face_center_offset(face, extents);
    const float tu = float(std::max<uint8_t>(tiles_u, 1));
    const float tv = float(std::max<uint8_t>(tiles_v, 1));
    frame.half_extents = {face_he.x / tu, face_he.y / tv};
    const float u_center = -face_he.x + frame.half_extents.x * (1.0f + 2.0f * float(tile_u));
    const float v_center = -face_he.y + frame.half_extents.y * (1.0f + 2.0f * float(tile_v));
    // Inset toward mesh interior so AABB shells sit closer to real surfaces
    // (curtains/walls) instead of floating on the outer bound.
    const float depth_axis =
        (face <= 1) ? extents.x : ((face <= 3) ? extents.y : extents.z);
    const float inset = std::min(std::max(depth_axis * 0.12f, 0.02f), 0.40f);
    const float inset_offset = std::max(offset - inset, 0.0f);
    frame.origin =
        center + frame.normal * inset_offset + frame.tangent * u_center + frame.bitangent * v_center;
    return frame;
}

} // namespace surface_cache
} // namespace unravel
