#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/physics/ecs/systems/physics_system.h>

namespace unravel
{
namespace
{

//-------------------------------------------------

auto internal_m2n_physics_ray_cast(dotnetpp_backend::managed_interface::raycast_hit* hit,
                                   const math::vec3& origin,
                                   const math::vec3& direction,
                                   float max_distance,
                                   int layer_mask,
                                   bool query_sensors) -> bool
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto ray_hit = physics.ray_cast(origin, direction, max_distance, layer_mask, query_sensors);

    using converter = dotnet::managed_interface::converter;

    if(ray_hit)
    {
        hit->entity = ray_hit->entity;
        hit->point = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit->point);
        hit->normal = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit->normal);
        hit->distance = ray_hit->distance;
    }

    return ray_hit.has_value();
}

auto internal_m2n_physics_ray_cast_all(const math::vec3& origin,
                                       const math::vec3& direction,
                                       float max_distance,
                                       int layer_mask,
                                       bool query_sensors) -> hpp::small_vector<dotnetpp_backend::managed_interface::raycast_hit>
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto ray_hits = physics.ray_cast_all(origin, direction, max_distance, layer_mask, query_sensors);

    hpp::small_vector<dotnetpp_backend::managed_interface::raycast_hit> hits;

    using converter = dotnet::managed_interface::converter;
    for(const auto& ray_hit : ray_hits)
    {
        auto& hit = hits.emplace_back();
        hit.entity = ray_hit.entity;
        hit.point = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit.point);
        hit.normal = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit.normal);
        hit.distance = ray_hit.distance;
    }

    return hits;
}

auto internal_m2n_physics_sphere_cast(dotnetpp_backend::managed_interface::raycast_hit* hit,
                                      const math::vec3& origin,
                                      const math::vec3& direction,
                                      float radius,
                                      float max_distance,
                                      int layer_mask,
                                      bool query_sensors) -> bool
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto ray_hit = physics.sphere_cast(origin, direction, radius, max_distance, layer_mask, query_sensors);

    using converter = dotnet::managed_interface::converter;

    if(ray_hit)
    {
        hit->entity = ray_hit->entity;
        hit->point = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit->point);
        hit->normal = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit->normal);
        hit->distance = ray_hit->distance;
    }

    return ray_hit.has_value();
}

auto internal_m2n_physics_sphere_cast_all(const math::vec3& origin,
                                          const math::vec3& direction,
                                          float radius,
                                          float max_distance,
                                          int layer_mask,
                                          bool query_sensors) -> hpp::small_vector<dotnetpp_backend::managed_interface::raycast_hit>
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto ray_hits = physics.sphere_cast_all(origin, direction, radius, max_distance, layer_mask, query_sensors);

    hpp::small_vector<dotnetpp_backend::managed_interface::raycast_hit> hits;

    using converter = dotnet::managed_interface::converter;
    for(const auto& ray_hit : ray_hits)
    {
        auto& hit = hits.emplace_back();
        hit.entity = ray_hit.entity;
        hit.point = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit.point);
        hit.normal = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit.normal);
        hit.distance = ray_hit.distance;
    }

    return hits;
}

auto internal_m2n_physics_sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors)
    -> physics_vector<entt::entity>
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto hits = physics.sphere_overlap(origin, radius, layer_mask, query_sensors);

    return hits;
}

auto internal_m2n_physics_get_gravity() -> math::vec3
{
    return engine::context().get_cached<physics_system>().get_gravity();
}

void internal_m2n_physics_set_gravity(const math::vec3& gravity)
{
    engine::context().get_cached<physics_system>().set_gravity(gravity);
}

void internal_m2n_physics_explode(const math::vec3& center, float radius, float falloff, float impulse_per_area, int layer_mask)
{
    engine::context().get_cached<physics_system>().explode(center, radius, falloff, impulse_per_area, layer_mask);
}

/// A query shape from its kind: 0 box (size = full extents), 1 capsule (size.x radius, size.y segment length), 2 sphere.
auto make_query_shape(int kind, const math::vec3& size) -> physics_compound_shape
{
    physics_compound_shape shape;
    if(kind == 0)
    {
        physics_box_shape box;
        box.extends = size;
        shape.shape = box;
    }
    else if(kind == 1)
    {
        physics_capsule_shape capsule;
        capsule.radius = size.x;
        capsule.length = size.y;
        shape.shape = capsule;
    }
    else
    {
        physics_sphere_shape sphere;
        sphere.radius = size.x;
        shape.shape = sphere;
    }
    return shape;
}

