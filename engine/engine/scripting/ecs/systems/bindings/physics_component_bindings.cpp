#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/physics/ecs/components/physics_component.h>

namespace unravel
{
namespace
{

//------------------------------

void internal_m2n_physics_apply_explosion_force(entt::entity id,
                                                float explosion_force,
                                                const math::vec3& explosion_position,
                                                float explosion_radius,
                                                float upwards_modifier,
                                                force_mode mode)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->apply_explosion_force(explosion_force, explosion_position, explosion_radius, upwards_modifier, mode);
    }
}
void internal_m2n_physics_apply_force(entt::entity id, const math::vec3& value, force_mode mode)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->apply_force(value, mode);
    }
}

void internal_m2n_physics_apply_torque(entt::entity id, const math::vec3& value, force_mode mode)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->apply_torque(value, mode);
    }
}

auto internal_m2n_physics_get_velocity(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_velocity();
    }

    return {};
}

void internal_m2n_physics_set_velocity(entt::entity id, const math::vec3& velocity)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_velocity(velocity);
    }
}

auto internal_m2n_physics_get_angular_velocity(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_angular_velocity();
    }

    return {};
}

void internal_m2n_physics_set_angular_velocity(entt::entity id, const math::vec3& velocity)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_angular_velocity(velocity);
    }
}

auto internal_m2n_physics_get_include_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_collision_include_mask();
    }

    return {};
}

void internal_m2n_physics_set_include_layers(entt::entity id, layer_mask mask)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_collision_include_mask(mask);
    }
}

auto internal_m2n_physics_get_exclude_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_collision_exclude_mask();
    }

    return {};
}

void internal_m2n_physics_set_exclude_layers(entt::entity id, layer_mask mask)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_collision_exclude_mask(mask);
    }
}

auto internal_m2n_physics_get_collision_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_collision_mask();
    }

    return {};
}

auto internal_m2n_physics_get_is_sensor(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->is_sensor();
    }

    return false;
}

void internal_m2n_physics_set_is_sensor(entt::entity id, bool sensor)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_is_sensor(sensor);
    }
}

auto internal_m2n_physics_get_mass(entt::entity id) -> float
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_mass();
    }

    return 1.0f;
}

void internal_m2n_physics_set_mass(entt::entity id, float mass)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_mass(mass);
    }
}

auto internal_m2n_physics_get_is_kinematic(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->is_kinematic();
    }

    return false;
}

void internal_m2n_physics_set_is_kinematic(entt::entity id, bool kinematic)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_is_kinematic(kinematic);
    }
}

auto internal_m2n_physics_get_use_gravity(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->is_using_gravity();
    }

    return true;
}

void internal_m2n_physics_set_use_gravity(entt::entity id, bool use_gravity)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_is_using_gravity(use_gravity);
    }
}

} // namespace

void register_physics_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.PhysicsComponent");
    reg.add_internal_call("internal_m2n_physics_apply_explosion_force",
                            dotnet_internal_call(internal_m2n_physics_apply_explosion_force));
    reg.add_internal_call("internal_m2n_physics_apply_force", dotnet_internal_call(internal_m2n_physics_apply_force));
    reg.add_internal_call("internal_m2n_physics_apply_torque", dotnet_internal_call(internal_m2n_physics_apply_torque));
    reg.add_internal_call("internal_m2n_physics_get_velocity", dotnet_internal_call(internal_m2n_physics_get_velocity));
    reg.add_internal_call("internal_m2n_physics_set_velocity", dotnet_internal_call(internal_m2n_physics_set_velocity));
    reg.add_internal_call("internal_m2n_physics_get_angular_velocity",
                            dotnet_internal_call(internal_m2n_physics_get_angular_velocity));
    reg.add_internal_call("internal_m2n_physics_set_angular_velocity",
                            dotnet_internal_call(internal_m2n_physics_set_angular_velocity));

    reg.add_internal_call("internal_m2n_physics_get_include_layers",
                            dotnet_internal_call(internal_m2n_physics_get_include_layers));
    reg.add_internal_call("internal_m2n_physics_set_include_layers",
                            dotnet_internal_call(internal_m2n_physics_set_include_layers));
    reg.add_internal_call("internal_m2n_physics_get_exclude_layers",
                            dotnet_internal_call(internal_m2n_physics_get_exclude_layers));
    reg.add_internal_call("internal_m2n_physics_set_exclude_layers",
                            dotnet_internal_call(internal_m2n_physics_set_exclude_layers));
    reg.add_internal_call("internal_m2n_physics_get_collision_layers",
                            dotnet_internal_call(internal_m2n_physics_get_collision_layers));

    reg.add_internal_call("internal_m2n_physics_get_is_sensor",
                            dotnet_internal_call(internal_m2n_physics_get_is_sensor));
    reg.add_internal_call("internal_m2n_physics_set_is_sensor",
                            dotnet_internal_call(internal_m2n_physics_set_is_sensor));
    reg.add_internal_call("internal_m2n_physics_get_mass",
                            dotnet_internal_call(internal_m2n_physics_get_mass));
    reg.add_internal_call("internal_m2n_physics_set_mass",
                            dotnet_internal_call(internal_m2n_physics_set_mass));
    reg.add_internal_call("internal_m2n_physics_get_is_kinematic",
                            dotnet_internal_call(internal_m2n_physics_get_is_kinematic));
    reg.add_internal_call("internal_m2n_physics_set_is_kinematic",
                            dotnet_internal_call(internal_m2n_physics_set_is_kinematic));
    reg.add_internal_call("internal_m2n_physics_get_use_gravity",
                            dotnet_internal_call(internal_m2n_physics_get_use_gravity));
    reg.add_internal_call("internal_m2n_physics_set_use_gravity",
                            dotnet_internal_call(internal_m2n_physics_set_use_gravity));
}

} // namespace unravel
