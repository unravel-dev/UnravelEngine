#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/physics/ecs/systems/physics_system.h>

#include <algorithm>

namespace unravel
{
namespace
{

using managed_raycast_hit = dotnetpp_backend::managed_interface::raycast_hit;

/// The shape of a box or capsule query; Physics.QueryShape in C# has the same values.
enum class query_shape : int32_t
{
    /// size is the box's full extents.
    box,
    /// size.x is the radius, size.y the length of the segment between the two sphere centers.
    capsule,
};

/// The shape a query sweeps or tests, or none for a kind this build does not know.
auto make_query_shape(query_shape kind, const math::vec3& size) -> hpp::optional<physics_compound_shape>
{
    physics_compound_shape shape;
    switch(kind)
    {
        case query_shape::box:
        {
            physics_box_shape box;
            box.extends = size;
            shape.shape = box;
            return shape;
        }
        case query_shape::capsule:
        {
            physics_capsule_shape capsule;
            capsule.radius = size.x;
            capsule.length = size.y;
            shape.shape = capsule;
            return shape;
        }
    }
    return hpp::nullopt;
}

void write_hit(managed_raycast_hit& out, const raycast_hit& hit)
{
    using converter = dotnet::managed_interface::converter;
    out.entity = hit.entity;
    out.point = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(hit.point);
    out.normal = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(hit.normal);
    out.distance = hit.distance;
}

/// Writes the hit, if any, and tells whether there was one.
auto write_first_hit(managed_raycast_hit* out, const hpp::optional<raycast_hit>& hit) -> bool
{
    if(hit)
    {
        write_hit(*out, *hit);
    }
    return hit.has_value();
}

/// The hits in the managed layout, for a call that returns them as a new array.
auto to_managed_hits(const physics_vector<raycast_hit>& hits) -> hpp::small_vector<managed_raycast_hit>
{
    hpp::small_vector<managed_raycast_hit> managed(hits.size());
    for(size_t i = 0; i < hits.size(); ++i)
    {
        write_hit(managed[i], hits[i]);
    }
    return managed;
}

/// Copies the nearest hits that fit into the caller's array and returns how many it copied.
auto copy_hits(const physics_vector<raycast_hit>& hits, managed_raycast_hit* out, int capacity) -> int
{
    const size_t count = std::min(hits.size(), size_t(std::max(capacity, 0)));
    for(size_t i = 0; i < count; ++i)
    {
        write_hit(out[i], hits[i]);
    }
    return int(count);
}

/// Copies the entities that fit into the caller's array and returns how many it copied.
auto copy_entities(const physics_vector<entt::entity>& entities, entt::entity* out, int capacity) -> int
{
    const size_t count = std::min(entities.size(), size_t(std::max(capacity, 0)));
    std::copy_n(entities.begin(), count, out);
    return int(count);
}

auto get_physics() -> physics_system&
{
    return engine::context().get_cached<physics_system>();
}

//-------------------------------------------------

auto internal_m2n_physics_ray_cast(managed_raycast_hit* hit,
                                   const math::vec3& origin,
                                   const math::vec3& direction,
                                   float max_distance,
                                   int layer_mask,
                                   bool query_sensors) -> bool
{
    return write_first_hit(hit, get_physics().ray_cast(origin, direction, max_distance, layer_mask, query_sensors));
}

auto internal_m2n_physics_ray_cast_all(const math::vec3& origin,
                                       const math::vec3& direction,
                                       float max_distance,
                                       int layer_mask,
                                       bool query_sensors) -> hpp::small_vector<managed_raycast_hit>
{
    return to_managed_hits(get_physics().ray_cast_all(origin, direction, max_distance, layer_mask, query_sensors));
}

auto internal_m2n_physics_ray_cast_non_alloc(managed_raycast_hit* results,
                                             int capacity,
                                             const math::vec3& origin,
                                             const math::vec3& direction,
                                             float max_distance,
                                             int layer_mask,
                                             bool query_sensors) -> int
{
    return copy_hits(get_physics().ray_cast_all(origin, direction, max_distance, layer_mask, query_sensors),
                     results,
                     capacity);
}

auto internal_m2n_physics_sphere_cast(managed_raycast_hit* hit,
                                      const math::vec3& origin,
                                      const math::vec3& direction,
                                      float radius,
                                      float max_distance,
                                      int layer_mask,
                                      bool query_sensors) -> bool
{
    return write_first_hit(hit,
                           get_physics().sphere_cast(origin, direction, radius, max_distance, layer_mask, query_sensors));
}

auto internal_m2n_physics_sphere_cast_all(const math::vec3& origin,
                                          const math::vec3& direction,
                                          float radius,
                                          float max_distance,
                                          int layer_mask,
                                          bool query_sensors) -> hpp::small_vector<managed_raycast_hit>
{
    return to_managed_hits(
        get_physics().sphere_cast_all(origin, direction, radius, max_distance, layer_mask, query_sensors));
}

auto internal_m2n_physics_sphere_cast_non_alloc(managed_raycast_hit* results,
                                                int capacity,
                                                const math::vec3& origin,
                                                const math::vec3& direction,
                                                float radius,
                                                float max_distance,
                                                int layer_mask,
                                                bool query_sensors) -> int
{
    return copy_hits(get_physics().sphere_cast_all(origin, direction, radius, max_distance, layer_mask, query_sensors),
                     results,
                     capacity);
}

auto internal_m2n_physics_sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors)
    -> physics_vector<entt::entity>
{
    return get_physics().sphere_overlap(origin, radius, layer_mask, query_sensors);
}

auto internal_m2n_physics_sphere_overlap_non_alloc(entt::entity* results,
                                                   int capacity,
                                                   const math::vec3& origin,
                                                   float radius,
                                                   int layer_mask,
                                                   bool query_sensors) -> int
{
    return copy_entities(get_physics().sphere_overlap(origin, radius, layer_mask, query_sensors), results, capacity);
}

auto internal_m2n_physics_get_gravity() -> math::vec3
{
    return get_physics().get_gravity();
}

void internal_m2n_physics_set_gravity(const math::vec3& gravity)
{
    get_physics().set_gravity(gravity);
}

void internal_m2n_physics_explode(const math::vec3& center, float radius, float falloff, float impulse_per_area, int layer_mask)
{
    get_physics().explode(center, radius, falloff, impulse_per_area, layer_mask);
}

auto internal_m2n_physics_shape_cast(managed_raycast_hit* hit,
                                     query_shape kind,
                                     const math::vec3& size,
                                     const math::vec3& origin,
                                     const math::quat& rotation,
                                     const math::vec3& direction,
                                     float max_distance,
                                     int layer_mask,
                                     bool query_sensors) -> bool
{
    const auto shape = make_query_shape(kind, size);
    if(!shape)
    {
        return false;
    }
    return write_first_hit(
        hit,
        get_physics().shape_cast(*shape, origin, rotation, direction, max_distance, layer_mask, query_sensors));
}

auto internal_m2n_physics_shape_cast_all(query_shape kind,
                                         const math::vec3& size,
                                         const math::vec3& origin,
                                         const math::quat& rotation,
                                         const math::vec3& direction,
                                         float max_distance,
                                         int layer_mask,
                                         bool query_sensors) -> hpp::small_vector<managed_raycast_hit>
{
    const auto shape = make_query_shape(kind, size);
    if(!shape)
    {
        return {};
    }
    return to_managed_hits(
        get_physics().shape_cast_all(*shape, origin, rotation, direction, max_distance, layer_mask, query_sensors));
}

auto internal_m2n_physics_shape_cast_non_alloc(managed_raycast_hit* results,
                                               int capacity,
                                               query_shape kind,
                                               const math::vec3& size,
                                               const math::vec3& origin,
                                               const math::quat& rotation,
                                               const math::vec3& direction,
                                               float max_distance,
                                               int layer_mask,
                                               bool query_sensors) -> int
{
    const auto shape = make_query_shape(kind, size);
    if(!shape)
    {
        return 0;
    }
    return copy_hits(
        get_physics().shape_cast_all(*shape, origin, rotation, direction, max_distance, layer_mask, query_sensors),
        results,
        capacity);
}

auto internal_m2n_physics_shape_overlap(query_shape kind,
                                        const math::vec3& size,
                                        const math::vec3& origin,
                                        const math::quat& rotation,
                                        int layer_mask,
                                        bool query_sensors) -> physics_vector<entt::entity>
{
    const auto shape = make_query_shape(kind, size);
    if(!shape)
    {
        return {};
    }
    return get_physics().shape_overlap(*shape, origin, rotation, layer_mask, query_sensors);
}

auto internal_m2n_physics_shape_overlap_non_alloc(entt::entity* results,
                                                  int capacity,
                                                  query_shape kind,
                                                  const math::vec3& size,
                                                  const math::vec3& origin,
                                                  const math::quat& rotation,
                                                  int layer_mask,
                                                  bool query_sensors) -> int
{
    const auto shape = make_query_shape(kind, size);
    if(!shape)
    {
        return 0;
    }
    return copy_entities(get_physics().shape_overlap(*shape, origin, rotation, layer_mask, query_sensors),
                         results,
                         capacity);
}

} // namespace

void register_physics_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.Physics");
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_ray_cast);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_ray_cast_all);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_ray_cast_non_alloc);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_sphere_cast);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_sphere_cast_all);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_sphere_cast_non_alloc);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_sphere_overlap);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_sphere_overlap_non_alloc);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_get_gravity);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_set_gravity);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_explode);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_shape_cast);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_shape_cast_all);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_shape_cast_non_alloc);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_shape_overlap);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_shape_overlap_non_alloc);
}

} // namespace unravel
