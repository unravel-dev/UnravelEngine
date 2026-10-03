#pragma once

#include <engine/rendering/gi/mesh_sdf_baker.h>

#include <math/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

/**
 * @file mesh_ray_tracing.h
 * @brief Offline ray casting over a mesh's triangles, as UE's Embree scene serves its mesh card build and
 *        its mesh distance field sign: a closest-hit BVH caster, UE's random stream and its stratified
 *        hemisphere directions.
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

struct ray_hit
{
    float t = std::numeric_limits<float>::max();
    uint32_t triangle = std::numeric_limits<uint32_t>::max();
    bool is_hit = false;
};

/**
 * @brief Closest-hit ray caster over a triangle soup (the Embree scene of the UE build).
 *
 * Triangles are two-sided for intersection; facing is decided by the caller from @ref get_normal, the
 * outward normal of the triangle's winding.
 */
class triangle_ray_caster
{
public:
    void build(const sdf_source_geometry& geometry)
    {
        positions_ = geometry.positions;
        indices_ = geometry.indices;
        const uint32_t triangle_count = uint32_t(indices_.size() / 3);
        normals_.resize(triangle_count);
        order_.resize(triangle_count);
        std::vector<math::vec3> centroids(triangle_count);
        // Outward normals follow the winding (UE: Embree's geometric normal). An enclosed-volume
        // orientation fix would be wrong here: an open sheet has no volume, and its sum only says
        // which side of the origin the sheet lies on.
        for(uint32_t t = 0; t < triangle_count; ++t)
        {
            const math::vec3& a = positions_[indices_[t * 3 + 0]];
            const math::vec3& b = positions_[indices_[t * 3 + 1]];
            const math::vec3& c = positions_[indices_[t * 3 + 2]];
            const math::vec3 n = math::cross(b - a, c - a);
            const float length = math::length(n);
            normals_[t] = length > 0.0f ? n / length : math::vec3(0.0f, 0.0f, 1.0f);
            centroids[t] = (a + b + c) / 3.0f;
            order_[t] = t;
        }
        nodes_.clear();
        nodes_.reserve(size_t(triangle_count) * 2u);
        if(triangle_count > 0)
        {
            build_node(0, triangle_count, centroids);
        }
    }

    auto get_normal(uint32_t triangle) const -> const math::vec3&
    {
        return normals_[triangle];
    }

    /// The closest hit with t in [@p t_near, @p t_far), t in units of @p direction's length.
    auto intersect(const math::vec3& origin,
                   const math::vec3& direction,
                   float t_near,
                   uint32_t skip_triangle,
                   float t_far = std::numeric_limits<float>::max()) const -> ray_hit
    {
        ray_hit best;
        best.t = t_far;
        if(nodes_.empty())
        {
            return best;
        }
        const math::vec3 inv_direction(safe_inverse(direction.x), safe_inverse(direction.y), safe_inverse(direction.z));
        std::array<uint32_t, 64> stack{};
        uint32_t stack_size = 0;
        stack[stack_size++] = 0;
        while(stack_size > 0)
        {
            const node& current = nodes_[stack[--stack_size]];
            if(!overlaps_ray(current.bounds, origin, inv_direction, t_near, best.t))
            {
                continue;
            }
            if(current.count > 0)
            {
                for(uint32_t i = current.first; i < current.first + current.count; ++i)
                {
                    const uint32_t triangle = order_[i];
                    if(triangle != skip_triangle)
                    {
                        intersect_triangle(triangle, origin, direction, t_near, best);
                    }
                }
                continue;
            }
            const uint32_t left = uint32_t(&current - nodes_.data()) + 1u;
            if(stack_size + 2 <= stack.size())
            {
                stack[stack_size++] = current.first;
                stack[stack_size++] = left;
            }
        }
        return best;
    }

private:
    ///< BVH leaf size.
    static constexpr uint32_t leaf_triangles = 4;

    struct node
    {
        math::bbox bounds{};
        ///< Leaves: first entry in order_. Internal nodes: index of the right child (the left
        ///< child is the next node).
        uint32_t first = 0;
        ///< Triangle count for leaves, 0 for internal nodes.
        uint32_t count = 0;
    };

