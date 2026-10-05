/*
 * Validation suite for the raytracing library's backends (engine/core/raytracing): the engine's own BVH on the math
 * library, and Embree in a build that has it. Every test runs on each backend the build has.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "raytracing backends"
 *
 * A bake must be able to run on any backend, so each must answer every query alike:
 *   - the closest hits a brute-force double-precision reference finds, at the same distances, on closed, overlapping
 *     and far-from-origin geometry;
 *   - no ray slips between two triangles that share an edge or a corner;
 *   - the skipped triangle and the t range are honoured;
 *   - a batch answers as its rays do one by one;
 *   - a region query lists every triangle whose bounds come within reach, each once.
 * The last test prints what each backend costs per ray, for comparing them.
 */

#include "../tests.h"

#include <raytracing/triangle_scene.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

/// The backends this build has: always the native one, Embree when it is part of the build.
auto get_backends() -> std::vector<raytracing::backend>
{
    std::vector<raytracing::backend> available;
    for(const raytracing::backend kind : {raytracing::backend::embree, raytracing::backend::native})
    {
        if(raytracing::is_backend_available(kind))
        {
            available.push_back(kind);
        }
    }
    return available;
}

auto get_name(raytracing::backend value) -> std::string
{
    return std::string(raytracing::to_string(value));
}

/// A triangle soup and its bounds.
struct mesh
{
    std::vector<math::vec3> positions;
    std::vector<uint32_t> indices;
    math::vec3 min{std::numeric_limits<float>::max()};
    math::vec3 max{-std::numeric_limits<float>::max()};

    void add_triangle(const math::vec3& a, const math::vec3& b, const math::vec3& c)
    {
        for(const math::vec3& corner : {a, b, c})
        {
            indices.push_back(uint32_t(positions.size()));
            positions.push_back(corner);
            min = math::min(min, corner);
            max = math::max(max, corner);
        }
    }

    auto get_extent() const -> float
    {
        const math::vec3 size = max - min;
        return math::max(size.x, math::max(size.y, size.z));
    }

    /// The largest coordinate magnitude, which sets how finely float positions here can resolve anything.
    auto get_coordinate_scale() const -> float
    {
        const math::vec3 extreme = math::max(math::abs(min), math::abs(max));
        return math::max(1.0f, math::max(extreme.x, math::max(extreme.y, extreme.z)));
    }
};