void write_hit(dotnetpp_backend::managed_interface::raycast_hit* out, const raycast_hit& hit)
{
    using converter = dotnet::managed_interface::converter;
    out->entity = hit.entity;
    out->point = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(hit.point);
    out->normal = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(hit.normal);
    out->distance = hit.distance;
}

auto internal_m2n_physics_shape_cast(dotnetpp_backend::managed_interface::raycast_hit* hit,
                                     int kind,
                                     const math::vec3& size,
                                     const math::vec3& origin,
                                     const math::quat& rotation,
                                     const math::vec3& direction,
                                     float max_distance,
                                     int layer_mask,
                                     bool query_sensors) -> bool
{
    auto& physics = engine::context().get_cached<physics_system>();
    const auto result = physics.shape_cast(make_query_shape(kind, size),
                                           origin,
                                           rotation,
                                           direction,
                                           max_distance,
                                           layer_mask,
                                           query_sensors);
    if(result)
    {
        write_hit(hit, *result);
    }
    return result.has_value();
}

auto internal_m2n_physics_shape_cast_all(int kind,
                                         const math::vec3& size,
                                         const math::vec3& origin,
                                         const math::quat& rotation,
                                         const math::vec3& direction,
                                         float max_distance,
                                         int layer_mask,
                                         bool query_sensors)
    -> hpp::small_vector<dotnetpp_backend::managed_interface::raycast_hit>
{
    auto& physics = engine::context().get_cached<physics_system>();
    const auto results = physics.shape_cast_all(make_query_shape(kind, size),
                                                origin,
                                                rotation,
                                                direction,
                                                max_distance,
                                                layer_mask,
                                                query_sensors);
    hpp::small_vector<dotnetpp_backend::managed_interface::raycast_hit> hits;
    for(const auto& result : results)
    {
        write_hit(&hits.emplace_back(), result);
    }
    return hits;
}

auto internal_m2n_physics_shape_overlap(int kind,
                                        const math::vec3& size,
                                        const math::vec3& origin,
                                        const math::quat& rotation,
                                        int layer_mask,
                                        bool query_sensors) -> physics_vector<entt::entity>
{
    auto& physics = engine::context().get_cached<physics_system>();
    return physics.shape_overlap(make_query_shape(kind, size), origin, rotation, layer_mask, query_sensors);
}

} // namespace

void register_physics_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.Physics");
    reg.add_internal_call("internal_m2n_physics_ray_cast", script_internal_call(internal_m2n_physics_ray_cast));
    reg.add_internal_call("internal_m2n_physics_ray_cast_all", script_internal_call(internal_m2n_physics_ray_cast_all));
    reg.add_internal_call("internal_m2n_physics_sphere_cast", script_internal_call(internal_m2n_physics_sphere_cast));
    reg.add_internal_call("internal_m2n_physics_sphere_cast_all",
                            script_internal_call(internal_m2n_physics_sphere_cast_all));
    reg.add_internal_call("internal_m2n_physics_sphere_overlap",
                            script_internal_call(internal_m2n_physics_sphere_overlap));
    reg.add_internal_call("internal_m2n_physics_get_gravity", script_internal_call(internal_m2n_physics_get_gravity));
    reg.add_internal_call("internal_m2n_physics_set_gravity", script_internal_call(internal_m2n_physics_set_gravity));
    reg.add_internal_call("internal_m2n_physics_explode", script_internal_call(internal_m2n_physics_explode));
    reg.add_internal_call("internal_m2n_physics_shape_cast", script_internal_call(internal_m2n_physics_shape_cast));
    reg.add_internal_call("internal_m2n_physics_shape_cast_all",
                          script_internal_call(internal_m2n_physics_shape_cast_all));
    reg.add_internal_call("internal_m2n_physics_shape_overlap",
                          script_internal_call(internal_m2n_physics_shape_overlap));
}

} // namespace unravel
