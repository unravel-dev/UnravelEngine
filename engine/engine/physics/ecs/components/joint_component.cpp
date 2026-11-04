#include "joint_component.h"

#include <engine/ecs/components/transform_component.h>

#include <utility>

namespace unravel
{

void joint_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);
    auto& component = entity.get<joint_component>();
    component.set_owner(entity);
    component.dirty_.set();
    component.needs_rebuild_ = true;
}

void joint_component::on_destroy_component(entt::registry& r, entt::entity e)
{
}

void joint_component::mark_changed(bool rebuild)
{
    dirty_.set();
    needs_rebuild_ |= rebuild;
}

void joint_component::set_type(joint_type type)
{
    if(type_ == type)
    {
        return;
    }
    type_ = type;
    mark_changed(true);
}

auto joint_component::get_type() const noexcept -> joint_type
{
    return type_;
}

void joint_component::set_body_a(entt::handle body)
{
    if(body_a_ == body)
    {
        return;
    }
    body_a_ = body;
    mark_changed(true);
}

auto joint_component::get_body_a() const noexcept -> entt::handle
{
    return body_a_;
}

void joint_component::set_body_b(entt::handle body)
{
    if(body_b_ == body)
    {
        return;
    }
    body_b_ = body;
    mark_changed(true);
}

auto joint_component::get_body_b() const noexcept -> entt::handle
{
    return body_b_;
}

auto joint_component::get_resolved_body_b() noexcept -> entt::handle
{
    if(body_b_)
    {
        return body_b_;
    }
    return get_owner();
}

void joint_component::set_anchor(const math::vec3& anchor)
{
    if(anchor_ == anchor)
    {
        return;
    }
    anchor_ = anchor;
    mark_changed(true);
}

auto joint_component::get_anchor() const noexcept -> const math::vec3&
{
    return anchor_;
}

void joint_component::set_axis_rotation(const math::vec3& rotation)
{
    if(axis_rotation_ == rotation)
    {
        return;
    }
    axis_rotation_ = rotation;
    mark_changed(true);
}

auto joint_component::get_axis_rotation() const noexcept -> const math::vec3&
{
    return axis_rotation_;
}

void joint_component::set_collide_connected(bool collide)
{
    if(collide_connected_ == collide)
    {
        return;
    }
    collide_connected_ = collide;
    mark_changed(false);
}

auto joint_component::get_collide_connected() const noexcept -> bool
{
    return collide_connected_;
}

void joint_component::set_limits(const joint_limits& limits)
{
    if(limits_ == limits)
    {
        return;
    }
    limits_ = limits;
    mark_changed(false);
}

auto joint_component::get_limits() const noexcept -> const joint_limits&
{
    return limits_;
}

void joint_component::set_spring(const joint_spring& spring)
{
    if(spring_ == spring)
    {
        return;
    }
    // A fixed joint is made soft or rigid at creation.
    const bool rebuild = type_ == joint_type::fixed && spring_.enabled != spring.enabled;
    spring_ = spring;
    mark_changed(rebuild);
}

auto joint_component::get_spring() const noexcept -> const joint_spring&
{
    return spring_;
}

void joint_component::set_motor(const joint_motor& motor)
{
    if(motor_ == motor)
    {
        return;
    }
    motor_ = motor;
    mark_changed(false);
}

auto joint_component::get_motor() const noexcept -> const joint_motor&
{
    return motor_;
}

void joint_component::set_steering(const joint_steering& steering)
{
    if(steering_ == steering)
    {
        return;
    }
    steering_ = steering;
    mark_changed(false);
}

auto joint_component::get_steering() const noexcept -> const joint_steering&
{
    return steering_;
}

void joint_component::set_friction(float friction)
{
    friction = math::max(friction, 0.0f);
    if(friction_ == friction)
    {
        return;
    }
    friction_ = friction;
    mark_changed(false);
}

auto joint_component::get_friction() const noexcept -> float
{
    return friction_;
}

void joint_component::set_distance(float distance)
{
    distance = math::max(distance, 0.0f);
    if(distance_ == distance)
    {
        return;
    }
    distance_ = distance;
    // 0 means measured at creation, which only a new joint can do.
    mark_changed(distance == 0.0f);
}

auto joint_component::get_distance() const noexcept -> float
{
    return distance_;
}

void joint_component::set_connected_anchor(const math::vec3& anchor)
{
    if(connected_anchor_ == anchor)
    {
        return;
    }
    connected_anchor_ = anchor;
    mark_changed(true);
}

auto joint_component::get_connected_anchor() const noexcept -> const math::vec3&
{
    return connected_anchor_;
}

void joint_component::set_break_force(float force)
{
    force = math::max(force, 0.0f);
    if(break_force_ == force)
    {
        return;
    }
    break_force_ = force;
    mark_changed(false);
}

auto joint_component::get_break_force() const noexcept -> float
{
    return break_force_;
}

void joint_component::set_break_torque(float torque)
{
    torque = math::max(torque, 0.0f);
    if(break_torque_ == torque)
    {
        return;
    }
    break_torque_ = torque;
    mark_changed(false);
}

auto joint_component::get_break_torque() const noexcept -> float
{
    return break_torque_;
}

void joint_component::break_joint()
{
    requested_break_ = true;
    requested_repair_ = false;
    dirty_.set();
}

void joint_component::repair()
{
    requested_repair_ = true;
    requested_break_ = false;
    dirty_.set();
}

auto joint_component::get_state() const noexcept -> const joint_state&
{
    return state_;
}

auto joint_component::get_state() noexcept -> joint_state&
{
    return state_;
}

auto joint_component::get_frame_global() const -> math::transform
{
    auto owner = get_owner();
    math::transform world;
    if(owner)
    {
        if(const auto* transform = owner.try_get<transform_component>())
        {
            world = transform->get_transform_global();
        }
    }
    const math::vec3 position = world.transform_coord(anchor_);
    const math::quat rotation = math::normalize(world.get_rotation() * math::quat(math::radians(axis_rotation_)));
    math::transform frame;
    frame.set_position(position);
    frame.set_rotation(rotation);
    return frame;
}

auto joint_component::is_dirty(uint8_t id) const noexcept -> bool
{
    return dirty_[id];
}

void joint_component::set_dirty(uint8_t id, bool dirty) noexcept
{
    dirty_.set(id, dirty);
}

auto joint_component::needs_rebuild() const noexcept -> bool
{
    return needs_rebuild_;
}

void joint_component::set_needs_rebuild(bool rebuild) noexcept
{
    needs_rebuild_ = rebuild;
}

auto joint_component::take_requested_break() noexcept -> bool
{
    return std::exchange(requested_break_, false);
}

auto joint_component::take_requested_repair() noexcept -> bool
{
    return std::exchange(requested_repair_, false);
}

} // namespace unravel
