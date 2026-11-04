#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/physics/ecs/components/joint_component.h>

namespace unravel
{
// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace joint_bindings
{

/// Entity.Invalid is id 0, the entity every scene reserves: it stands for none, like a null handle.
auto to_handle(entt::entity id) -> entt::handle
{
    auto& registry = *engine::context().get_cached<ecs>().get_scene().registry;
    const bool is_none = id == entt::entity{0} || !registry.valid(id);
    return is_none ? entt::handle{} : entt::handle(registry, id);
}

auto internal_m2n_joint_get_type(entt::entity id) -> uint8_t
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        return uint8_t(comp->get_type());
    }
    return 0;
}

void internal_m2n_joint_set_type(entt::entity id, uint8_t value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_type(joint_type(value));
    }
}

auto internal_m2n_joint_get_body_a(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        const auto body = comp->get_body_a();
        return body ? body.entity() : entt::entity(entt::null);
    }
    return entt::null;
}

void internal_m2n_joint_set_body_a(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_body_a(to_handle(value));
    }
}

auto internal_m2n_joint_get_body_b(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        const auto body = comp->get_body_b();
        return body ? body.entity() : entt::entity(entt::null);
    }
    return entt::null;
}

void internal_m2n_joint_set_body_b(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_body_b(to_handle(value));
    }
}

auto internal_m2n_joint_get_anchor(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        return comp->get_anchor();
    }
    return {};
}

void internal_m2n_joint_set_anchor(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_anchor(value);
    }
}

auto internal_m2n_joint_get_axis_rotation(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        return comp->get_axis_rotation();
    }
    return {};
}

void internal_m2n_joint_set_axis_rotation(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_axis_rotation(value);
    }
}

auto internal_m2n_joint_get_collide_connected(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        return comp->get_collide_connected();
    }
    return false;
}

void internal_m2n_joint_set_collide_connected(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_collide_connected(value);
    }
}

/// Reads one field of a joint's settings, or a default without the component.
template<typename Getter>
auto read_joint(entt::entity id, Getter&& getter) -> decltype(getter(std::declval<const joint_component&>()))
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        return getter(*comp);
    }
    return {};
}

auto internal_m2n_joint_get_limits_enabled(entt::entity id) -> bool
{
    return read_joint(id, [](const joint_component& c) { return c.get_limits().enabled; });
}

auto internal_m2n_joint_get_limits_lower(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_limits().lower; });
}

auto internal_m2n_joint_get_limits_upper(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_limits().upper; });
}

auto internal_m2n_joint_get_limits_cone(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_limits().cone_angle; });
}

void internal_m2n_joint_set_limits(entt::entity id, bool enabled, float lower, float upper, float cone_angle)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_limits({enabled, lower, upper, cone_angle});
    }
}

auto internal_m2n_joint_get_spring_enabled(entt::entity id) -> bool
{
    return read_joint(id, [](const joint_component& c) { return c.get_spring().enabled; });
}

auto internal_m2n_joint_get_spring_frequency(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_spring().frequency; });
}

auto internal_m2n_joint_get_spring_damping(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_spring().damping_ratio; });
}

auto internal_m2n_joint_get_spring_target(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_spring().target; });
}

auto internal_m2n_joint_get_spring_target_rotation(entt::entity id) -> math::vec3
{
    return read_joint(id, [](const joint_component& c) { return c.get_spring().target_rotation; });
}

void internal_m2n_joint_set_spring(entt::entity id,
                                   bool enabled,
                                   float frequency,
                                   float damping_ratio,
                                   float target,
                                   const math::vec3& target_rotation)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_spring({enabled, frequency, damping_ratio, target, target_rotation});
    }
}

auto internal_m2n_joint_get_motor_enabled(entt::entity id) -> bool
{
    return read_joint(id, [](const joint_component& c) { return c.get_motor().enabled; });
}

auto internal_m2n_joint_get_motor_speed(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_motor().speed; });
}

