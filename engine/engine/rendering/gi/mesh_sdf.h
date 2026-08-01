#pragma once

#include <engine/engine_export.h>

#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief Sparse signed distance field baked per mesh asset, expressed in mesh local space.
 *
 * The local-space bounds are divided into a regular grid of @ref brick_size ^ 3 voxel
 * bricks. Bricks the surface passes through store their voxels in @ref brick_voxels;
 * bricks that contain no surface store no voxels at all and instead carry a conservative
 * distance directly in their @ref indirection entry, so a ray crossing empty space takes
 * one large step without touching brick memory.
 *
 * All distances are in LOCAL space units. A uniformly scaled instance can trace the same
 * field by scaling the ray; non-uniform scale is handled by transforming the ray into
 * local space and correcting the step length by the smallest scale axis.
 */
struct mesh_sdf
{
    /// Interior voxels per brick edge. 8 keeps the indirection volume 512x smaller than
    /// the equivalent dense grid while staying small enough that a brick covers a single
    /// coherent region of the surface.
    static constexpr uint32_t brick_size = 8;
    /// Voxels of overlap replicated on every brick face. Trilinear filtering of a voxel on
    /// the brick boundary reads its neighbours, which live in a different (or absent)
    /// brick; without this border the filter would read unrelated atlas memory and produce
    /// a discontinuity in the distance field at every brick seam, which sphere tracing
    /// turns into visible cracks. One voxel is exactly what trilinear needs.
    static constexpr uint32_t brick_border = 1;
    /// Stored voxels per brick edge, border included.
    static constexpr uint32_t brick_stride = brick_size + 2u * brick_border;
    static constexpr uint32_t brick_voxel_count = brick_stride * brick_stride * brick_stride;

    /// Voxel distances are stored as R8 unorm covering [-encode_range, +encode_range]
    /// voxels and saturate beyond it.
    ///
    /// The field is therefore a NARROW BAND, and this is the contract every consumer must
    /// respect: a sampled value is a conservative UNDER-ESTIMATE of the true distance, never
    /// an over-estimate. Two consequences:
    ///   - Sphere tracing is always safe (a step never overshoots into geometry), but takes
    ///     unnecessarily small steps more than encode_range voxels from a surface. Long-range
    ///     skipping is the job of the empty-brick distances in @ref indirection, not of the
    ///     voxels.
    ///   - The field is NOT continuous where a surface brick meets an empty brick: the
    ///     surface brick saturates while its empty neighbour reports a much larger distance.
    ///     Sphere tracing does not need continuity, only conservativeness, so this is by
    ///     design. Do not "fix" it by widening the encode range; that costs precision in the
    ///     band that actually matters.
    static constexpr float encode_range = 4.0f;

    /// Set on an indirection entry that references no brick storage.
    static constexpr uint32_t indirection_empty_flag = 0x80000000u;
    /// Set together with @ref indirection_empty_flag when that empty region is INSIDE the
    /// surface. Tracing must treat an inside region as an immediate hit rather than as
    /// free space, otherwise rays that start inside geometry escape through it.
    static constexpr uint32_t indirection_inside_flag = 0x40000000u;
    /// Conservative distance to the nearest surface, in whole voxels, for empty entries.
    static constexpr uint32_t indirection_distance_mask = 0x00FFFFFFu;
    static constexpr uint32_t indirection_max_distance = indirection_distance_mask;

    /// Local-space region the grid covers. Always exactly
    /// `grid_dim * voxel_size` in size, so voxel addressing needs no clamping fixups.
    ///
    /// The bake pads this outward from the mesh by at least @ref encode_range voxels on every
    /// side, so the surface is never closer than @ref get_bounds_padding to the boundary.
    /// Sampling relies on that: see @ref get_bounds_padding.
    math::bbox bounds{};
    /// Local-space edge length of one voxel.
    float voxel_size = 0.0f;
    /// Grid size in voxels. Always `brick_dim * brick_size`.
    math::uvec3 grid_dim{0u, 0u, 0u};
    /// Grid size in bricks.
    math::uvec3 brick_dim{0u, 0u, 0u};
    /// True when the source mesh was baked unsigned (foliage, single-sided cards). Tracing
    /// must not use the sign, and must treat the field as a thin shell of
    /// @ref two_sided_thickness around the surface.
    bool is_two_sided = false;
    /// Local-space half-thickness applied to two-sided fields.
    float two_sided_thickness = 0.0f;

    /// One entry per brick, indexed `x + y * brick_dim.x + z * brick_dim.x * brick_dim.y`.
    /// See the INDIRECTION_* constants for the encoding.
    std::vector<uint32_t> indirection;
    /// Voxel storage for surface bricks only, @ref brick_voxel_count bytes per brick,
    /// voxel-major within a brick (`x + y * brick_stride + z * brick_stride * brick_stride`).
    /// Local coordinate 0 is the border voxel, so interior voxel `i` sits at `i + brick_border`.
    std::vector<uint8_t> brick_voxels;

