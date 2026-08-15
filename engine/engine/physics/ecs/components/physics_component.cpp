#include "physics_component.h"
#include <cstdint>
#include <engine/ecs/components/transform_component.h>
#include <engine/physics/ecs/systems/physics_system.h>

namespace unravel
{

void physics_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);

    auto& component = entity.get<physics_component>();
    component.set_owner(entity);
    component.dirty_.set();
    component.dirty_properties_.set();
}

void physics_component::on_destroy_component(entt::registry& r, entt::entity e)
{
}

void physics_component::set_body_type(rigidbody_type type)
{
    if(body_type_ == type)
    {
        return;
    }

    body_type_ = type;

    on_change_kind();
}

auto physics_component::get_body_type() const noexcept -> rigidbody_type
{
    return body_type_;
}

void physics_component::on_change_kind()
{
    dirty_.set();
    set_property_dirty(physics_property::kind, true);
}

void physics_component::set_is_autoscaled(bool autoscaled)
{
    is_autoscaled_ = autoscaled;
}

auto physics_component::is_autoscaled() const noexcept -> bool
{
    return is_autoscaled_;
}

void physics_component::set_is_using_gravity(bool use_gravity)
{
    if(is_using_gravity_ == use_gravity)
    {
        return;
    }

    is_using_gravity_ = use_gravity;

    on_change_gravity();
}

auto physics_component::is_using_gravity() const noexcept -> bool
{
    return is_using_gravity_;
}

void physics_component::on_change_gravity()
{
    dirty_.set();
    set_property_dirty(physics_property::gravity, true);
}

void physics_component::set_mass(float mass)
{
    if(math::epsilonEqual(mass_, mass, math::epsilon<float>()))
    {
        return;
    }

    mass_ = mass;

    on_change_mass();
}

auto physics_component::get_mass() const noexcept -> float
{
    return mass_;
}

void physics_component::on_change_mass()
{
    dirty_.set();
    set_property_dirty(physics_property::mass, true);
}

void physics_component::set_is_sensor(bool sensor)
{
    if(is_sensor_ == sensor)
    {
        return;
    }

    is_sensor_ = sensor;

    on_change_sensor();
}

auto physics_component::is_sensor() const noexcept -> bool
{
    return is_sensor_;
}

void physics_component::on_change_sensor()
{
    dirty_.set();
    set_property_dirty(physics_property::sensor, true);
}

auto physics_component::get_contact_event_flags() const noexcept -> contact_event_flags
{
    return contact_events_;
}

void physics_component::set_contact_event_flags(contact_event_flags flags)
{
    if(contact_events_ == flags)
    {
        return;
    }

    contact_events_ = flags;

    on_change_contact_events();
}

void physics_component::set_contact_event_flag(contact_event_flags flag, bool enabled)
{
    set_contact_event_flags(enabled ? (contact_events_ | flag) : (contact_events_ & ~flag));
}

void physics_component::set_sensor_events_enabled(bool enabled)
{
    set_contact_event_flag(contact_event_flags::sensor_events, enabled);
}

auto physics_component::is_sensor_events_enabled() const noexcept -> bool
{
    return has_any(contact_events_, contact_event_flags::sensor_events);
}

void physics_component::set_collision_events_enabled(bool enabled)
{
    set_contact_event_flag(contact_event_flags::collision_events, enabled);
}

auto physics_component::is_collision_events_enabled() const noexcept -> bool
{
    return has_any(contact_events_, contact_event_flags::collision_events);
}

void physics_component::set_sensor_exit_on_destroy(bool enabled)
{
    set_contact_event_flag(contact_event_flags::sensor_exit_on_destroy, enabled);
}

auto physics_component::is_sensor_exit_on_destroy() const noexcept -> bool
{
    return has_any(contact_events_, contact_event_flags::sensor_exit_on_destroy);
}

void physics_component::set_collision_exit_on_destroy(bool enabled)
{
    set_contact_event_flag(contact_event_flags::collision_exit_on_destroy, enabled);
}

auto physics_component::is_collision_exit_on_destroy() const noexcept -> bool
{
    return has_any(contact_events_, contact_event_flags::collision_exit_on_destroy);
}

void physics_component::on_change_contact_events()
{
    dirty_.set();
    set_property_dirty(physics_property::contact_events, true);
}

auto physics_component::is_dirty(uint8_t id) const noexcept -> bool
{
    return dirty_[id];
}

void physics_component::set_dirty(uint8_t id, bool dirty) noexcept
{
    dirty_.set(id, dirty);

    if(!dirty)
    {
        dirty_properties_ = {};
    }
}

auto physics_component::is_property_dirty(physics_property prop) const noexcept -> bool
{
    return dirty_properties_[static_cast<std::underlying_type_t<physics_property>>(prop)];
}

