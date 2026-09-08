#pragma once

#include <engine/rendering/gi/gi_constants.h>

#include <math/math.h>

#include <cmath>

namespace unravel::gi
{

/**
 * The emitter table's fourth float - the "power lane" - and the selection weight derived
 * from it. THE SINGLE OWNER of that encoding on the CPU side; its mirror is GiLoadEmitter
 * in engine_data/data/shaders/gi/gi_emissive_nee.sh, and the "gi emitter packing" suite
 * pins the two together.
 *
 * The table has no room for both the piece's extent and its power, so the upload spends the
 * lane on the extent and the shader RECONSTRUCTS the power from what it decodes. Both sides
 * must therefore agree on one definition of the weight, which is why it lives here rather
 * than being spelled out at each site.
 */

/// The marker an unpacked (positive) power lane can never produce.
constexpr float EMITTER_EXTENT_LANE_OFFSET = 1.0f;
/// Quantisation of one axis: 8 bits over [0, GI_EMISSIVE_NEE_SEGMENT] metres.
constexpr int EMITTER_EXTENT_LANE_STEPS = 255;

/// One axis of an extent as its 8-bit code, exact in a float.
inline auto quantize_emitter_extent_axis(float metres) -> float
{
    const float unit = metres / float(GI_EMISSIVE_NEE_SEGMENT);
    return float(math::clamp(int(std::lround(unit * float(EMITTER_EXTENT_LANE_STEPS))), 0, EMITTER_EXTENT_LANE_STEPS));
}

/// The power lane for a piece of the given extent: three 8-bit axes, negated and offset so a
/// shader fed by an older upload (a positive power) still reads "no extent".
inline auto pack_emitter_extent_lane(const math::vec3& extent) -> float
{
    const float x8 = quantize_emitter_extent_axis(extent.x);
    const float y8 = quantize_emitter_extent_axis(extent.y);
    const float z8 = quantize_emitter_extent_axis(extent.z);
    return -(x8 + y8 * 256.0f + z8 * 65536.0f) - EMITTER_EXTENT_LANE_OFFSET;
}

/// Whether @p lane carries a packed extent rather than a legacy power.
inline auto has_emitter_extent_lane(float lane) -> bool
{
    return lane < -0.5f;
}

/// The extent @ref pack_emitter_extent_lane encoded, or zero for a legacy power lane.
inline auto unpack_emitter_extent_lane(float lane) -> math::vec3
{
    if(!has_emitter_extent_lane(lane))
    {
        return math::vec3(0.0f, 0.0f, 0.0f);
    }
    const float packed = -lane - EMITTER_EXTENT_LANE_OFFSET;
    const float x8 = std::floor(std::fmod(packed, 256.0f));
    const float y8 = std::floor(std::fmod(packed / 256.0f, 256.0f));
    const float z8 = std::floor(packed / 65536.0f);
    return math::vec3(x8, y8, z8) * (float(GI_EMISSIVE_NEE_SEGMENT) / float(EMITTER_EXTENT_LANE_STEPS));
}

/// Full surface area of an axis-aligned box of the given extent. The reflection tier's
/// analytic near-field term spreads a piece's power over this, treating it as a closed box.
inline auto emitter_surface_area(const math::vec3& extent) -> float
{
    return 2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
}

/// The area of one emitter piece that can actually radiate: its two largest faces.
///
/// Not the full box area, which the ranking used to take. Subdivision is VOLUMETRIC while
/// emission is a surface, so a thick emissive solid produces interior pieces that emit
/// nothing at all - and a 1x1x1 interior piece scores 6 on box area against ~2.04 for a
/// 1x1x0.01 piece of a real emitting panel, so the pieces that emit nothing outranked the
/// ones that do by three to one. The largest face pair scores both at 2: it cannot tell an
/// interior piece from a surface one either, but it stops rewarding the interior for its
/// thickness, and for the thin panels most emissive content is actually made of it is the
/// correct emitting area rather than an approximation of it.
inline auto emitter_emitting_area(const math::vec3& extent) -> float
{
    const float smallest = math::min(extent.x, math::min(extent.y, extent.z));
    const float largest = math::max(extent.x, math::max(extent.y, extent.z));
    // The middle axis falls out of the sum; no sort needed.
    const float middle = (extent.x + extent.y + extent.z) - smallest - largest;
    return 2.0f * largest * middle;
}

/// The ranking weight for one emitter piece: how much light it puts out, up to the inverse
/// square the consumers divide by. Radiance is per unit area, so a piece's standing among
/// its neighbours is its luminance times the area that emits - the CPU orders the table by
/// this and the reflection tier's near-field top-K scores by this over distance squared.
inline auto emitter_selection_weight(float luminance, const math::vec3& extent) -> float
{
    return luminance * emitter_emitting_area(extent);
}

} // namespace unravel::gi
