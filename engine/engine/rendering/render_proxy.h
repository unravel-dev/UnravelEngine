#pragma once
#include <engine/engine_export.h>

#include <hpp/small_vector.hpp>
#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief Retained per-model render proxy data used by the culling/LOD code.
 *
 * Stores cached world-space bounds for every renderable surface of a model:
 *  - one AABB per (submesh, transform instance) pair for non-skinned submeshes,
 *    mirroring the layout of @ref submesh_pose_mat4 (outer index = submesh index,
 *    inner index = transform instance),
 *  - one animated AABB per submesh index for skinned submeshes, derived from the
 *    per-bone bind-space bounds transformed by the current bone world transforms.
 *
 * The data is refreshed by @ref model_component whenever poses/transforms are
 * updated (dirty tracking via @ref version), and consumed by the @ref model
 * submit paths so per-submesh visibility tests become cheap AABB-vs-frustum
 * checks instead of per-draw OBB classification. Unpopulated bounds mean "no
 * cached data" and consumers must fall back to conservative behavior (draw).
 */
struct submesh_render_proxies
{
    /// Cached world-space bounds per (submesh, transform instance). Mirrors
    /// submesh_pose_mat4::submesh_to_transform_indices layout.
    hpp::small_vector<hpp::small_vector<math::bbox>> instance_bounds;

    /// Cached world-space animated bounds per skinned submesh index.
    std::vector<math::bbox> skinned_bounds;

    /// Union of all skinned submesh bounds (world space). Unpopulated when the
    /// model has no skinned submeshes or bone bounds are unavailable.
    math::bbox animated_bounds;

    /// Union of all populated per-instance bounds (world space), accumulated during
    /// refresh so whole-model bounds don't need to walk the per-instance lists.
    math::bbox instance_bounds_union;

    /// Increments on every refresh so consumers can detect staleness.
    uint64_t version{0};

    void clear()
    {
        instance_bounds.clear();
        skinned_bounds.clear();
        animated_bounds = {};
        instance_bounds_union = {};
    }

    void begin_refresh(size_t submesh_count)
    {
        instance_bounds.clear();
        instance_bounds.resize(submesh_count);
        skinned_bounds.clear();
        animated_bounds = {};
        instance_bounds_union = {};
        version++;
    }

    void add_instance_bounds(uint32_t submesh_index, const math::bbox& world_bounds)
    {
        if(submesh_index >= instance_bounds.size())
        {
            instance_bounds.resize(submesh_index + 1);
        }
        instance_bounds[submesh_index].emplace_back(world_bounds);
        if(world_bounds.is_populated())
        {
            instance_bounds_union.add_point(world_bounds.min);
            instance_bounds_union.add_point(world_bounds.max);
        }
    }

    /**
     * @brief Gets the cached world bounds for a (submesh, instance) pair.
     * @return Pointer to populated bounds, or nullptr when no cached data exists.
     */
    auto get_instance_bounds(uint32_t submesh_index, size_t instance_index) const -> const math::bbox*
    {
        if(submesh_index >= instance_bounds.size())
        {
            return nullptr;
        }
        const auto& bounds_list = instance_bounds[submesh_index];
        if(instance_index >= bounds_list.size())
        {
            return nullptr;
        }
        const auto& bounds = bounds_list[instance_index];
        return bounds.is_populated() ? &bounds : nullptr;
    }

    /**
     * @brief Gets the cached animated world bounds for a skinned submesh.
     * @return Pointer to populated bounds, or nullptr when no cached data exists.
     */
    auto get_skinned_bounds(uint32_t submesh_index) const -> const math::bbox*
    {
        if(submesh_index >= skinned_bounds.size())
        {
            return nullptr;
        }
        const auto& bounds = skinned_bounds[submesh_index];
        return bounds.is_populated() ? &bounds : nullptr;
    }

    auto has_animated_bounds() const -> bool
    {
        return animated_bounds.is_populated();
    }

    auto has_instance_bounds() const -> bool
    {
        return instance_bounds_union.is_populated();
    }

    /// True when any cached bounds exist that can diverge from the static mesh box
    /// (node-driven submesh placements or skinned pose bounds).
    auto has_pose_bounds() const -> bool
    {
        return has_instance_bounds() || has_animated_bounds();
    }
};

} // namespace unravel
