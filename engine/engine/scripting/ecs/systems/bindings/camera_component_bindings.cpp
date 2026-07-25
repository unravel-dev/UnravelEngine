#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/layers/layer_mask.h>
#include <engine/rendering/ecs/components/camera_component.h>

namespace unravel
{
namespace
{

auto internal_m2n_camera_get_fov(entt::entity id) -> float
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        return comp->get_fov();
    }
    return 0.0f;
}

void internal_m2n_camera_set_fov(entt::entity id, float fov)
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        comp->set_fov(fov);
    }
}

auto internal_m2n_camera_get_near_clip(entt::entity id) -> float
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        return comp->get_near_clip();
    }
    return 0.0f;
}

void internal_m2n_camera_set_near_clip(entt::entity id, float distance)
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        comp->set_near_clip(distance);
    }
}

auto internal_m2n_camera_get_far_clip(entt::entity id) -> float
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        return comp->get_far_clip();
    }
    return 0.0f;
}

void internal_m2n_camera_set_far_clip(entt::entity id, float distance)
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        comp->set_far_clip(distance);
    }
}

auto internal_m2n_camera_get_projection_mode(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        return static_cast<uint32_t>(comp->get_projection_mode());
    }
    return static_cast<uint32_t>(projection_mode::perspective);
}

void internal_m2n_camera_set_projection_mode(entt::entity id, uint32_t mode)
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        comp->set_projection_mode(static_cast<projection_mode>(mode));
    }
}

auto internal_m2n_camera_get_ortho_size(entt::entity id) -> float
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        return comp->get_ortho_size();
    }
    return 0.0f;
}

void internal_m2n_camera_set_ortho_size(entt::entity id, float size)
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        comp->set_ortho_size(size);
    }
}

auto internal_m2n_camera_get_include_mask(entt::entity id) -> int
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        return comp->get_render_include_mask().mask;
    }
    return layer_reserved::everything_layer;
}

void internal_m2n_camera_set_include_mask(entt::entity id, int mask)
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        layer_mask layer{};
        layer.mask = mask;
        comp->set_render_include_mask(layer);
    }
}

auto internal_m2n_camera_get_exclude_mask(entt::entity id) -> int
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        return comp->get_render_exclude_mask().mask;
    }
    return layer_reserved::nothing_layer;
}

void internal_m2n_camera_set_exclude_mask(entt::entity id, int mask)
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        layer_mask layer{};
        layer.mask = mask;
        comp->set_render_exclude_mask(layer);
    }
}

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
        return ray_origin + (ray_dir * distance_from_camera);
    }
    return {};
}

} // namespace

void register_camera_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.CameraComponent");
    reg.add_internal_call("internal_m2n_camera_get_fov", dotnet_internal_call(internal_m2n_camera_get_fov));
    reg.add_internal_call("internal_m2n_camera_set_fov", dotnet_internal_call(internal_m2n_camera_set_fov));
    reg.add_internal_call("internal_m2n_camera_get_near_clip", dotnet_internal_call(internal_m2n_camera_get_near_clip));
    reg.add_internal_call("internal_m2n_camera_set_near_clip", dotnet_internal_call(internal_m2n_camera_set_near_clip));
    reg.add_internal_call("internal_m2n_camera_get_far_clip", dotnet_internal_call(internal_m2n_camera_get_far_clip));
    reg.add_internal_call("internal_m2n_camera_set_far_clip", dotnet_internal_call(internal_m2n_camera_set_far_clip));
    reg.add_internal_call("internal_m2n_camera_get_projection_mode",
                          dotnet_internal_call(internal_m2n_camera_get_projection_mode));
    reg.add_internal_call("internal_m2n_camera_set_projection_mode",
                          dotnet_internal_call(internal_m2n_camera_set_projection_mode));
    reg.add_internal_call("internal_m2n_camera_get_ortho_size",
                          dotnet_internal_call(internal_m2n_camera_get_ortho_size));
    reg.add_internal_call("internal_m2n_camera_set_ortho_size",
                          dotnet_internal_call(internal_m2n_camera_set_ortho_size));
    reg.add_internal_call("internal_m2n_camera_get_include_mask",
                          dotnet_internal_call(internal_m2n_camera_get_include_mask));
    reg.add_internal_call("internal_m2n_camera_set_include_mask",
                          dotnet_internal_call(internal_m2n_camera_set_include_mask));
    reg.add_internal_call("internal_m2n_camera_get_exclude_mask",
                          dotnet_internal_call(internal_m2n_camera_get_exclude_mask));
    reg.add_internal_call("internal_m2n_camera_set_exclude_mask",
                          dotnet_internal_call(internal_m2n_camera_set_exclude_mask));
    reg.add_internal_call("internal_m2n_camera_screen_point_to_ray",
                          dotnet_internal_call(internal_m2n_camera_screen_point_to_ray));
    reg.add_internal_call("internal_m2n_camera_screen_point_to_world_2d",
                          dotnet_internal_call(internal_m2n_camera_screen_point_to_world_2d));
    reg.add_internal_call("internal_m2n_camera_screen_point_to_world",
                          dotnet_internal_call(internal_m2n_camera_screen_point_to_world));
}

} // namespace unravel
