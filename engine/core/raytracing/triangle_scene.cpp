#include "triangle_scene.h"

#include "detail/scene_backend.h"

#include <algorithm>

namespace raytracing
{
namespace
{

/// A backend of @p kind, which this build has.
auto make_backend(backend kind) -> std::unique_ptr<detail::scene_backend>
{
#if defined(RAYTRACING_WITH_EMBREE)
    if(kind == backend::embree)
    {
        return detail::make_embree_scene();
    }
#endif
    return detail::make_native_scene();
}

} // namespace

triangle_scene::triangle_scene() = default;
triangle_scene::~triangle_scene() = default;
triangle_scene::triangle_scene(triangle_scene&& other) noexcept = default;
auto triangle_scene::operator=(triangle_scene&& other) noexcept -> triangle_scene& = default;

auto triangle_scene::build(const std::vector<math::vec3>& positions, const std::vector<uint32_t>& indices, backend kind)
    -> bool
{
    implementation_.reset();
    normals_.clear();
    backend_ = kind;
    const size_t triangle_count = indices.size() / 3;
    const bool has_bad_index = std::any_of(indices.begin(),
                                           indices.end(),
                                           [&](uint32_t index)
                                           {
                                               return index >= positions.size();
                                           });
    if(triangle_count == 0 || has_bad_index || !is_backend_available(kind))
    {
        return false;
    }
    normals_.resize(triangle_count);
    for(size_t t = 0; t < triangle_count; ++t)
    {
        const math::vec3& a = positions[indices[t * 3 + 0]];
        const math::vec3& b = positions[indices[t * 3 + 1]];
        const math::vec3& c = positions[indices[t * 3 + 2]];
        const math::vec3 n = math::cross(b - a, c - a);
        const float length = math::length(n);
        normals_[t] = length > 0.0f ? n / length : math::vec3(0.0f, 0.0f, 1.0f);
    }
    std::unique_ptr<detail::scene_backend> implementation = make_backend(kind);
    if(!implementation->build(positions, indices))
    {
        return false;
    }
    implementation_ = std::move(implementation);
    return true;
}

auto triangle_scene::get_backend() const -> backend
{
    return backend_;
}

auto triangle_scene::intersect(const math::vec3& origin,
                               const math::vec3& direction,
                               float t_near,
                               uint32_t skip_triangle,
                               float t_far) const -> ray_hit
{
    if(implementation_ == nullptr)
    {
        ray_hit miss;
        miss.t = t_far;
        return miss;
    }
    return implementation_->intersect(origin, direction, t_near, skip_triangle, t_far);
}

void triangle_scene::intersect(hpp::span<const ray> rays, hpp::span<ray_hit> hits) const
{
    if(implementation_ == nullptr)
    {
        for(size_t i = 0; i < rays.size(); ++i)
        {
            hits[i] = {};
            hits[i].t = rays[i].t_far;
        }
        return;
    }
    implementation_->intersect(rays, hits);
}

auto triangle_scene::collect_triangles(const math::bbox& region, float reach, hpp::span<uint32_t> out) const -> uint32_t
{
    return implementation_ != nullptr ? implementation_->collect_triangles(region, reach, out) : 0u;
}

auto triangle_scene::get_normal(uint32_t triangle) const -> const math::vec3&
{
    return normals_[triangle];
}

} // namespace raytracing
