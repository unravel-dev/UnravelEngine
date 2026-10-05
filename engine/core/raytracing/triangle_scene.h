#pragma once

#include <raytracing/backend.h>

#include <math/math.h>

#include <hpp/span.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace raytracing
{

namespace detail
{
class scene_backend;
} // namespace detail

/// A ray for the batched @ref triangle_scene::intersect.
struct ray
{
    math::vec3 origin{};
    ///< t is measured in units of its length.
    math::vec3 direction{};
    float t_near = 0.0f;
    float t_far = std::numeric_limits<float>::max();
};

/// The closest hit of a ray against a @ref triangle_scene.
struct ray_hit
{
    ///< Distance along the ray, in units of the direction's length.
    float t = std::numeric_limits<float>::max();
    ///< The triangle hit, indexed as in the indices the scene was built from.
    uint32_t triangle = std::numeric_limits<uint32_t>::max();
    bool is_hit = false;
};

/**
 * @brief A triangle mesh prepared for closest-hit ray queries on the CPU, by one of the @ref backend ray tracers.
 *
 * Triangles are two-sided for intersection; which side a ray hit is the caller's to decide, from @ref get_normal,
 * the normal of each triangle's winding. Of hits at the same distance, which one is reported is up to the backend.
 *
 * Queries on a built scene are thread-safe. A build runs on the calling thread: the bakes that use this parallelise
 * over many scenes rather than inside one.
 */
class triangle_scene
{
public:
    static constexpr uint32_t invalid_triangle = std::numeric_limits<uint32_t>::max();
    /// Rays a batch for @ref intersect should hold to fill the backends' packets: one of Embree's, two native ones.
    static constexpr uint32_t packet_size = 8;

    triangle_scene();
    ~triangle_scene();
    triangle_scene(triangle_scene&& other) noexcept;
    auto operator=(triangle_scene&& other) noexcept -> triangle_scene&;
    triangle_scene(const triangle_scene&) = delete;
    auto operator=(const triangle_scene&) -> triangle_scene& = delete;

    /**
     * @brief Builds the scene on @p kind, replacing any previous one.
     * @param indices Three per triangle, into @p positions.
     * @return false when this build lacks @p kind (@ref is_backend_available), the backend could not build it, or an
     *         index is past @p positions; queries then report no hits.
     */
    auto build(const std::vector<math::vec3>& positions,
               const std::vector<uint32_t>& indices,
               backend kind = get_default_backend()) -> bool;

    /// The backend of the last build.
    auto get_backend() const -> backend;

    /// The closest hit with t in [@p t_near, @p t_far], ignoring @p skip_triangle.
    auto intersect(const math::vec3& origin,
                   const math::vec3& direction,
                   float t_near,
                   uint32_t skip_triangle = invalid_triangle,
                   float t_far = std::numeric_limits<float>::max()) const -> ray_hit;

    /**
     * @brief The closest hit of each of @p rays into the same element of @p hits, which must be as long.
     *
     * Both backends trace the rays in packets (Embree @ref packet_size at a time, the native backend four), which
     * costs less than one by one when they are coherent, such as rays leaving one point.
     */
    void intersect(hpp::span<const ray> rays, hpp::span<ray_hit> hits) const;

    /**
     * @brief Every triangle that has a point within @p reach of @p region, and possibly others whose bounds come that
     *        close.
     *
     * Each is listed once, in no particular order. Collection stops as soon as @p out is full and another one turns up.
     * @return how many were written, or out.size() + 1 when there were more than @p out holds.
     */
    auto collect_triangles(const math::bbox& region, float reach, hpp::span<uint32_t> out) const -> uint32_t;

    /// The normalized normal of the triangle's winding, (0, 0, 1) for a degenerate one.
    auto get_normal(uint32_t triangle) const -> const math::vec3&;

private:
    std::unique_ptr<detail::scene_backend> implementation_;
    std::vector<math::vec3> normals_;
    backend backend_ = backend::native;
};

} // namespace raytracing
