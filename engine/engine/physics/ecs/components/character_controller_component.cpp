#include "character_controller_component.h"
#include <engine/physics/ecs/systems/physics_system.h>

namespace unravel
{

void character_controller_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);
    auto& component = entity.get<character_controller_component>();
    component.set_owner(entity);
    component.dirty_.set();
    component.dirty_properties_.set();
}

void character_controller_component::on_destroy_component(entt::registry& r, entt::entity e)
{
}

void character_controller_component::set_radius(float radius)
{
    if(math::epsilonEqual(radius_, radius, math::epsilon<float>()))
    {
        return;
    }
    radius_ = radius;
    on_change_shape();
}

auto character_controller_component::get_radius() const noexcept -> float
{
    return radius_;
}

void character_controller_component::set_height(float height)
{
    if(math::epsilonEqual(height_, height, math::epsilon<float>()))
    {
        return;
    }
    height_ = height;
    on_change_shape();
}

auto character_controller_component::get_height() const noexcept -> float
{
    return height_;
}

void character_controller_component::set_center(const math::vec3& center)
{
    if(center_ == center)
    {
        return;
    }
    center_ = center;
    on_change_shape();
}

auto character_controller_component::get_center() const noexcept -> const math::vec3&
{
    return center_;
}

void character_controller_component::set_step_height(float step_height)
{
    if(math::epsilonEqual(step_height_, step_height, math::epsilon<float>()))
    {
        return;
    }
    step_height_ = step_height;
    dirty_.set();
    set_property_dirty(character_controller_property::step_height, true);
}

auto character_controller_component::get_step_height() const noexcept -> float
{
    return step_height_;
}

void character_controller_component::set_slope_limit(float slope_limit_degrees)
{
    if(math::epsilonEqual(slope_limit_, slope_limit_degrees, math::epsilon<float>()))
    {
        return;
    }
    slope_limit_ = slope_limit_degrees;
    dirty_.set();
    set_property_dirty(character_controller_property::slope_limit, true);
}

auto character_controller_component::get_slope_limit() const noexcept -> float
{
    return slope_limit_;
}

void character_controller_component::set_skin_width(float skin_width)
{
    if(math::epsilonEqual(skin_width_, skin_width, math::epsilon<float>()))
    {
        return;
    }
    skin_width_ = skin_width;
    dirty_.set();
    set_property_dirty(character_controller_property::skin_width, true);
}

auto character_controller_component::get_skin_width() const noexcept -> float
{
    return skin_width_;
}

void character_controller_component::set_gravity_scale(float scale)
{
    if(math::epsilonEqual(gravity_scale_, scale, math::epsilon<float>()))
    {
        return;
    }
    gravity_scale_ = scale;
    dirty_.set();
    set_property_dirty(character_controller_property::gravity_scale, true);
}

auto character_controller_component::get_gravity_scale() const noexcept -> float
{
    return gravity_scale_;
}

void character_controller_component::set_collision_include_mask(layer_mask mask)
{
    dirty_.set();
    set_property_dirty(character_controller_property::layer, true);
    collision_include_mask_ = mask;
}

auto character_controller_component::get_collision_include_mask() const -> layer_mask
{
    return collision_include_mask_;
}

void character_controller_component::set_collision_exclude_mask(layer_mask mask)
{
    dirty_.set();
    set_property_dirty(character_controller_property::layer, true);
    collision_exclude_mask_ = mask;
}

auto character_controller_component::get_collision_exclude_mask() const -> layer_mask
{
    return collision_exclude_mask_;
}

auto character_controller_component::get_collision_mask() const -> layer_mask
{
    return layer_mask{collision_include_mask_.mask & ~collision_exclude_mask_.mask};
}

void character_controller_component::move(const math::vec3& displacement)
{
    physics_system::move_character(*this, displacement);
}

void character_controller_component::jump(float speed)
{
    physics_system::jump_character(*this, math::vec3{0.0f, speed, 0.0f});
}

void character_controller_component::jump(const math::vec3& velocity)
{
    physics_system::jump_character(*this, velocity);
}

void character_controller_component::apply_impulse(const math::vec3& impulse)
{
    physics_system::apply_impulse_character(*this, impulse);
}

void character_controller_component::warp(const math::vec3& position)
{
    physics_system::warp_character(*this, position);
}

void character_controller_component::set_terminal_velocity(float speed)
{
    if(math::epsilonEqual(terminal_velocity_, speed, math::epsilon<float>()))
    {
        return;
    }
    terminal_velocity_ = speed;
    dirty_.set();
    set_property_dirty(character_controller_property::movement_params, true);
}

auto character_controller_component::get_terminal_velocity() const noexcept -> float
{
    return terminal_velocity_;
}

void character_controller_component::set_linear_velocity(const math::vec3& velocity)
{
    physics_system::set_character_linear_velocity(*this, velocity);
}

auto character_controller_component::get_linear_velocity() const noexcept -> math::vec3
{
    return velocity_;
}

void character_controller_component::set_linear_damping(float damping)
{
    if(math::epsilonEqual(linear_damping_, damping, math::epsilon<float>()))
    {
        return;
    }
    linear_damping_ = damping;
    dirty_.set();
    set_property_dirty(character_controller_property::movement_params, true);
}

auto character_controller_component::get_linear_damping() const noexcept -> float
{
    return linear_damping_;
}

auto character_controller_component::can_jump() const noexcept -> bool
{
    return grounded_;
}

auto character_controller_component::is_grounded() const noexcept -> bool
{
    return grounded_;
}

auto character_controller_component::get_velocity() const noexcept -> const math::vec3&
{
    return velocity_;
}

void character_controller_component::set_grounded(bool grounded) noexcept
{
    grounded_ = grounded;
}

void character_controller_component::set_velocity_internal(const math::vec3& vel) noexcept
{
    velocity_ = vel;
}

auto character_controller_component::is_dirty(uint8_t id) const noexcept -> bool
{
    return dirty_[id];
}

void character_controller_component::set_dirty(uint8_t id, bool dirty) noexcept
{
    dirty_.set(id, dirty);
    if(!dirty)
    {
        dirty_properties_ = {};
    }
}

auto character_controller_component::is_property_dirty(character_controller_property prop) const noexcept -> bool
{
    return dirty_properties_[static_cast<underlying_t>(prop)];
}

auto character_controller_component::are_any_properties_dirty() const noexcept -> bool
{
    return dirty_properties_.any();
}

void character_controller_component::set_property_dirty(character_controller_property prop, bool dirty) noexcept
{
    dirty_properties_[static_cast<underlying_t>(prop)] = dirty;
}

void character_controller_component::on_change_shape()
{
    dirty_.set();
    set_property_dirty(character_controller_property::shape, true);
}

} // namespace unravel