/// A closed UV sphere with shared vertices, wound outward.
auto make_sphere(float radius, uint32_t rings, uint32_t sectors, const math::vec3& center) -> mesh
{
    constexpr float pi = 3.14159265358979323846f;
    mesh result;
    for(uint32_t r = 0; r <= rings; ++r)
    {
        const float theta = pi * float(r) / float(rings);
        for(uint32_t s = 0; s <= sectors; ++s)
        {
            const float phi = 2.0f * pi * float(s) / float(sectors);
            const math::vec3 position =
                center +
                radius * math::vec3(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            result.positions.push_back(position);
            result.min = math::min(result.min, position);
            result.max = math::max(result.max, position);
        }
    }
    const uint32_t stride = sectors + 1;
    for(uint32_t r = 0; r < rings; ++r)
    {
        for(uint32_t s = 0; s < sectors; ++s)
        {
            const uint32_t i0 = r * stride + s;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + stride + 1;
            const uint32_t i3 = i0 + stride;
            result.indices.insert(result.indices.end(), {i0, i2, i3, i0, i1, i2});
        }
    }
    return result;
}

/// Randomly placed, sized and oriented triangles, overlapping and crossing each other.
auto make_soup(uint32_t count, float extent, uint32_t seed) -> mesh
{
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> position(-extent, extent);
    std::uniform_real_distribution<float> offset(-0.15f * extent, 0.15f * extent);
    mesh result;
    for(uint32_t i = 0; i < count; ++i)
    {
        const math::vec3 a(position(random), position(random), position(random));
        const math::vec3 b = a + math::vec3(offset(random), offset(random), offset(random));
        const math::vec3 c = a + math::vec3(offset(random), offset(random), offset(random));
        result.add_triangle(a, b, c);
    }
    return result;
}

/// A flat grid of @p cells x @p cells quads at y = 0 from @p origin, two triangles each, sharing vertices.
auto make_grid(uint32_t cells, float cell_size, const math::vec3& origin) -> mesh
{
    mesh result;
    for(uint32_t z = 0; z <= cells; ++z)
    {
        for(uint32_t x = 0; x <= cells; ++x)
        {
            const math::vec3 position = origin + math::vec3(float(x) * cell_size, 0.0f, float(z) * cell_size);
            result.positions.push_back(position);
            result.min = math::min(result.min, position);
            result.max = math::max(result.max, position);
        }
    }
    const uint32_t stride = cells + 1;
    for(uint32_t z = 0; z < cells; ++z)
    {
        for(uint32_t x = 0; x < cells; ++x)
        {
            const uint32_t i0 = z * stride + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + stride + 1;
            const uint32_t i3 = i0 + stride;
            // Alternate the diagonal, so edges run three ways through the grid.
            if((x + z) % 2 == 0)
            {
                result.indices.insert(result.indices.end(), {i0, i1, i2, i0, i2, i3});
            }
            else
            {
                result.indices.insert(result.indices.end(), {i0, i1, i3, i1, i2, i3});
            }
        }
    }
    return result;
}

auto merge(const mesh& first, const mesh& second) -> mesh
{
    mesh result = first;
    const uint32_t base = uint32_t(result.positions.size());
    result.positions.insert(result.positions.end(), second.positions.begin(), second.positions.end());
    for(const uint32_t index : second.indices)
    {
        result.indices.push_back(base + index);
    }
    result.min = math::min(first.min, second.min);
    result.max = math::max(first.max, second.max);
    return result;
}

auto make_random_direction(std::mt19937& random) -> math::vec3
{
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float z = 2.0f * unit(random) - 1.0f;
    const float phi = 6.28318530718f * unit(random);
    const float radius = std::sqrt(math::max(0.0f, 1.0f - z * z));
    return math::vec3(radius * std::cos(phi), radius * std::sin(phi), z);
}

/// Random rays starting in and around a mesh's bounds, each ending somewhere between a fifth of the mesh and twice it.
auto make_random_rays(const mesh& geometry, uint32_t count, uint32_t seed) -> std::vector<raytracing::ray>
{
    std::mt19937 random(seed);
    const math::vec3 center = 0.5f * (geometry.min + geometry.max);
    // Flat geometry still gets a slab of origins on both sides, not a sheet of origins on the surface itself.
    const math::vec3 half = 0.75f * (geometry.max - geometry.min) + math::vec3(0.1f * geometry.get_extent());
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> length(0.2f, 2.0f);
    std::vector<raytracing::ray> rays(count);
    for(raytracing::ray& ray : rays)
    {
        ray.origin = center + half * math::vec3(unit(random), unit(random), unit(random));
        ray.direction = make_random_direction(random);
        ray.t_near = 0.0f;
        ray.t_far = length(random) * geometry.get_extent();
    }
    return rays;
}

/// Compares two backends' answers to the same rays.
struct hit_comparison
{
    uint32_t rays = 0;
    uint32_t hits = 0;
    uint32_t hit_mismatches = 0;
    ///< Of the hit mismatches: the hit lies within rounding distance of the ray's ends or of its triangle's edges.
    uint32_t rounding_mismatches = 0;
    uint32_t distance_mismatches = 0;
    uint32_t other_triangle_at_same_distance = 0;
    float worst_distance_difference = 0.0f;
};

/// Distance from @p point to the line through @p start and @p end.
auto get_distance_to_line(const math::dvec3& point, const math::dvec3& start, const math::dvec3& end) -> double
{
    const math::dvec3 edge = end - start;
    const double length = math::length(edge);
    return length > 0.0 ? math::length(math::cross(edge, point - start)) / length : math::length(point - start);
}

/**
 * @brief Whether float rounding alone can decide if @p ray hits where @p hit says: the hit lies within @p tolerance of
 *        the ray's start or end, or of an edge of the triangle hit.
 */
auto is_decided_by_rounding(const mesh& geometry,
                            const raytracing::ray& ray,
                            const raytracing::ray_hit& hit,
                            float tolerance) -> bool
{
    if(hit.t - ray.t_near < tolerance || ray.t_far - hit.t < tolerance)
    {
        return true;
    }
    const math::dvec3 point = math::dvec3(ray.origin) + double(hit.t) * math::dvec3(ray.direction);
    const math::dvec3 a(geometry.positions[geometry.indices[hit.triangle * 3 + 0]]);
    const math::dvec3 b(geometry.positions[geometry.indices[hit.triangle * 3 + 1]]);
    const math::dvec3 c(geometry.positions[geometry.indices[hit.triangle * 3 + 2]]);
    const double nearest_edge = std::min(
        {get_distance_to_line(point, a, b), get_distance_to_line(point, b, c), get_distance_to_line(point, c, a)});
    return nearest_edge < double(tolerance);
}

void compare_hits(const mesh& geometry,
                  const raytracing::ray& ray,
                  const raytracing::ray_hit& a,
                  const raytracing::ray_hit& b,
                  float tolerance,
                  hit_comparison& out)
{
    ++out.rays;
    if(a.is_hit != b.is_hit)
    {
        ++out.hit_mismatches;
        out.rounding_mismatches += is_decided_by_rounding(geometry, ray, a.is_hit ? a : b, tolerance) ? 1u : 0u;
        return;
    }
    if(!a.is_hit)
    {
        return;
    }
    ++out.hits;
    const float difference = std::fabs(a.t - b.t);
    out.worst_distance_difference = math::max(out.worst_distance_difference, difference);
    if(difference > tolerance)
    {
        ++out.distance_mismatches;
        return;
    }
    out.other_triangle_at_same_distance += a.triangle != b.triangle ? 1u : 0u;
}

/// The closest hit by testing every triangle in double precision (Moeller-Trumbore): what the backends are held to.
auto intersect_by_brute_force(const mesh& geometry, const raytracing::ray& ray) -> raytracing::ray_hit
{
    const math::dvec3 origin(ray.origin);
    const math::dvec3 direction(ray.direction);
    double best_t = double(ray.t_far);
    raytracing::ray_hit best;
    best.t = ray.t_far;
    for(uint32_t triangle = 0; triangle < uint32_t(geometry.indices.size() / 3); ++triangle)
    {
        const math::dvec3 a(geometry.positions[geometry.indices[triangle * 3 + 0]]);
        const math::dvec3 edge_ab = math::dvec3(geometry.positions[geometry.indices[triangle * 3 + 1]]) - a;
        const math::dvec3 edge_ac = math::dvec3(geometry.positions[geometry.indices[triangle * 3 + 2]]) - a;
        const math::dvec3 p = math::cross(direction, edge_ac);
        const double determinant = math::dot(edge_ab, p);
        if(determinant == 0.0)
        {
            continue;
        }
        const math::dvec3 to_origin = origin - a;
        const math::dvec3 q = math::cross(to_origin, edge_ab);
        const double u = math::dot(to_origin, p) / determinant;
        const double v = math::dot(direction, q) / determinant;
        const double distance = math::dot(edge_ac, q) / determinant;
        if(u < 0.0 || v < 0.0 || u + v > 1.0 || distance < double(ray.t_near) || distance > best_t)
        {
            continue;
        }
        best_t = distance;
        best.t = float(distance);
        best.triangle = triangle;
        best.is_hit = true;
    }
    return best;
}

void test_closest_hits_match_brute_force()
{
    std::printf("test_closest_hits_match_brute_force\n");
    struct fixture
    {
        const char* name = "";
        mesh geometry;
    };
    const std::array<fixture, 4> fixtures = {{
        {"sphere", make_sphere(1.0f, 40, 60, math::vec3(0.0f))},
        {"overlapping soup", make_soup(3000, 1.0f, 7u)},
        {"sphere far from the origin", make_sphere(2.0f, 40, 60, math::vec3(1500.0f, -800.0f, 1200.0f))},
        {"grid far from the origin", make_grid(48, 0.37f, math::vec3(-900.0f, 310.0f, 2400.0f))},
    }};
    for(const fixture& f : fixtures)
    {
        const std::vector<raytracing::ray> rays = make_random_rays(f.geometry, 8000, 11u);
        std::vector<raytracing::ray_hit> expected(rays.size());
        std::transform(rays.begin(),
                       rays.end(),
                       expected.begin(),
                       [&](const raytracing::ray& ray)
                       {
                           return intersect_by_brute_force(f.geometry, ray);
                       });
        // Distances agree to a few float steps of the coordinates involved.
        const float tolerance = 1e-5f * f.geometry.get_coordinate_scale();
        constexpr uint32_t no_skip = raytracing::triangle_scene::invalid_triangle;
        for(const raytracing::backend kind : get_backends())
        {
            const std::string name = std::string(f.name) + ", " + get_name(kind);
            raytracing::triangle_scene scene;
            check(scene.build(f.geometry.positions, f.geometry.indices, kind), name + ": the scene builds");
            hit_comparison comparison;
            for(size_t i = 0; i < rays.size(); ++i)
            {
                const raytracing::ray& ray = rays[i];
                compare_hits(f.geometry,
                             ray,
                             expected[i],
                             scene.intersect(ray.origin, ray.direction, ray.t_near, no_skip, ray.t_far),
                             tolerance,
                             comparison);
            }
            std::printf("  %s: %u rays, %u hits, hit/miss differs %u (%u within rounding of an edge or a ray end), "
                        "distance differs %u (worst %.2e), other triangle at the same distance %u\n",
                        name.c_str(),
                        comparison.rays,
                        comparison.hits,
                        comparison.hit_mismatches,
                        comparison.rounding_mismatches,
                        comparison.distance_mismatches,
                        double(comparison.worst_distance_difference),
                        comparison.other_triangle_at_same_distance);
            check(comparison.hits > comparison.rays / 10, name + ": the rays meaningfully hit");
            check(comparison.hit_mismatches == comparison.rounding_mismatches,
                  name + ": every hit and miss that rounding does not decide matches the reference");
            check(comparison.distance_mismatches == 0, name + ": every distance matches the reference");
        }
    }
}

void test_shared_edges_are_watertight()
{
    std::printf("test_shared_edges_are_watertight\n");
    // Corners on multiples of a quarter are exact in float, and so are rays aimed at them along integer directions:
    // each ray passes exactly through a grid vertex or the middle of a grid edge.
    constexpr uint32_t cells = 12;
    constexpr float cell = 0.25f;
    const math::vec3 origin(-1.5f, 0.0f, -1.5f);
    const mesh grid = make_grid(cells, cell, origin);
    const std::array<math::vec3, 5> directions = {math::vec3(0.0f, -1.0f, 0.0f),
                                                  math::vec3(1.0f, -1.0f, 0.0f),
                                                  math::vec3(0.0f, -1.0f, -1.0f),
                                                  math::vec3(-1.0f, -2.0f, 1.0f),
                                                  math::vec3(2.0f, -1.0f, 1.0f)};
    std::vector<math::vec3> targets;
    for(uint32_t z = 1; z < cells * 2; ++z)
    {
        for(uint32_t x = 1; x < cells * 2; ++x)
        {
            // Every half-cell point: vertices, edge middles, and quad centres on the diagonals.
            targets.push_back(origin + math::vec3(float(x) * 0.5f * cell, 0.0f, float(z) * 0.5f * cell));
        }
    }
    for(const raytracing::backend kind : get_backends())
    {
        raytracing::triangle_scene scene;
        check(scene.build(grid.positions, grid.indices, kind), get_name(kind) + ": the grid builds");
        uint32_t rays = 0;
        uint32_t misses = 0;
        uint32_t wrong_distance = 0;
        for(const math::vec3& direction : directions)
        {
            for(const math::vec3& target : targets)
            {
                // Starts one direction length above the target, so the hit is at t = 1 exactly.
                const raytracing::ray_hit hit = scene.intersect(target - direction, direction, 0.0f);
                ++rays;
                misses += hit.is_hit ? 0u : 1u;
                wrong_distance += hit.is_hit && std::fabs(hit.t - 1.0f) > 1e-6f ? 1u : 0u;
            }
        }
        std::printf("  %s: %u rays through vertices and edges, %u missed, %u at the wrong distance\n",
                    get_name(kind).c_str(),
                    rays,
                    misses,
                    wrong_distance);
        check(misses == 0, get_name(kind) + ": no ray slips between triangles sharing an edge or a corner");
        check(wrong_distance == 0, get_name(kind) + ": every such ray hits at its target");
    }
}

void test_skip_and_range()
{
    std::printf("test_skip_and_range\n");
    // Two unit quads, at y = 1 and y = 0, under a ray coming straight down from y = 2 away from their diagonals.
    mesh layers;
    for(const float y : {1.0f, 0.0f})
    {
        layers.add_triangle(math::vec3(0.0f, y, 0.0f), math::vec3(1.0f, y, 0.0f), math::vec3(1.0f, y, 1.0f));
        layers.add_triangle(math::vec3(0.0f, y, 0.0f), math::vec3(1.0f, y, 1.0f), math::vec3(0.0f, y, 1.0f));
    }
    const math::vec3 origin(0.75f, 2.0f, 0.25f);
    const math::vec3 down(0.0f, -1.0f, 0.0f);
    constexpr uint32_t no_skip = raytracing::triangle_scene::invalid_triangle;
    // Embree divides with a refined reciprocal, so its distances may be a float step off.
    constexpr float tolerance = 1e-6f;
    for(const raytracing::backend kind : get_backends())
    {
        const std::string name = get_name(kind);
        raytracing::triangle_scene scene;
        check(scene.build(layers.positions, layers.indices, kind), name + ": the layers build");
        const raytracing::ray_hit first = scene.intersect(origin, down, 0.0f);
        check(first.is_hit && first.triangle == 0 && std::fabs(first.t - 1.0f) <= tolerance,
              name + ": the upper layer is hit first");
        const raytracing::ray_hit skipped = scene.intersect(origin, down, 0.0f, first.triangle);
        check(skipped.is_hit && skipped.triangle == 2 && std::fabs(skipped.t - 2.0f) <= tolerance,
              name + ": skipping it reaches the lower layer");
        const raytracing::ray_hit short_ray = scene.intersect(origin, down, 0.0f, no_skip, 0.5f);
        check(!short_ray.is_hit && short_ray.t == 0.5f, name + ": a ray that ends before both misses");
        const raytracing::ray_hit late_start = scene.intersect(origin, down, 1.5f);
        check(late_start.is_hit && late_start.triangle == 2,
              name + ": a ray that starts past the upper layer hits the lower");
    }
}

void test_batches_answer_as_single_rays()
{
    std::printf("test_batches_answer_as_single_rays\n");
    const mesh geometry = merge(make_sphere(1.0f, 30, 40, math::vec3(0.0f)), make_soup(1500, 1.2f, 3u));
    const std::vector<raytracing::ray> rays = make_random_rays(geometry, 6000, 5u);
    for(const raytracing::backend kind : get_backends())
    {
        const std::string name = get_name(kind);
        raytracing::triangle_scene scene;
        check(scene.build(geometry.positions, geometry.indices, kind), name + ": the scene builds");
        std::vector<raytracing::ray_hit> batched(rays.size());
        // Batches of every size from one ray to past two packets, so full packets, partial ones and lone rays all run.
        size_t first = 0;
        size_t batch = 1;
        while(first < rays.size())
        {
            const size_t count = std::min(batch, rays.size() - first);
            scene.intersect(hpp::span<const raytracing::ray>(rays.data() + first, count),
                            hpp::span<raytracing::ray_hit>(batched.data() + first, count));
            first += count;
            batch = batch % (2 * raytracing::triangle_scene::packet_size + 3) + 1;
        }
        hit_comparison comparison;
        uint32_t different_triangle = 0;
        for(size_t i = 0; i < rays.size(); ++i)
        {
            const raytracing::ray& ray = rays[i];
            const raytracing::ray_hit single = scene.intersect(ray.origin,
                                                               ray.direction,
                                                               ray.t_near,
                                                               raytracing::triangle_scene::invalid_triangle,
                                                               ray.t_far);
            compare_hits(geometry, ray, single, batched[i], 1e-5f, comparison);
            const bool both_hit = single.is_hit && batched[i].is_hit;
            different_triangle += both_hit && single.triangle != batched[i].triangle ? 1u : 0u;
        }
        std::printf("  %s: %u rays, %u hits, hit/miss differs %u, distance differs %u, triangle differs %u\n",
                    name.c_str(),
                    comparison.rays,
                    comparison.hits,
                    comparison.hit_mismatches,
                    comparison.distance_mismatches,
                    different_triangle);
        check(comparison.hit_mismatches == 0 && comparison.distance_mismatches == 0,
              name + ": a batch finds the hits its rays find one by one");
    }
}

/// The triangles whose bounds come within @p reach of @p region, by brute force, sorted.
auto collect_by_bounds(const mesh& geometry, const math::bbox& region, float reach) -> std::vector<uint32_t>
{
    std::vector<uint32_t> result;
    for(uint32_t t = 0; t < uint32_t(geometry.indices.size() / 3); ++t)
    {
        const math::vec3& a = geometry.positions[geometry.indices[t * 3 + 0]];
        const math::vec3& b = geometry.positions[geometry.indices[t * 3 + 1]];
        const math::vec3& c = geometry.positions[geometry.indices[t * 3 + 2]];
        const math::vec3 minimum = math::min(a, math::min(b, c));
        const math::vec3 maximum = math::max(a, math::max(b, c));
        const math::vec3 gap = math::max(math::max(minimum - region.max, region.min - maximum), math::vec3(0.0f));
        if(math::dot(gap, gap) <= reach * reach)
        {
            result.push_back(t);
        }
    }
    return result;
}

/// The triangles with a corner within @p reach of @p region: a subset of the ones with any point that close.
auto collect_by_corners(const mesh& geometry, const math::bbox& region, float reach) -> std::vector<uint32_t>
{
    std::vector<uint32_t> result;
    for(uint32_t t = 0; t < uint32_t(geometry.indices.size() / 3); ++t)
    {
        for(uint32_t corner = 0; corner < 3; ++corner)
        {
            const math::vec3& p = geometry.positions[geometry.indices[t * 3 + corner]];
            const math::vec3 gap = math::max(math::max(region.min - p, p - region.max), math::vec3(0.0f));
            if(math::dot(gap, gap) <= reach * reach)
            {
                result.push_back(t);
                break;
            }
        }
    }
    return result;
}

void test_region_queries_agree()
{
    std::printf("test_region_queries_agree\n");
    const mesh geometry = merge(make_sphere(1.0f, 30, 40, math::vec3(0.0f)), make_soup(1500, 1.2f, 9u));
    const std::vector<raytracing::backend> backends = get_backends();
    std::vector<raytracing::triangle_scene> scenes(backends.size());
    for(size_t b = 0; b < backends.size(); ++b)
    {
        check(scenes[b].build(geometry.positions, geometry.indices, backends[b]),
              get_name(backends[b]) + ": the scene builds");
    }
    std::mt19937 random(13u);
    std::uniform_real_distribution<float> unit(-1.2f, 1.2f);
    std::uniform_real_distribution<float> size(0.0f, 0.3f);
    std::uniform_real_distribution<float> reach_of(0.0f, 0.3f);
    std::vector<uint32_t> buffer(8192);
    // Per backend: queries that listed a triangle the bounds test rules out, missed a triangle with a corner within
    // reach, listed one twice, or did not report an overflow; and queries that listed fewer than the bounds test, which
    // the contract allows.
    struct tally
    {
        uint32_t outside_bounds = 0;
        uint32_t missed_corner = 0;
        uint32_t duplicates = 0;
        uint32_t overflow_wrong = 0;
        uint32_t fewer_than_bounds = 0;
        uint64_t found = 0;
    };
    std::vector<tally> tallies(backends.size());
    const auto contains = [](const std::vector<uint32_t>& set, const std::vector<uint32_t>& subset) -> bool
    {
        return std::includes(set.begin(), set.end(), subset.begin(), subset.end());
    };
    constexpr uint32_t queries = 300;
    for(uint32_t q = 0; q < queries; ++q)
    {
        const math::vec3 corner(unit(random), unit(random), unit(random));
        const math::bbox region(corner, corner + math::vec3(size(random), size(random), size(random)));
        const float reach = reach_of(random);
        const std::vector<uint32_t> expected = collect_by_bounds(geometry, region, reach);
        const std::vector<uint32_t> corners = collect_by_corners(geometry, region, reach);
        for(size_t b = 0; b < scenes.size(); ++b)
        {
            tally& counts = tallies[b];
            const uint32_t count = scenes[b].collect_triangles(region, reach, buffer);
            std::vector<uint32_t> listed(buffer.begin(), buffer.begin() + std::min<size_t>(count, buffer.size()));
            std::sort(listed.begin(), listed.end());
            counts.found += listed.size();
            counts.duplicates += std::adjacent_find(listed.begin(), listed.end()) != listed.end() ? 1u : 0u;
            counts.outside_bounds += contains(expected, listed) ? 0u : 1u;
            counts.missed_corner += contains(listed, corners) ? 0u : 1u;
            counts.fewer_than_bounds += listed.size() < expected.size() ? 1u : 0u;
            // One short of its own full list, a backend must report the overflow rather than a partial list.
            if(!listed.empty())
            {
                const uint32_t capacity = uint32_t(listed.size() - 1);
                const uint32_t short_count =
                    scenes[b].collect_triangles(region, reach, hpp::span<uint32_t>(buffer.data(), capacity));
                counts.overflow_wrong += short_count == capacity + 1 ? 0u : 1u;
            }
        }
    }
    for(size_t b = 0; b < scenes.size(); ++b)
    {
        const tally& counts = tallies[b];
        const std::string name = get_name(backends[b]);
        std::printf("  %s: %u queries, %llu triangles found; listed one outside the bounds test %u, missed a corner "
                    "within reach %u, duplicates %u, wrong overflow %u; fewer than the bounds test %u\n",
                    name.c_str(),
                    queries,
                    (unsigned long long)counts.found,
                    counts.outside_bounds,
                    counts.missed_corner,
                    counts.duplicates,
                    counts.overflow_wrong,
                    counts.fewer_than_bounds);
        check(counts.found > 1000, name + ": the queries meaningfully find triangles");
        check(counts.outside_bounds == 0, name + ": lists no triangle whose bounds stay out of reach");
        check(counts.missed_corner == 0, name + ": lists every triangle with a corner within reach");
        check(counts.duplicates == 0, name + ": lists no triangle twice");
        check(counts.overflow_wrong == 0, name + ": a list that does not fit is reported as overflowing");
    }
}

using clock_type = std::chrono::steady_clock;

auto get_seconds_since(clock_type::time_point start) -> double
{
    return std::chrono::duration<double>(clock_type::now() - start).count();
}

void measure_throughput(const char* name, const mesh& geometry, const std::vector<raytracing::ray>& rays);

void test_backend_throughput()
{
    std::printf("test_backend_throughput\n");
    // A dense closed surface with clutter around it, and the rays a sign vote casts: 98 directions from each of many
    // points, over a short reach.
    constexpr uint32_t directions_per_point = 98;
    std::mt19937 random(17u);
    std::vector<math::vec3> directions(directions_per_point);
    for(math::vec3& direction : directions)
    {
        direction = make_random_direction(random);
    }
    // A bake's case: a mesh of a submesh's size, and votes from points near its surface over a short reach.
    {
        constexpr uint32_t floor_cells = 16;
        constexpr float floor_cell = 0.2f;
        const math::vec3 floor_origin(-1.6f, -1.1f, -1.6f);
        const mesh part =
            merge(make_sphere(1.0f, 24, 32, math::vec3(0.0f)), make_grid(floor_cells, floor_cell, floor_origin));
        std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
        std::uniform_real_distribution<float> across(0.0f, float(floor_cells) * floor_cell);
        std::uniform_real_distribution<float> offset(-0.08f, 0.08f);
        std::vector<raytracing::ray> rays;
        for(uint32_t p = 0; p < 3000; ++p)
        {
            // Near the sphere or near the floor, as band voxels are.
            const math::vec3 near_sphere = math::normalize(math::vec3(unit(random), unit(random), unit(random)));
            const math::vec3 near_floor = floor_origin + math::vec3(across(random), 0.0f, across(random));
            const math::vec3 origin =
                (p % 2 == 0 ? near_sphere : near_floor) + math::vec3(offset(random), offset(random), offset(random));
            for(const math::vec3& direction : directions)
            {
                rays.push_back({origin, direction, 0.0f, 0.3f});
            }
        }
        measure_throughput("a submesh, short rays from near its surface", part, rays);
    }
    // Dense, overlapping clutter, where the tree's quality decides.
    const mesh geometry = merge(make_sphere(1.0f, 200, 300, math::vec3(0.0f)), make_soup(20000, 1.5f, 21u));
    std::uniform_real_distribution<float> unit(-1.3f, 1.3f);
    std::vector<raytracing::ray> rays;
    for(uint32_t p = 0; p < 3000; ++p)
    {
        const math::vec3 origin(unit(random), unit(random), unit(random));
        for(const math::vec3& direction : directions)
        {
            rays.push_back({origin, direction, 0.0f, 0.2f});
        }
    }
    measure_throughput("dense overlapping clutter", geometry, rays);
}

/// Builds @p geometry on each backend and times @p rays one by one and in batches; the backends must hit alike.
void measure_throughput(const char* name, const mesh& geometry, const std::vector<raytracing::ray>& rays)
{
    std::printf("  %s:\n", name);
    const std::vector<raytracing::backend> backends = get_backends();
    std::vector<uint64_t> hit_counts(backends.size());
    for(size_t b = 0; b < backends.size(); ++b)
    {
        const raytracing::backend kind = backends[b];
        raytracing::triangle_scene scene;
        const clock_type::time_point build_start = clock_type::now();
        check(scene.build(geometry.positions, geometry.indices, kind), get_name(kind) + ": the scene builds");
        const double build_seconds = get_seconds_since(build_start);
        uint64_t single_hits = 0;
        const clock_type::time_point single_start = clock_type::now();
        for(const raytracing::ray& ray : rays)
        {
            const raytracing::ray_hit hit = scene.intersect(ray.origin,
                                                            ray.direction,
                                                            ray.t_near,
                                                            raytracing::triangle_scene::invalid_triangle,
                                                            ray.t_far);
            single_hits += hit.is_hit ? 1u : 0u;
        }
        const double single_seconds = get_seconds_since(single_start);
        std::vector<raytracing::ray_hit> hits(rays.size());
        const clock_type::time_point batch_start = clock_type::now();
        for(size_t first = 0; first < rays.size(); first += raytracing::triangle_scene::packet_size)
        {
            const size_t count = std::min<size_t>(raytracing::triangle_scene::packet_size, rays.size() - first);
            scene.intersect(hpp::span<const raytracing::ray>(rays.data() + first, count),
                            hpp::span<raytracing::ray_hit>(hits.data() + first, count));
        }
        const double batch_seconds = get_seconds_since(batch_start);
        hit_counts[b] = single_hits;
        std::printf("    %s: %zu triangles built in %.2f ms; %zu rays, %llu hit: %.1f ns per ray one by one, %.1f ns "
                    "per ray in batches of %u\n",
                    get_name(kind).c_str(),
                    geometry.indices.size() / 3,
                    build_seconds * 1e3,
                    rays.size(),
                    (unsigned long long)single_hits,
                    single_seconds * 1e9 / double(rays.size()),
                    batch_seconds * 1e9 / double(rays.size()),
                    raytracing::triangle_scene::packet_size);
    }
    check(hit_counts[0] > rays.size() / 20, std::string(name) + ": the rays meaningfully hit");
    check(std::adjacent_find(hit_counts.begin(), hit_counts.end(), std::not_equal_to<>()) == hit_counts.end(),
          std::string(name) + ": every backend hits with the same rays");
}

auto run_raytracing_backends_suite(rtti::context& ctx) -> int
{
    (void)ctx;
    g_checks = 0;
    g_failures = 0;
    test_closest_hits_match_brute_force();
    test_shared_edges_are_watertight();
    test_skip_and_range();
    test_batches_answer_as_single_rays();
    test_region_queries_agree();
    test_backend_throughput();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("raytracing backends", run_raytracing_backends_suite)
