#pragma once

#include <engine/rendering/gi/mesh_sdf_baker.h>

#include <math/math.h>
#include <raytracing/triangle_scene.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

/**
 * @file mesh_ray_tracing.h
 * @brief Offline ray casting over a mesh's triangles, as UE's Embree scene serves its mesh card build and
 *        its mesh distance field sign: a closest-hit caster on the raytracing library, UE's random stream and
 *        its stratified hemisphere directions.
 */

namespace unravel::mesh_ray
{

/// UE's FRandomStream: a 32-bit LCG whose fraction fills the float mantissa.
class random_stream
{
public:
    explicit random_stream(uint32_t seed)
        : seed_(seed)
    {
    }

    auto get_fraction() -> float
    {
        seed_ = seed_ * 196314165u + 907633515u;
        const uint32_t bits = 0x3F800000u | (seed_ >> 9);
        float result = 0.0f;
        std::memcpy(&result, &bits, sizeof(result));
        return result - 1.0f;
    }

private:
    uint32_t seed_ = 0;
};

/// Shirley-Chiu concentric map from the unit square to the cosine-free uniform hemisphere (+z).
inline auto sample_uniform_hemisphere(float u1, float u2) -> math::vec3
{
    const float x = u1 * 2.0f - 1.0f;
    const float y = u2 * 2.0f - 1.0f;
    if(x == 0.0f && y == 0.0f)
    {
        return math::vec3(0.0f);
    }
    constexpr float quarter_pi = 0.78539816f;
    constexpr float half_pi = 1.57079633f;
    float radius = 0.0f;
    float theta = 0.0f;
    if(std::fabs(x) > std::fabs(y))
    {
        radius = x;
        theta = quarter_pi * (y / x);
    }
    else
    {
        radius = y;
        theta = half_pi - quarter_pi * (x / y);
    }
    const float u = radius * std::cos(theta);
    const float v = radius * std::sin(theta);
    const float r2 = radius * radius;
    const float lift = std::sqrt(2.0f - r2);
    return math::vec3(u * lift, v * lift, 1.0f - r2);
}

/// UE GenerateStratifiedUniformHemisphereSamples: a floor(sqrt(request))^2 jittered grid over the +z
/// hemisphere, drawing two fractions per sample from @p stream.
inline auto generate_stratified_hemisphere_directions(uint32_t request, random_stream& stream)
    -> std::vector<math::vec3>
{
    const int dimension = int(std::sqrt(float(request)));
    std::vector<math::vec3> directions;
    directions.reserve(size_t(dimension * dimension));
    for(int ix = 0; ix < dimension; ++ix)
    {
        for(int iy = 0; iy < dimension; ++iy)
        {
            const float u1 = stream.get_fraction();
            const float u2 = stream.get_fraction();
            directions.push_back(
                sample_uniform_hemisphere((float(ix) + u1) / float(dimension), (float(iy) + u2) / float(dimension)));
        }
    }
    return directions;
}

using ray = raytracing::ray;
using ray_hit = raytracing::ray_hit;

/**
 * @brief Closest-hit ray caster over a triangle soup: the Embree scene of the UE build, on the default
 *        raytracing::backend.
 *
 * Triangles are two-sided for intersection; facing is decided by the caller from @ref get_normal, the outward normal
 * of the triangle's winding (UE: Embree's geometric normal). An enclosed-volume orientation fix would be wrong here:
 * an open sheet has no volume, and its sum only says which side of the origin the sheet lies on.
 */
class triangle_ray_caster
{
public:
    /// Rays the batched @ref intersect traces together.
    static constexpr uint32_t packet_size = raytracing::triangle_scene::packet_size;

    void build(const sdf_source_geometry& geometry)
    {
        scene_.build(geometry.positions, geometry.indices);
    }

    auto get_normal(uint32_t triangle) const -> const math::vec3&
    {
        return scene_.get_normal(triangle);
    }

    /// The closest hit with t in [@p t_near, @p t_far], t in units of @p direction's length, ignoring
    /// @p skip_triangle.
    auto intersect(const math::vec3& origin,
                   const math::vec3& direction,
                   float t_near,
                   uint32_t skip_triangle,
                   float t_far = std::numeric_limits<float>::max()) const -> ray_hit
    {
        return scene_.intersect(origin, direction, t_near, skip_triangle, t_far);
    }

    /// The closest hit of each of @p rays into @p hits, traced in packets of @ref packet_size.
    void intersect(hpp::span<const ray> rays, hpp::span<ray_hit> hits) const
    {
        scene_.intersect(rays, hits);
    }

    /// The triangles that may come within @p reach of @p region, each once; out.size() + 1 when @p out overflowed.
    auto collect_triangles(const math::bbox& region, float reach, hpp::span<uint32_t> out) const -> uint32_t
    {
        return scene_.collect_triangles(region, reach, out);
    }

private:
    raytracing::triangle_scene scene_;
};

} // namespace unravel::mesh_ray