    /**
     * @brief O(1) structural check for the SAMPLING path.
     *
     * @ref is_valid additionally verifies every indirection entry against the stored brick count,
     * which is linear in the brick count and belongs at load or upload time. Calling it per
     * sample made each lookup hundreds of times more expensive than the lookup itself, and it
     * did not show up as a slow function -- it showed up as the whole clipmap composition being
     * inexplicably slow while every count around it looked healthy.
     *
     * The out-of-range protection that scan provided is not lost: sampling range-checks the one
     * entry it actually dereferences, which is the same guarantee for O(1) instead of O(bricks).
     */
    auto is_sampleable() const -> bool
    {
        return voxel_size > 0.0f && !indirection.empty() && !brick_voxels.empty();
    }

    /**
     * @brief Whether this field holds usable data.
     *
     * Thorough and LINEAR in the brick count. For load, deserialization and atlas upload -- not
     * for anything that runs per sample; see @ref is_sampleable.
     */
    auto is_valid() const -> bool
    {
        if(!(voxel_size > 0.0f) || grid_dim.x == 0u || grid_dim.y == 0u || grid_dim.z == 0u)
        {
            return false;
        }
        if(indirection.size() != size_t(brick_dim.x) * brick_dim.y * brick_dim.z || indirection.empty())
        {
            return false;
        }
        if(brick_voxels.size() % brick_voxel_count != 0u)
        {
            return false;
        }
        // Every surface entry indexes brick storage, so the storage must be large enough for
        // the highest index any entry references. Checking this here is what stops a field
        // that lost its voxel payload (a truncated asset, a failed deserialize) from passing
        // validation and then reading out of bounds during atlas upload -- which renders as
        // plausible-looking noise rather than as an error.
        const size_t stored_bricks = brick_voxels.size() / brick_voxel_count;
        for(uint32_t entry : indirection)
        {
            if(!is_empty_entry(entry) && size_t(entry) >= stored_bricks)
            {
                return false;
            }
        }
        return true;
    }

    /// True when the indirection entry references no brick storage. Duplicated as a member so
    /// @ref is_valid can be self-contained; @ref is_sdf_empty_entry is the public spelling.
    static auto is_empty_entry(uint32_t entry) -> bool
    {
        return (entry & indirection_empty_flag) != 0u;
    }

    /**
     * @brief Minimum local-space distance from the field bounds to the mesh surface.
     *
     * The bake expands the mesh bounds by @ref encode_range voxels (and then rounds up to
     * whole bricks, which only adds more), so no part of the surface can lie within this
     * distance of the boundary.
     *
     * Sampling MUST add this to the distance-to-bounds it reports for a position outside the
     * field. Reporting the bare distance-to-bounds is conservative but degenerate: it is zero
     * exactly on the boundary, which is precisely where a ray enters, so a sphere trace
     * registers an immediate hit and renders the bounding box instead of the mesh. Adding the
     * padding stays conservative -- any straight path from an outside point to the surface
     * crosses the boundary, so the true distance is at least distance-to-bounds plus this.
     */
    auto get_bounds_padding() const -> float
    {
        return encode_range * voxel_size;
    }

    /**
     * @brief Number of bricks that actually own voxel storage.
     */
    auto get_surface_brick_count() const -> uint32_t
    {
        return uint32_t(brick_voxels.size() / brick_voxel_count);
    }

    /**
     * @brief Total voxel storage in bytes, for budgeting and the profiler overlay.
     */
    auto get_memory_usage() const -> size_t
    {
        return brick_voxels.size() + indirection.size() * sizeof(uint32_t);
    }
};

/**
 * @brief Encodes a distance, given in voxels, into the R8 unorm storage representation.
 */
inline auto encode_sdf_distance(float distance_in_voxels) -> uint8_t
{
    const float normalized = distance_in_voxels / (2.0f * mesh_sdf::encode_range) + 0.5f;
    return uint8_t(math::clamp(normalized, 0.0f, 1.0f) * 255.0f + 0.5f);
}

/**
 * @brief Decodes an R8 unorm voxel back to a distance in voxels.
 */
inline auto decode_sdf_distance(uint8_t encoded) -> float
{
    return (float(encoded) / 255.0f - 0.5f) * (2.0f * mesh_sdf::encode_range);
}

/**
 * @brief Builds the indirection entry for a brick that owns voxel storage.
 */
inline auto make_sdf_surface_entry(uint32_t brick_index) -> uint32_t
{
    return brick_index & ~(mesh_sdf::indirection_empty_flag | mesh_sdf::indirection_inside_flag);
}

/**
 * @brief Builds the indirection entry for a brick with no surface in it.
 * @param distance_in_voxels Conservative (under-estimated) distance to the nearest surface.
 * @param is_inside          True when the region lies inside the surface.
 */
inline auto make_sdf_empty_entry(uint32_t distance_in_voxels, bool is_inside) -> uint32_t
{
    const uint32_t clamped = math::min(distance_in_voxels, mesh_sdf::indirection_max_distance);
    return mesh_sdf::indirection_empty_flag | (is_inside ? mesh_sdf::indirection_inside_flag : 0u) | clamped;
}

/**
 * @brief True when the indirection entry references no brick storage.
 */
inline auto is_sdf_empty_entry(uint32_t entry) -> bool
{
    return (entry & mesh_sdf::indirection_empty_flag) != 0u;
}

} // namespace unravel
