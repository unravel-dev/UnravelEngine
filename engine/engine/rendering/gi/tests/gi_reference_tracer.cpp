#include "gi_reference_tracer.h"

#include <engine/rendering/gi/mesh_sdf_baker.h>

#include <poolstl/poolstl.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace unravel::gi_reference
{

namespace
{

/// Guard against a degenerate ray spinning forever; a reference march is otherwise uncapped.
/// Exhausting it is treated as a hit at the current t - the conservative direction, and the
/// same contract the runtime tracer has (gi_constants.h, GI_TRACE_MAX_STEPS rationale).
constexpr uint32_t reference_max_steps = 8192;

/// Hit acceptance and gradient step, as fractions of the winning instance's WORLD voxel. The
/// units-of-the-thing rule: everything scaled to a voxel names which voxel.
constexpr float hit_epsilon_voxels = 0.25f;
constexpr float gradient_step_voxels = 0.5f;

/// PCG-family integer hash, matching the shaders' GiHashUint so sequences can be compared
/// across CPU and GPU if ever needed.
auto hash_uint(uint32_t value) -> uint32_t
{
    uint32_t state = value * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

auto hash_combine(uint32_t seed, uint32_t value) -> uint32_t
{
    return hash_uint(seed ^ (value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u)));
}

/// Advances the state and returns a uniform float in [0, 1).
auto next_uniform(uint32_t& state) -> float
{
    state = hash_uint(state);
    return float(state & 0x00FFFFFFu) / float(0x01000000);
}

/// Distance from a point to an axis-aligned box, zero inside.
auto distance_to_bounds(const math::bbox& bounds, const math::vec3& p) -> float
{
    const math::vec3 lo = bounds.min;
    const math::vec3 hi = bounds.max;
    const math::vec3 d = math::max(math::max(lo - p, p - hi), math::vec3(0.0f));
    return math::length(d);
}

/// World-space size of one voxel of an instance's field, conservative under non-uniform scale.
auto instance_world_voxel(const reference_instance& instance) -> float
{
    return instance.sdf->voxel_size * instance.local_to_world_scale;
}

/// Branch-free orthonormal basis around a normal (same construction as the shaders).
void build_basis(const math::vec3& n, math::vec3& t, math::vec3& b)
{
    const float s = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (s + n.z);
    const float c = n.x * n.y * a;
    t = math::vec3(1.0f + s * n.x * n.x * a, s * c, -s * n.x);
    b = math::vec3(c, s + n.y * n.y * a, -n.y);
}

/// Cosine-weighted hemisphere direction: the plain mean of sampled radiance then estimates
/// irradiance / pi with no per-sample cosine factor.
auto cosine_direction(const math::vec3& n, float u1, float u2) -> math::vec3
{
    math::vec3 t;
    math::vec3 b;
    build_basis(n, t, b);
    const float r = std::sqrt(u1);
    const float phi = 6.2831853f * u2;
    return math::normalize(t * (r * std::cos(phi)) + b * (r * std::sin(phi)) +
                           n * std::sqrt(math::max(0.0f, 1.0f - u1)));
}

/// Lifts a launch point off the field isosurface: out by however far inside it sits, plus half
/// the local voxel. The isosurface is displaced from the analytic surface by up to a voxel, and
/// a ray started inside it measures its own launch surface.
auto lift_off_surface(const reference_scene& scene, const math::vec3& position, const math::vec3& normal)
    -> math::vec3
{
    uint32_t winner = 0;
    const float d = scene_distance(scene, position, winner);
    const float voxel =
        scene.instances.empty() ? 0.01f : instance_world_voxel(scene.instances[winner]);
    const float lift = math::max(0.0f, -d) + 0.5f * voxel;
    return position + normal * lift;
}

/// Whether a shadow ray from @p origin reaches @p target unoccluded.
auto is_visible(const reference_scene& scene, const math::vec3& origin, const math::vec3& target) -> bool
{
    const math::vec3 to_target = target - origin;
    const float distance = math::length(to_target);
    if(distance <= 1e-6f)
    {
        return true;
    }
    const reference_hit hit = trace(scene, origin, to_target / distance, distance);
    return !hit.hit;
}

/// Direct irradiance at a lifted surface point from every scene light, shadow rays included.
auto direct_irradiance(const reference_scene& scene, const math::vec3& position, const math::vec3& normal)
    -> math::vec3
{
    math::vec3 result(0.0f);
    for(const auto& light : scene.point_lights)
    {
        const math::vec3 to_light = light.position - position;
        const float r2 = math::dot(to_light, to_light);
        if(r2 <= 1e-8f)
        {
            continue;
        }
        const math::vec3 direction = to_light / std::sqrt(r2);
        const float cosine = math::dot(normal, direction);
        if(cosine <= 0.0f)
        {
            continue;
        }
        if(!is_visible(scene, position, light.position))
        {
            continue;
        }
        result += light.intensity * (cosine / r2);
    }
    for(const auto& light : scene.directional_lights)
    {
        const math::vec3 direction = -light.direction;
        const float cosine = math::dot(normal, direction);
        if(cosine <= 0.0f)
        {
            continue;
        }
        // Any hit within the scene's extent occludes; a directional light sits at infinity.
        const reference_hit hit = trace(scene, position, direction, 1e6f);
        if(hit.hit)
        {
            continue;
        }
        result += light.radiance * cosine;
    }
    return result;
}

/// Radiance arriving at a point from one sampled direction: a forward path with next-event
/// estimation at every vertex. Sky is added only at escape, so it is never double counted.
auto path_radiance(const reference_scene& scene,
                   math::vec3 origin,
                   math::vec3 direction,
                   uint32_t max_bounces,
                   uint32_t& rng) -> math::vec3
{
    math::vec3 radiance(0.0f);
    math::vec3 throughput(1.0f);
    for(uint32_t bounce = 0; bounce <= max_bounces; ++bounce)
    {
        const reference_hit hit = trace(scene, origin, direction, 1e6f);
        if(!hit.hit)
        {
            radiance += throughput * scene.sky_radiance;
            break;
        }
        const reference_instance& instance = scene.instances[hit.instance];
        // The gradient points out of the field; a ray arriving at a surface must see the side
        // facing it, so flip when the march converged from the far side of a thin feature.
        math::vec3 normal = hit.normal;
        if(math::dot(normal, direction) > 0.0f)
        {
            normal = -normal;
        }
        radiance += throughput * instance.emissive;
        const math::vec3 lifted = lift_off_surface(scene, hit.position, normal);
        const math::vec3 direct = direct_irradiance(scene, lifted, normal);
        radiance += throughput * instance.albedo * (direct / math::pi<float>());
        throughput *= instance.albedo;
        // Russian roulette is deliberately absent: deterministic cost, and depth truncation
        // error is bounded empirically by the furnace test.
        const float u1 = next_uniform(rng);
        const float u2 = next_uniform(rng);
        direction = cosine_direction(normal, u1, u2);
        origin = lifted;
    }
    return radiance;
}

} // namespace

auto scene_distance(const reference_scene& scene, const math::vec3& world_position, uint32_t& out_instance)
    -> float
{
    float nearest = 1e6f;
    out_instance = 0;
    for(uint32_t i = 0; i < uint32_t(scene.instances.size()); ++i)
    {
        const reference_instance& instance = scene.instances[i];
        const float to_bounds = distance_to_bounds(instance.world_bounds, world_position);
        if(to_bounds > 0.0f)
        {
            // Conservative lower bound without sampling: the surface lies inside the bounds by
            // at least the field's padding, so this cannot over-estimate.
            const float bound =
                to_bounds + instance.sdf->get_bounds_padding() * instance.local_to_world_scale;
            if(bound < nearest)
            {
                nearest = bound;
                out_instance = i;
            }
            continue;
        }
        const math::vec3 local = math::vec3(instance.world_to_local * math::vec4(world_position, 1.0f));
        const float distance =
            sample_mesh_sdf(*instance.sdf, local) * instance.local_to_world_scale;
        if(distance < nearest)
        {
            nearest = distance;
            out_instance = i;
        }
    }
    return nearest;
}

auto trace(const reference_scene& scene,
           const math::vec3& origin,
           const math::vec3& direction,
           float t_max) -> reference_hit
{
    reference_hit result;
    float t = 0.0f;
    for(uint32_t step = 0; step < reference_max_steps; ++step)
    {
        const math::vec3 p = origin + direction * t;
        uint32_t winner = 0;
        const float d = scene_distance(scene, p, winner);
        const float voxel = scene.instances.empty() ? 0.01f : instance_world_voxel(scene.instances[winner]);
        const float epsilon = hit_epsilon_voxels * voxel;
        if(d < epsilon)
        {
            result.hit = true;
            result.t = t;
            result.position = p;
            result.instance = winner;
            const float h = math::max(gradient_step_voxels * voxel, 1e-4f);
            uint32_t ignored = 0;
            const math::vec3 gradient(
                scene_distance(scene, p + math::vec3(h, 0, 0), ignored) -
                    scene_distance(scene, p - math::vec3(h, 0, 0), ignored),
                scene_distance(scene, p + math::vec3(0, h, 0), ignored) -
                    scene_distance(scene, p - math::vec3(0, h, 0), ignored),
                scene_distance(scene, p + math::vec3(0, 0, h), ignored) -
                    scene_distance(scene, p - math::vec3(0, 0, h), ignored));
            const float length = math::length(gradient);
            if(length > 1e-6f)
            {
                result.normal = gradient / length;
            }
            return result;
        }
        t += d;
        if(t >= t_max)
        {
            return result;
        }
    }
    // Step budget exhausted: report a hit at the current position - over-occlusion, never a
    // silent pass-through (the same contract the runtime holds).
    result.hit = true;
    result.t = t;
    result.position = origin + direction * t;
    uint32_t winner = 0;
    scene_distance(scene, result.position, winner);
    result.instance = winner;
    return result;
}

auto integrate_irradiance(const reference_scene& scene,
                          const math::vec3& position,
                          const math::vec3& normal,
                          const integrate_params& params) -> math::vec3
{
    const math::vec3 lifted = lift_off_surface(scene, position, normal);
    // Direct at the evaluation point itself, once - it is not sample-dependent.
    const math::vec3 direct = direct_irradiance(scene, lifted, normal);
    std::vector<math::vec3> samples(params.sample_count, math::vec3(0.0f));
    std::vector<uint32_t> indices(params.sample_count);
    std::iota(indices.begin(), indices.end(), 0u);
    std::for_each(poolstl::par,
                  indices.begin(),
                  indices.end(),
                  [&](uint32_t sample_index)
                  {
                      uint32_t rng = hash_combine(hash_uint(params.seed), sample_index);
                      const float u1 = next_uniform(rng);
                      const float u2 = next_uniform(rng);
                      const math::vec3 direction = cosine_direction(normal, u1, u2);
                      samples[sample_index] =
                          path_radiance(scene, lifted, direction, params.max_bounces, rng);
                  });
    math::dvec3 sum(0.0);
    for(const auto& sample : samples)
    {
        sum += math::dvec3(sample);
    }
    // Cosine-sampled estimator: E = pi * mean(L).
    const math::dvec3 mean = sum / double(math::max<uint32_t>(params.sample_count, 1u));
    return math::vec3(mean * math::pi<double>());
}

auto make_instance(const mesh_sdf& sdf,
                   const math::mat4& local_to_world,
                   const math::vec3& albedo,
                   const math::vec3& emissive) -> reference_instance
{
    reference_instance instance;
    instance.sdf = &sdf;
    instance.world_to_local = glm::inverse(local_to_world);
    instance.albedo = albedo;
    instance.emissive = emissive;
    // World bounds: transform the local bounds' corners.
    instance.world_bounds.reset();
    for(int corner = 0; corner < 8; ++corner)
    {
        const math::vec3 local((corner & 1) != 0 ? sdf.bounds.max.x : sdf.bounds.min.x,
                               (corner & 2) != 0 ? sdf.bounds.max.y : sdf.bounds.min.y,
                               (corner & 4) != 0 ? sdf.bounds.max.z : sdf.bounds.min.z);
        instance.world_bounds.add_point(math::vec3(local_to_world * math::vec4(local, 1.0f)));
    }
    // Conservative local-to-world distance scale: the smallest axis scale of the transform.
    const float sx = math::length(math::vec3(local_to_world[0]));
    const float sy = math::length(math::vec3(local_to_world[1]));
    const float sz = math::length(math::vec3(local_to_world[2]));
    instance.local_to_world_scale = math::min(sx, math::min(sy, sz));
    return instance;
}

} // namespace unravel::gi_reference