auto physics_component::are_any_properties_dirty() const noexcept -> bool
{
    return dirty_properties_.any();
}

auto physics_component::are_all_properties_dirty() const noexcept -> bool
{
    return dirty_properties_.all();
}

void physics_component::set_property_dirty(physics_property prop, bool dirty) noexcept
{
    dirty_properties_[static_cast<std::underlying_type_t<physics_property>>(prop)] = dirty;
}

auto physics_component::get_shapes_count() const -> size_t
{
    return compound_shape_.size();
}

auto physics_component::get_shape_by_index(size_t index) const -> const physics_compound_shape&
{
    return compound_shape_.at(index);
}

void physics_component::set_shape_by_index(size_t index, const physics_compound_shape& shape)
{
    compound_shape_.at(index) = shape;
}

auto physics_component::get_shapes() const -> const std::vector<physics_compound_shape>&
{
    return compound_shape_;
}

void physics_component::set_shapes(const std::vector<physics_compound_shape>& shape)
{
    compound_shape_ = shape;

    on_change_shape();
}

void physics_component::on_change_shape()
{
    dirty_.set();
    set_property_dirty(physics_property::shape, true);
}

auto physics_component::get_material() const -> const asset_handle<physics_material>&
{
    return material_;
}

void physics_component::set_material(const asset_handle<physics_material>& material)
{
    if(material_ == material)
    {
        return;
    }
    material_ = material;

    on_change_material();
}

void physics_component::on_change_material()
{
    dirty_.set();
    set_property_dirty(physics_property::material, true);
}

void physics_component::apply_explosion_force(float explosion_force,
                                              const math::vec3& explosion_position,
                                              float explosion_radius,
                                              float upwards_modifier,
                                              force_mode mode)
{
    physics_system::apply_explosion_force(*this,
                                          explosion_force,
                                          explosion_position,
                                          explosion_radius,
                                          upwards_modifier,
                                          mode);
}

void physics_component::apply_force(const math::vec3& force, force_mode mode)
{
    physics_system::apply_force(*this, force, mode);
}

void physics_component::apply_torque(const math::vec3& torque, force_mode mode)
{
    physics_system::apply_torque(*this, torque, mode);
}

void physics_component::clear_kinematic_velocities()
{
    physics_system::clear_kinematic_velocities(*this);
}

void physics_component::set_freeze_rotation(const math::bvec3& xyz)
{
    if(freeze_rotation_xyz_ == xyz)
    {
        return;
    }

    freeze_rotation_xyz_ = xyz;

    dirty_.set();
    set_property_dirty(physics_property::constraints, true);
}

void physics_component::set_freeze_position(const math::bvec3& xyz)
{
    if(freeze_position_xyz_ == xyz)
    {
        return;
    }
    freeze_position_xyz_ = xyz;

    dirty_.set();
    set_property_dirty(physics_property::constraints, true);
}

auto physics_component::get_freeze_rotation() const -> const math::bvec3&
{
    return freeze_rotation_xyz_;
}

auto physics_component::get_freeze_position() const -> const math::bvec3&
{
    return freeze_position_xyz_;
}

auto physics_component::get_velocity() const -> const math::vec3&
{
    return velocity_;
}

void physics_component::set_velocity(const math::vec3& velocity)
{
    velocity_ = velocity;
    dirty_.set();
    set_property_dirty(physics_property::velocity, true);
}

void physics_component::set_velocity_internal(const math::vec3& velocity) noexcept
{
    velocity_ = velocity;
}

auto physics_component::get_angular_velocity() const -> const math::vec3&
{
    return angular_velocity_;
}

void physics_component::set_angular_velocity(const math::vec3& velocity)
{
    angular_velocity_ = velocity;

    dirty_.set();
    set_property_dirty(physics_property::angular_velocity, true);
}

void physics_component::set_angular_velocity_internal(const math::vec3& velocity) noexcept
{
    angular_velocity_ = velocity;
}

auto physics_component::get_collision_include_mask() const -> layer_mask
{
    return collision_include_mask_;
}

void physics_component::set_collision_include_mask(layer_mask group)
{
    dirty_.set();
    set_property_dirty(physics_property::layer, true);
    collision_include_mask_ = group;
}

auto physics_component::get_collision_exclude_mask() const -> layer_mask
{
    return collision_exclude_mask_;
}

void physics_component::set_collision_exclude_mask(layer_mask mask)
{
    dirty_.set();
    set_property_dirty(physics_property::layer, true);
    collision_exclude_mask_ = mask;
}

auto physics_component::get_collision_mask() const -> layer_mask
{
    return layer_mask{collision_include_mask_.mask & ~collision_exclude_mask_.mask};
}

} // namespace unravel
