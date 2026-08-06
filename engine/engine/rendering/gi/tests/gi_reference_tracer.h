#pragma once

/*
 * CPU reference path tracer over baked mesh SDFs (plan: tasks/gi_rewrite_plan.md, section 9.2).
 *
 * This is the harness's ground-truth oracle. It traces the EXACT per-instance fields the bake
 * produced - brute force over every instance, no clipmap, no instance grid, no budget - so the
 * number it produces is "the best answer our scene REPRESENTATION admits". Comparing the runtime
 * against it therefore measures the runtime's approximations (clipmap coarseness, budgets,
 * filters), while representation error itself stays pinned by the existing bake accuracy tests.
 *
 * Deliberately test-only code: lives in the tests directory, is compiled into gi_tests alone,
 * and favours clarity and physical correctness over speed.
 */

#include <engine/rendering/gi/mesh_sdf.h>

#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel::gi_reference
{

/// One placed field with the surface properties the runtime will eventually voxelise.
struct reference_instance
{
    ///< Borrowed; must outlive the scene.
    const mesh_sdf* sdf = nullptr;
    math::mat4 world_to_local{1.0f};
    math::bbox world_bounds{};
    ///< Smallest scale axis: converts a local distance to a conservative world distance.
    float local_to_world_scale = 1.0f;
    ///< Lambertian reflectance, linear.
    math::vec3 albedo{0.5f};
    ///< Outgoing emission, radiance units.
    math::vec3 emissive{0.0f};
};

/// Point light with inverse-square falloff: irradiance on a facing surface at distance r is
/// intensity / r^2 (radiant intensity units).
struct reference_point_light
{
    math::vec3 position{0.0f};
    math::vec3 intensity{0.0f};
};

struct reference_directional_light
{
    ///< Direction light TRAVELS (from the light toward the scene), normalised.
    math::vec3 direction{0.0f, -1.0f, 0.0f};
    math::vec3 radiance{0.0f};
};

struct reference_scene
{
    std::vector<reference_instance> instances;
    std::vector<reference_point_light> point_lights;
    std::vector<reference_directional_light> directional_lights;
    ///< Radiance of every escaped ray, uniform over the sphere. A furnace when instances have
    ///< albedo 1 and no lights exist.
    math::vec3 sky_radiance{0.0f};
};

struct reference_hit
{
    bool hit = false;
    float t = 0.0f;
    math::vec3 position{0.0f};
    ///< Field gradient at the hit, normalised.
    math::vec3 normal{0.0f, 1.0f, 0.0f};
    ///< Instance whose field produced the hit, for material lookup.
    uint32_t instance = 0;
};

/// Signed world distance to the nearest instance surface: min over every instance of its
/// conservatively scaled local sample. @p out_instance receives the winning instance.
auto scene_distance(const reference_scene& scene, const math::vec3& world_position, uint32_t& out_instance)
    -> float;

/**
 * @brief Sphere traces the scene. Conservative fields make plain sphere tracing exact for
 *        hit/miss purposes; the step count is uncapped in spirit (the cap exists only against
 *        degenerate rays) because a reference must not trade correctness for time.
 */
auto trace(const reference_scene& scene,
           const math::vec3& origin,
           const math::vec3& direction,
           float t_max) -> reference_hit;

struct integrate_params
{
    ///< Cosine-sampled paths from the evaluation point.
    uint32_t sample_count = 1024;
    ///< Path depth. Error of a truncated depth is the energy still in flight at the cut, which
    ///< the furnace test bounds empirically.
    uint32_t max_bounces = 6;
    ///< Deterministic base seed; two runs with equal seeds are bit-identical.
    uint32_t seed = 1u;
};

/**
 * @brief Irradiance arriving at a surface point - the quantity the GI system estimates.
 *
 * Forward path tracing with next-event estimation at every vertex: direct light via shadow rays,
 * sky at path escape, emission on hit. Ray origins are lifted off the field isosurface by the
 * reported (negative) distance plus half a local voxel, because the isosurface sits displaced
 * from the analytic surface by up to a voxel and a reference that starts inside it measures
 * its own launch surface.
 */
auto integrate_irradiance(const reference_scene& scene,
                          const math::vec3& position,
                          const math::vec3& normal,
                          const integrate_params& params) -> math::vec3;

/// Helper: builds a reference_instance from a baked field placed with a world transform,
/// deriving bounds and the conservative scale from the transform.
auto make_instance(const mesh_sdf& sdf,
                   const math::mat4& local_to_world,
                   const math::vec3& albedo,
                   const math::vec3& emissive = math::vec3(0.0f)) -> reference_instance;

} // namespace unravel::gi_reference