auto internal_m2n_joint_get_motor_force(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_motor().max_force; });
}

void internal_m2n_joint_set_motor(entt::entity id, bool enabled, float speed, float max_force)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_motor({enabled, speed, max_force});
    }
}

auto internal_m2n_joint_get_steering_enabled(entt::entity id) -> bool
{
    return read_joint(id, [](const joint_component& c) { return c.get_steering().enabled; });
}

auto internal_m2n_joint_get_steering_angle(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_steering().angle; });
}

auto internal_m2n_joint_get_steering_limit(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_steering().limit; });
}

auto internal_m2n_joint_get_steering_torque(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_steering().max_torque; });
}

void internal_m2n_joint_set_steering(entt::entity id, bool enabled, float angle, float limit, float max_torque)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_steering({enabled, angle, limit, max_torque});
    }
}

auto internal_m2n_joint_get_friction(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_friction(); });
}

void internal_m2n_joint_set_friction(entt::entity id, float value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_friction(value);
    }
}

auto internal_m2n_joint_get_distance(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_distance(); });
}

void internal_m2n_joint_set_distance(entt::entity id, float value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_distance(value);
    }
}

auto internal_m2n_joint_get_break_force(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_break_force(); });
}

void internal_m2n_joint_set_break_force(entt::entity id, float value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_break_force(value);
    }
}

auto internal_m2n_joint_get_break_torque(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_break_torque(); });
}

void internal_m2n_joint_set_break_torque(entt::entity id, float value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_break_torque(value);
    }
}

auto internal_m2n_joint_is_active(entt::entity id) -> bool
{
    return read_joint(id, [](const joint_component& c) { return c.get_state().is_active; });
}

auto internal_m2n_joint_is_broken(entt::entity id) -> bool
{
    return read_joint(id, [](const joint_component& c) { return c.get_state().is_broken; });
}

auto internal_m2n_joint_get_angle(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_state().angle; });
}

auto internal_m2n_joint_get_position(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_state().position; });
}

auto internal_m2n_joint_get_speed(entt::entity id) -> float
{
    return read_joint(id, [](const joint_component& c) { return c.get_state().speed; });
}

auto internal_m2n_joint_get_reaction_force(entt::entity id) -> math::vec3
{
    return read_joint(id, [](const joint_component& c) { return c.get_state().force; });
}

auto internal_m2n_joint_get_reaction_torque(entt::entity id) -> math::vec3
{
    return read_joint(id, [](const joint_component& c) { return c.get_state().torque; });
}

void internal_m2n_joint_break(entt::entity id)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->break_joint();
    }
}

void internal_m2n_joint_repair(entt::entity id)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->repair();
    }
}

} // namespace joint_bindings

void register_joint_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    using namespace joint_bindings;
    auto reg = dotnet::internal_call_registry("Unravel.Core.JointComponent");
#define UNRAVEL_JOINT_CALL(name) reg.add_internal_call(#name, script_internal_call(name))
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_type);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_type);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_body_a);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_body_a);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_body_b);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_body_b);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_anchor);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_anchor);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_axis_rotation);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_axis_rotation);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_collide_connected);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_collide_connected);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_limits_enabled);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_limits_lower);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_limits_upper);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_limits_cone);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_limits);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_spring_enabled);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_spring_frequency);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_spring_damping);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_spring_target);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_spring_target_rotation);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_spring);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_motor_enabled);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_motor_speed);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_motor_force);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_motor);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_steering_enabled);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_steering_angle);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_steering_limit);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_steering_torque);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_steering);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_friction);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_friction);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_distance);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_distance);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_break_force);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_break_force);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_break_torque);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_set_break_torque);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_is_active);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_is_broken);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_angle);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_position);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_speed);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_reaction_force);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_get_reaction_torque);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_break);
    UNRAVEL_JOINT_CALL(internal_m2n_joint_repair);
#undef UNRAVEL_JOINT_CALL
}

} // namespace unravel
