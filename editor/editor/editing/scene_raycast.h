#pragma once

#include <engine/ecs/scene.h>
#include <math/math.h>

#include <limits>
#include <vector>

namespace unravel
{

/// A ray through a scene: from @ref origin along @ref direction, no further than @ref max_distance.
struct scene_ray
{
    math::vec3 origin{};
    /// Normalized.
    math::vec3 direction{0.0f, 0.0f, 1.0f};
    float max_distance{std::numeric_limits<float>::max()};
};

/// Where a ray met a model's surface.
struct scene_ray_hit
{
    entt::handle entity;
    math::vec3 point{};
    /// Facing the ray's origin.
    math::vec3 normal{};
    float distance{};
    uint32_t submesh_index{};
};

/**
 * @brief The model surfaces a ray crosses, nearest first, one hit per entity.
 *
 * Tests the triangles each submesh is drawn with, where it is drawn: at each node transform it is
 * mapped to, else at the entity's own transform. Needs no physics, so it answers while editing.
 * Skinned submeshes are tested in their bind pose. Inactive entities, disabled models and models
 * whose mesh has not loaded are skipped.
 *
 * @param skip Entities left out, together with everything under them.
 */
auto raycast_scene_models(scene& scn, const scene_ray& ray, const std::vector<entt::handle>& skip = {})
    -> std::vector<scene_ray_hit>;

} // namespace unravel
