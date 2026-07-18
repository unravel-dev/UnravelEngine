#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/rendering/ecs/components/camera_component.h>

namespace unravel
{
namespace
{

//------------------------------
auto internal_m2n_camera_screen_point_to_ray(entt::entity id,
                                             const math::vec2& origin,
                                             dotnetpp_backend::managed_interface::ray* managed_ray) -> bool
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        math::vec3 ray_origin{};
        math::vec3 ray_dir{};
        bool result = comp->get_camera().viewport_to_ray(origin, ray_origin, ray_dir);
        if(result)
        {
            using converter = dotnet::managed_interface::converter;
            managed_ray->origin = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_origin);
            managed_ray->direction = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_dir);
        }
        return result;
    }

    return false;
}

auto internal_m2n_camera_screen_point_to_world_2d(entt::entity id, const math::vec2& origin) -> math::vec3
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        math::vec3 world_pos{};
        
        const auto& frustum = comp->get_camera().get_frustum();

        bool result = comp->get_camera().viewport_to_world(origin, frustum.planes[math::volume_plane::near_plane], world_pos, false);
        if(!result)
        {
            return {};
        }
        return world_pos;
    }
    return {};
}

auto internal_m2n_camera_screen_point_to_world(entt::entity id, const math::vec3& origin) -> math::vec3
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        math::vec2 screen_point(origin.x, origin.y);
        float distance_from_camera = origin.z;
        
        math::vec3 ray_origin{};
        math::vec3 ray_dir{};
        
        if(!comp->get_camera().viewport_to_ray(screen_point, ray_origin, ray_dir))
        {
            return {};
        }
        
        math::vec3 world_pos = ray_origin + (ray_dir * distance_from_camera);
        return world_pos;
    }
    return {};
}

} // namespace

void register_camera_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.CameraComponent");
    reg.add_internal_call("internal_m2n_camera_screen_point_to_ray",
                            dotnet_internal_call(internal_m2n_camera_screen_point_to_ray));
    reg.add_internal_call("internal_m2n_camera_screen_point_to_world_2d",
                            dotnet_internal_call(internal_m2n_camera_screen_point_to_world_2d));
    reg.add_internal_call("internal_m2n_camera_screen_point_to_world",
                            dotnet_internal_call(internal_m2n_camera_screen_point_to_world));
}

} // namespace unravel
