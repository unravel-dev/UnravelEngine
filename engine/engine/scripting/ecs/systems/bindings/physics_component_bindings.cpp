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

auto internal_m2n_physics_get_contact_event_flags(entt::entity id) -> uint8_t
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return static_cast<uint8_t>(comp->get_contact_event_flags());
    }

    return static_cast<uint8_t>(contact_event_flags::none);
}

void internal_m2n_physics_set_contact_event_flags(entt::entity id, uint8_t flags)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_contact_event_flags(static_cast<contact_event_flags>(flags));
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

auto internal_m2n_physics_get_body_type(entt::entity id) -> rigidbody_type
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_body_type();
    }

    return rigidbody_type::dynamic;
}

void internal_m2n_physics_set_body_type(entt::entity id, rigidbody_type type)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_body_type(type);
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

auto internal_m2n_physics_get_gravity_scale(entt::entity id) -> float
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_gravity_scale();
    }
    return 1.0f;
}

void internal_m2n_physics_set_gravity_scale(entt::entity id, float value)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_gravity_scale(value);
    }
}

auto internal_m2n_physics_get_linear_damping(entt::entity id) -> float
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_linear_damping();
    }
    return 0.0f;
}

void internal_m2n_physics_set_linear_damping(entt::entity id, float value)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_linear_damping(value);
    }
}

auto internal_m2n_physics_get_angular_damping(entt::entity id) -> float
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_angular_damping();
    }
    return 0.0f;
}

void internal_m2n_physics_set_angular_damping(entt::entity id, float value)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_angular_damping(value);
    }
}

auto internal_m2n_physics_get_is_continuous(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->is_continuous();
    }
    return false;
}

void internal_m2n_physics_set_is_continuous(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_is_continuous(value);
    }
}

auto internal_m2n_physics_get_can_sleep(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->can_sleep();
    }
    return true;
}

void internal_m2n_physics_set_can_sleep(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_can_sleep(value);
    }
}

auto internal_m2n_physics_get_interpolate(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_interpolate();
    }
    return false;
}

void internal_m2n_physics_set_interpolate(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_interpolate(value);
    }
}

auto internal_m2n_physics_is_sleeping(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->is_sleeping();
    }
    return false;
}

void internal_m2n_physics_wake_up(entt::entity id)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->wake_up();
    }
}

void internal_m2n_physics_sleep(entt::entity id)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->put_to_sleep();
    }
}

auto internal_m2n_physics_get_surface_velocity(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_surface_velocity();
    }
    return {};
}

void internal_m2n_physics_set_surface_velocity(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_surface_velocity(value);
    }
}

auto internal_m2n_physics_get_freeze_position(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return math::vec3(comp->get_freeze_position());
    }
    return {};
}

void internal_m2n_physics_set_freeze_position(entt::entity id, bool x, bool y, bool z)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_freeze_position({x, y, z});
    }
}

auto internal_m2n_physics_get_freeze_rotation(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return math::vec3(comp->get_freeze_rotation());
    }
    return {};
}

void internal_m2n_physics_set_freeze_rotation(entt::entity id, bool x, bool y, bool z)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_freeze_rotation({x, y, z});
    }
}

void internal_m2n_physics_apply_force_at_position(entt::entity id,
                                                  const math::vec3& force,
                                                  const math::vec3& position,
                                                  force_mode mode)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->apply_force_at_position(force, position, mode);
    }
}

} // namespace

void register_physics_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.PhysicsComponent");
    reg.add_internal_call("internal_m2n_physics_apply_explosion_force",
                            script_internal_call(internal_m2n_physics_apply_explosion_force));
    reg.add_internal_call("internal_m2n_physics_apply_force", script_internal_call(internal_m2n_physics_apply_force));
    reg.add_internal_call("internal_m2n_physics_apply_torque", script_internal_call(internal_m2n_physics_apply_torque));
    reg.add_internal_call("internal_m2n_physics_get_velocity", script_internal_call(internal_m2n_physics_get_velocity));
    reg.add_internal_call("internal_m2n_physics_set_velocity", script_internal_call(internal_m2n_physics_set_velocity));
    reg.add_internal_call("internal_m2n_physics_get_angular_velocity",
                            script_internal_call(internal_m2n_physics_get_angular_velocity));
    reg.add_internal_call("internal_m2n_physics_set_angular_velocity",
                            script_internal_call(internal_m2n_physics_set_angular_velocity));

    reg.add_internal_call("internal_m2n_physics_get_include_layers",
                            script_internal_call(internal_m2n_physics_get_include_layers));
    reg.add_internal_call("internal_m2n_physics_set_include_layers",
                            script_internal_call(internal_m2n_physics_set_include_layers));
    reg.add_internal_call("internal_m2n_physics_get_exclude_layers",
                            script_internal_call(internal_m2n_physics_get_exclude_layers));
    reg.add_internal_call("internal_m2n_physics_set_exclude_layers",
                            script_internal_call(internal_m2n_physics_set_exclude_layers));
    reg.add_internal_call("internal_m2n_physics_get_collision_layers",
                            script_internal_call(internal_m2n_physics_get_collision_layers));

    reg.add_internal_call("internal_m2n_physics_get_is_sensor",
                            script_internal_call(internal_m2n_physics_get_is_sensor));
    reg.add_internal_call("internal_m2n_physics_set_is_sensor",
                            script_internal_call(internal_m2n_physics_set_is_sensor));
    reg.add_internal_call("internal_m2n_physics_get_contact_event_flags",
                            script_internal_call(internal_m2n_physics_get_contact_event_flags));
    reg.add_internal_call("internal_m2n_physics_set_contact_event_flags",
                            script_internal_call(internal_m2n_physics_set_contact_event_flags));
    reg.add_internal_call("internal_m2n_physics_get_mass",
                            script_internal_call(internal_m2n_physics_get_mass));
    reg.add_internal_call("internal_m2n_physics_set_mass",
                            script_internal_call(internal_m2n_physics_set_mass));
    reg.add_internal_call("internal_m2n_physics_get_body_type",
                            script_internal_call(internal_m2n_physics_get_body_type));
    reg.add_internal_call("internal_m2n_physics_set_body_type",
                            script_internal_call(internal_m2n_physics_set_body_type));
    reg.add_internal_call("internal_m2n_physics_get_use_gravity",
                            script_internal_call(internal_m2n_physics_get_use_gravity));
    reg.add_internal_call("internal_m2n_physics_set_use_gravity",
                            script_internal_call(internal_m2n_physics_set_use_gravity));
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_get_gravity_scale);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_set_gravity_scale);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_get_linear_damping);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_set_linear_damping);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_get_angular_damping);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_set_angular_damping);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_get_is_continuous);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_set_is_continuous);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_get_can_sleep);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_set_can_sleep);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_get_interpolate);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_set_interpolate);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_is_sleeping);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_wake_up);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_sleep);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_get_surface_velocity);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_set_surface_velocity);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_get_freeze_position);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_set_freeze_position);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_get_freeze_rotation);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_set_freeze_rotation);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_physics_apply_force_at_position);
}

} // namespace unravel
