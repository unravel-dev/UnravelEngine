#pragma once

#include <raytracing/triangle_scene.h>

#include <memory>

namespace raytracing::detail
{

/**
 * @brief A ray tracer behind @ref triangle_scene.
 *
 * The scene keeps what every backend shares (the winding normals, the answers of an empty scene) and hands each query
 * to its backend, which must answer it exactly as @ref triangle_scene documents.
 */
class scene_backend
{
public:
    virtual ~scene_backend() = default;

    /// Builds over the triangles @p indices make of @p positions. False when the backend cannot.
    virtual auto build(const std::vector<math::vec3>& positions, const std::vector<uint32_t>& indices) -> bool = 0;

    /// @ref triangle_scene::intersect for one ray.
    virtual auto intersect(const math::vec3& origin,
                           const math::vec3& direction,
                           float t_near,
                           uint32_t skip_triangle,
                           float t_far) const -> ray_hit = 0;

    /// @ref triangle_scene::intersect for a batch.
    virtual void intersect(hpp::span<const ray> rays, hpp::span<ray_hit> hits) const = 0;

    /// @ref triangle_scene::collect_triangles.
    virtual auto collect_triangles(const math::bbox& region, float reach, hpp::span<uint32_t> out) const
        -> uint32_t = 0;
};

#if defined(RAYTRACING_WITH_EMBREE)
auto make_embree_scene() -> std::unique_ptr<scene_backend>;
#endif
auto make_native_scene() -> std::unique_ptr<scene_backend>;

} // namespace raytracing::detail