    static auto safe_inverse(float value) -> float
    {
        constexpr float tiny = 1e-20f;
        return 1.0f / (std::fabs(value) > tiny ? value : std::copysign(tiny, value));
    }

    static auto overlaps_ray(const math::bbox& bounds,
                             const math::vec3& origin,
                             const math::vec3& inv_direction,
                             float t_near,
                             float t_far) -> bool
    {
        const math::vec3 t0 = (bounds.min - origin) * inv_direction;
        const math::vec3 t1 = (bounds.max - origin) * inv_direction;
        const math::vec3 t_min = math::min(t0, t1);
        const math::vec3 t_max = math::max(t0, t1);
        const float enter = std::max(std::max(t_min.x, t_min.y), std::max(t_min.z, t_near));
        const float exit = std::min(std::min(t_max.x, t_max.y), std::min(t_max.z, t_far));
        return enter <= exit;
    }

    void intersect_triangle(uint32_t triangle,
                            const math::vec3& origin,
                            const math::vec3& direction,
                            float t_near,
                            ray_hit& best) const
    {
        // Moller-Trumbore, both faces.
        const math::vec3& a = positions_[indices_[triangle * 3 + 0]];
        const math::vec3& b = positions_[indices_[triangle * 3 + 1]];
        const math::vec3& c = positions_[indices_[triangle * 3 + 2]];
        const math::vec3 edge1 = b - a;
        const math::vec3 edge2 = c - a;
        const math::vec3 p = math::cross(direction, edge2);
        const float determinant = math::dot(edge1, p);
        if(std::fabs(determinant) < 1e-12f)
        {
            return;
        }
        const float inv_determinant = 1.0f / determinant;
        const math::vec3 s = origin - a;
        const float u = math::dot(s, p) * inv_determinant;
        if(u < 0.0f || u > 1.0f)
        {
            return;
        }
        const math::vec3 q = math::cross(s, edge1);
        const float v = math::dot(direction, q) * inv_determinant;
        if(v < 0.0f || u + v > 1.0f)
        {
            return;
        }
        const float t = math::dot(edge2, q) * inv_determinant;
        if(t >= t_near && t < best.t)
        {
            best.t = t;
            best.triangle = triangle;
            best.is_hit = true;
        }
    }

    auto build_node(uint32_t begin, uint32_t end, const std::vector<math::vec3>& centroids) -> uint32_t
    {
        const uint32_t index = uint32_t(nodes_.size());
        nodes_.emplace_back();
        math::bbox bounds;
        math::bbox centroid_bounds;
        for(uint32_t i = begin; i < end; ++i)
        {
            const uint32_t triangle = order_[i];
            bounds.add_point(positions_[indices_[triangle * 3 + 0]]);
            bounds.add_point(positions_[indices_[triangle * 3 + 1]]);
            bounds.add_point(positions_[indices_[triangle * 3 + 2]]);
            centroid_bounds.add_point(centroids[triangle]);
        }
        nodes_[index].bounds = bounds;
        const math::vec3 spread = centroid_bounds.max - centroid_bounds.min;
        const int axis = spread.x > spread.y ? (spread.x > spread.z ? 0 : 2) : (spread.y > spread.z ? 1 : 2);
        if(end - begin <= leaf_triangles || spread[axis] <= 0.0f)
        {
            nodes_[index].first = begin;
            nodes_[index].count = end - begin;
            return index;
        }
        const uint32_t middle = begin + (end - begin) / 2;
        std::nth_element(order_.begin() + begin,
                         order_.begin() + middle,
                         order_.begin() + end,
                         [&](uint32_t lhs, uint32_t rhs)
                         {
                             return centroids[lhs][axis] < centroids[rhs][axis];
                         });
        build_node(begin, middle, centroids);
        const uint32_t right = build_node(middle, end, centroids);
        nodes_[index].first = right;
        nodes_[index].count = 0;
        return index;
    }

    std::vector<math::vec3> positions_;
    std::vector<uint32_t> indices_;
    std::vector<math::vec3> normals_;
    std::vector<uint32_t> order_;
    std::vector<node> nodes_;
};

} // namespace unravel::mesh_ray
