#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/physics/ecs/components/joint_component.h>

namespace unravel
{
// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace joint_bindings
{

/**
 * @brief Managed JointSpring. joint_spring holds a math::vec3, which is padded to 16 bytes, while C#'s Vector3 is 12.
 */
struct managed_joint_spring
{
    bool enabled;
    float frequency;
    float damping_ratio;
    float target;
    dotnetpp_backend::managed_interface::vector3 target_rotation;
};

// JointLimits, JointSpring, JointMotor and JointSteering in C# mirror these structs field for field, bools as one
// byte, and cross the boundary as they are. A field added on one side must be added on the other.
static_assert(sizeof(joint_limits) == 16, "JointLimits in C# is 16 bytes.");
static_assert(sizeof(managed_joint_spring) == 28, "JointSpring in C# is 28 bytes.");
static_assert(sizeof(joint_motor) == 12, "JointMotor in C# is 12 bytes.");
static_assert(sizeof(joint_steering) == 16, "JointSteering in C# is 16 bytes.");

/// The id C# sees for a body: Entity.Invalid (id 0) for none or a destroyed one, as get_entity_from_id reads back.
auto to_script_entity(const entt::handle& body) -> entt::entity
{
    return body ? body.entity() : entt::entity(0);
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
        return to_script_entity(comp->get_body_a());
    }
    return entt::entity(0);
}

void internal_m2n_joint_set_body_a(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_body_a(get_entity_from_id(value));
    }
}

auto internal_m2n_joint_get_body_b(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        return to_script_entity(comp->get_body_b());
    }
    return entt::entity(0);
}

void internal_m2n_joint_set_body_b(entt::entity id, entt::entity value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_body_b(get_entity_from_id(value));
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

auto internal_m2n_joint_get_connected_anchor(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        return comp->get_connected_anchor();
    }
    return {};
}

void internal_m2n_joint_set_connected_anchor(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_connected_anchor(value);
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

/// Reads a joint's setting or state, or a default without the component.
template<typename Getter>
auto read_joint(entt::entity id, Getter&& getter) -> decltype(getter(std::declval<const joint_component&>()))
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        return getter(*comp);
    }
    return {};
}

void internal_m2n_joint_get_limits(entt::entity id, joint_limits* value)
{
    *value = read_joint(id, [](const joint_component& c) { return c.get_limits(); });
}

void internal_m2n_joint_set_limits(entt::entity id, const joint_limits& value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_limits(value);
    }
}

void internal_m2n_joint_get_spring(entt::entity id, managed_joint_spring* value)
{
    using converter = dotnet::managed_interface::converter;
    const joint_spring spring = read_joint(id, [](const joint_component& c) { return c.get_spring(); });
    *value = {spring.enabled,
              spring.frequency,
              spring.damping_ratio,
              spring.target,
              converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(spring.target_rotation)};
}

void internal_m2n_joint_set_spring(entt::entity id, const managed_joint_spring& value)
{
    using converter = dotnet::managed_interface::converter;
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_spring({value.enabled,
                          value.frequency,
                          value.damping_ratio,
                          value.target,
                          converter::convert<dotnetpp_backend::managed_interface::vector3, math::vec3>(value.target_rotation)});
    }
}

void internal_m2n_joint_get_motor(entt::entity id, joint_motor* value)
{
    *value = read_joint(id, [](const joint_component& c) { return c.get_motor(); });
}

void internal_m2n_joint_set_motor(entt::entity id, const joint_motor& value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_motor(value);
    }
}

void internal_m2n_joint_get_steering(entt::entity id, joint_steering* value)
{
    *value = read_joint(id, [](const joint_component& c) { return c.get_steering(); });
}

void internal_m2n_joint_set_steering(entt::entity id, const joint_steering& value)
{
    if(auto comp = safe_get_component<joint_component>(id))
    {
        comp->set_steering(value);
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
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_type);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_type);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_body_a);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_body_a);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_body_b);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_body_b);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_anchor);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_anchor);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_axis_rotation);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_axis_rotation);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_connected_anchor);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_connected_anchor);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_collide_connected);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_collide_connected);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_limits);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_limits);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_spring);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_spring);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_motor);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_motor);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_steering);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_steering);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_friction);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_friction);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_distance);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_distance);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_break_force);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_break_force);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_break_torque);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_set_break_torque);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_is_active);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_is_broken);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_angle);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_position);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_speed);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_reaction_force);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_get_reaction_torque);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_break);
    UNRAVEL_ADD_INTERNAL_CALL(reg, internal_m2n_joint_repair);
}

} // namespace unravel
