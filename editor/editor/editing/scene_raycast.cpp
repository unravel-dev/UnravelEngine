#include "scene_raycast.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>

#include <algorithm>
#include <unordered_map>

namespace unravel
{
namespace
{

/// A ray this close to running along a triangle's plane (the determinant against its bound) misses it;
/// rounding would otherwise report a hit anywhere on a flat panel the ray grazes.
constexpr float k_parallel_tolerance = 1e-5f;
/// Grows a submesh's bounds before the slab test, so a flat submesh (a plane) is not missed by rounding.
constexpr float k_bounds_padding = 1e-4f;

/// A mesh's vertex positions, decoded once per raycast for every model drawing that mesh.
using position_cache = std::unordered_map<const mesh*, std::vector<math::vec3>>;

struct triangle_hit
{
    float distance{std::numeric_limits<float>::max()};
    math::vec3 normal{};
    bool found{};
};

auto decode_positions(mesh& source) -> std::vector<math::vec3>
{
    std::vector<math::vec3> positions;
    const auto* vertices = source.get_system_vb();
    const auto& format = source.get_vertex_format();
    if(vertices == nullptr || !format.has(bgfx::Attrib::Position))
    {
        return positions;
    }
    positions.resize(source.get_vertex_count());
    for(uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i)
    {
        float unpacked[4]{};
        bgfx::vertexUnpack(unpacked, bgfx::Attrib::Position, format, vertices, i);
        positions[i] = math::vec3(unpacked[0], unpacked[1], unpacked[2]);
    }
    return positions;
}

auto get_positions(mesh& source, position_cache& cache) -> const std::vector<math::vec3>&
{
    auto it = cache.find(&source);
    if(it == cache.end())
    {
        it = cache.emplace(&source, decode_positions(source)).first;
    }
    return it->second;
}

/// Moller-Trumbore, both faces: the distance along @p direction in units of its length, or below zero.
auto intersect_triangle(const math::vec3& origin,
                        const math::vec3& direction,
                        const math::vec3& a,
                        const math::vec3& b,
                        const math::vec3& c) -> float
{
    const math::vec3 edge1 = b - a;
    const math::vec3 edge2 = c - a;
    const math::vec3 p = math::cross(direction, edge2);
    const float determinant = math::dot(edge1, p);
    if(math::abs(determinant) <= k_parallel_tolerance * math::length(edge1) * math::length(p))
    {
        return -1.0f;
    }
    const float inverse_determinant = 1.0f / determinant;
    const math::vec3 to_origin = origin - a;
    const float u = math::dot(to_origin, p) * inverse_determinant;
    if(u < 0.0f || u > 1.0f)
    {
        return -1.0f;
    }
    const math::vec3 q = math::cross(to_origin, edge1);
    const float v = math::dot(direction, q) * inverse_determinant;
    if(v < 0.0f || u + v > 1.0f)
    {
        return -1.0f;
    }
    return math::dot(edge2, q) * inverse_determinant;
}

/// The nearest triangle of one submesh the ray crosses within @p max_distance, all in the submesh's space.
auto intersect_submesh(const mesh::submesh& submesh,
                       const uint32_t* indices,
                       const std::vector<math::vec3>& positions,
                       const math::vec3& origin,
                       const math::vec3& direction,
                       float max_distance) -> triangle_hit
{
    triangle_hit best;
    best.distance = max_distance;
    const size_t first_index = static_cast<size_t>(submesh.face_start) * 3;
    const size_t end_index = first_index + static_cast<size_t>(submesh.face_count) * 3;
    for(size_t i = first_index; i < end_index; i += 3)
    {
        const auto ia = indices[i];
        const auto ib = indices[i + 1];
        const auto ic = indices[i + 2];
        if(ia >= positions.size() || ib >= positions.size() || ic >= positions.size())
        {
            continue;
        }
        const float distance = intersect_triangle(origin, direction, positions[ia], positions[ib], positions[ic]);
        if(distance >= 0.0f && distance < best.distance)
        {
            best.distance = distance;
            best.normal = math::cross(positions[ib] - positions[ia], positions[ic] - positions[ia]);
            best.found = true;
        }
    }
    return best;
}

/// The transforms a submesh is drawn at: each node transform mapped to it, else the model's own.
auto collect_placements(const submesh_pose_mat4& pose, uint32_t submesh_index, const math::mat4& world)
    -> std::vector<math::mat4>
{
    if(submesh_index >= pose.submesh_to_transform_indices.size() ||
       pose.submesh_to_transform_indices[submesh_index].empty())
    {
        return {world};
    }
    std::vector<math::mat4> placements;
    for(const auto& mapped : pose.submesh_to_transform_indices[submesh_index])
    {
        if(mapped.active && mapped.index < pose.transforms.size())
        {
            placements.push_back(pose.transforms[mapped.index]);
        }
    }
    return placements;
}

auto padded(const math::bbox& bounds) -> math::bbox
{
    return math::bbox(bounds.min - math::vec3(k_bounds_padding), bounds.max + math::vec3(k_bounds_padding));
}

/// The nearest surface of one model the ray crosses, or nothing.
auto raycast_model(entt::handle entity,
                   const transform_component& transform,
                   const model_component& model_comp,
                   const scene_ray& ray,
                   position_cache& cache) -> hpp::optional<scene_ray_hit>
{
    const auto mesh_handle = model_comp.get_model().get_lod(0);
    if(!mesh_handle.is_ready())
    {
        return {};
    }
    const auto source = mesh_handle.get();
    const auto* indices = source ? source->get_system_ib(0) : nullptr;
    if(indices == nullptr)
    {
        return {};
    }
    const auto& pose = model_comp.get_submesh_transforms();
    const math::mat4& world = transform.get_transform_global().get_matrix();
    scene_ray_hit nearest;
    nearest.entity = entity;
    nearest.distance = ray.max_distance;
    bool found = false;
    for(uint32_t submesh_index = 0; submesh_index < static_cast<uint32_t>(source->get_submeshes_count()); ++submesh_index)
    {
        const auto* submesh = source->get_submesh(submesh_index);
        if(submesh == nullptr || submesh->face_start < 0)
        {
            continue;
        }
        for(const auto& placement : collect_placements(pose, submesh_index, world))
        {
            // The local ray keeps the world parameterization: t along it is a world distance.
            const math::mat4 to_local = glm::inverse(placement);
            const math::vec3 local_origin = math::vec3(to_local * math::vec4(ray.origin, 1.0f));
            const math::vec3 local_direction = math::vec3(to_local * math::vec4(ray.direction, 0.0f));
            float entry = 0.0f;
            if(!padded(submesh->bbox).intersect(local_origin, local_direction, entry, false) || entry > nearest.distance)
            {
                continue;
            }
            const auto hit = intersect_submesh(*submesh,
                                               indices,
                                               get_positions(*source, cache),
                                               local_origin,
                                               local_direction,
                                               nearest.distance);
            if(!hit.found)
            {
                continue;
            }
            const math::mat3 normal_matrix = glm::transpose(math::mat3(to_local));
            math::vec3 normal = math::normalize(normal_matrix * hit.normal);
            nearest.normal = math::dot(normal, ray.direction) > 0.0f ? -normal : normal;
            nearest.distance = hit.distance;
            nearest.submesh_index = submesh_index;
            found = true;
        }
    }
    if(!found)
    {
        return {};
    }
    nearest.point = ray.origin + ray.direction * nearest.distance;
    return nearest;
}

auto is_skipped(entt::handle entity, const std::vector<entt::handle>& skip) -> bool
{
    for(auto current = entity; current;)
    {
        if(std::find(skip.begin(), skip.end(), current) != skip.end())
        {
            return true;
        }
        const auto* transform = current.try_get<transform_component>();
        current = transform != nullptr ? transform->get_parent() : entt::handle{};
    }
    return false;
}

} // namespace

auto raycast_scene_models(scene& scn, const scene_ray& ray, const std::vector<entt::handle>& skip)
    -> std::vector<scene_ray_hit>
{
    std::vector<scene_ray_hit> hits;
    if(!scn.registry)
    {
        return hits;
    }
    position_cache cache;
    scn.registry->view<transform_component, model_component>().each(
        [&](auto entity, const transform_component& transform, const model_component& model_comp)
        {
            auto handle = scn.create_handle(entity);
            if(!transform.is_active_global() || !model_comp.is_enabled() || is_skipped(handle, skip))
            {
                return;
            }
            if(auto hit = raycast_model(handle, transform, model_comp, ray, cache))
            {
                hits.push_back(*hit);
            }
        });
    std::sort(hits.begin(),
              hits.end(),
              [](const scene_ray_hit& lhs, const scene_ray_hit& rhs)
              {
                  return lhs.distance < rhs.distance;
              });
    return hits;
}

} // namespace unravel
