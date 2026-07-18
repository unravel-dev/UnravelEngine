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

} // namespace

void register_physics_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.Physics");
    reg.add_internal_call("internal_m2n_physics_ray_cast", dotnet_internal_call(internal_m2n_physics_ray_cast));
    reg.add_internal_call("internal_m2n_physics_ray_cast_all", dotnet_internal_call(internal_m2n_physics_ray_cast_all));
    reg.add_internal_call("internal_m2n_physics_sphere_cast", dotnet_internal_call(internal_m2n_physics_sphere_cast));
    reg.add_internal_call("internal_m2n_physics_sphere_cast_all",
                            dotnet_internal_call(internal_m2n_physics_sphere_cast_all));
    reg.add_internal_call("internal_m2n_physics_sphere_overlap",
                            dotnet_internal_call(internal_m2n_physics_sphere_overlap));
}

} // namespace unravel
