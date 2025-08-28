#include "transform_actions.h"
#include "base/basetypes.hpp"
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>

namespace unravel
{

// Transform Move Action Implementation
transform_move_action_t::transform_move_action_t(entt::handle ent, const math::vec3& old_pos, const math::vec3& new_pos)
    : entity(ent), old_position(old_pos), new_position(new_pos)
{
    name = "Move Transform";
}

void transform_move_action_t::do_action()
{
    if (entity)
    {
        if (auto transform = entity.try_get<transform_component>())
        {
            transform->set_position_local(new_position);
            prefab_override_context::mark_transform_as_changed(entity, true, false, false, false);
        }
    }
}

void transform_move_action_t::undo_action()
{
    if (entity)
    {
        if (auto transform = entity.try_get<transform_component>())
        {
            transform->set_position_local(old_position);
            prefab_override_context::mark_transform_as_changed(entity, true, false, false, false);
        }
    }
}

auto transform_move_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const transform_move_action_t&>(previous);
    return entity == prev.entity && name == prev.name;
}

void transform_move_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const transform_move_action_t&>(previous);
    old_position = prev.old_position; // Extend the range of the move
}

auto transform_move_action_t::is_valid() const -> bool
{
    return entity.valid() && entity.try_get<transform_component>();
}   

// Transform Rotate Action Implementation
transform_rotate_action_t::transform_rotate_action_t(entt::handle ent, const math::quat& old_rot, const math::quat& new_rot)
    : entity(ent), old_rotation(old_rot), new_rotation(new_rot)
{
    name = "Rotate Transform";
}

void transform_rotate_action_t::do_action()
{
    if (entity)
    {
        if (auto transform = entity.try_get<transform_component>())
        {
            transform->set_rotation_local(new_rotation);
            prefab_override_context::mark_transform_as_changed(entity, false, true, false, false);
        }
    }
}

void transform_rotate_action_t::undo_action()
{
    if (entity)
    {
        if (auto transform = entity.try_get<transform_component>())
        {
            transform->set_rotation_local(old_rotation);
            prefab_override_context::mark_transform_as_changed(entity, false, true, false, false);
        }
    }
}

auto transform_rotate_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const transform_rotate_action_t&>(previous);
    return entity == prev.entity && name == prev.name;
}

void transform_rotate_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const transform_rotate_action_t&>(previous);
    old_rotation = prev.old_rotation; // Extend the range of the rotation
}

auto transform_rotate_action_t::is_valid() const -> bool
{
    return entity.valid() && entity.try_get<transform_component>();
}

// Transform Scale Action Implementation
transform_scale_action_t::transform_scale_action_t(entt::handle ent, const math::vec3& old_sc, const math::vec3& new_sc)
    : entity(ent), old_scale(old_sc), new_scale(new_sc)
{
    name = "Scale Transform";
}

void transform_scale_action_t::do_action()
{
    if (entity)
    {
        if (auto transform = entity.try_get<transform_component>())
        {
            transform->set_scale_local(new_scale);
            prefab_override_context::mark_transform_as_changed(entity, false, false, true, false);
        }
    }
}

void transform_scale_action_t::undo_action()
{
    if (entity)
    {
        if (auto transform = entity.try_get<transform_component>())
        {
            transform->set_scale_local(old_scale);
            prefab_override_context::mark_transform_as_changed(entity, false, false, true, false);
        }
    }
}

auto transform_scale_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const transform_scale_action_t&>(previous);
    return entity == prev.entity && name == prev.name;
}

void transform_scale_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const transform_scale_action_t&>(previous);
    old_scale = prev.old_scale; // Extend the range of the scale
}

auto transform_scale_action_t::is_valid() const -> bool
{
    return entity.valid() && entity.try_get<transform_component>();
}

// Transform Skew Action Implementation
transform_skew_action_t::transform_skew_action_t(entt::handle ent, const math::vec3& old_sk, const math::vec3& new_sk)
    : entity(ent), old_skew(old_sk), new_skew(new_sk)
{
    name = "Skew Transform";
}

void transform_skew_action_t::do_action()
{
    if (entity)
    {
        if (auto transform = entity.try_get<transform_component>())
        {
            transform->set_skew_local(new_skew);
            prefab_override_context::mark_transform_as_changed(entity, false, false, false, true);
        }
    }
}

void transform_skew_action_t::undo_action()
{
    if (entity)
    {
        if (auto transform = entity.try_get<transform_component>())
        {
            transform->set_skew_local(old_skew);
            prefab_override_context::mark_transform_as_changed(entity, false, false, false, true);
        }
    }
}

auto transform_skew_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const transform_skew_action_t&>(previous);
    return entity == prev.entity && name == prev.name;
}

void transform_skew_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const transform_skew_action_t&>(previous);
    old_skew = prev.old_skew; // Extend the range of the skew
}

auto transform_skew_action_t::is_valid() const -> bool
{
    return entity.valid() && entity.try_get<transform_component>();
}

// Property Action Implementation
property_action_t::property_action_t(instance_getter inst, entt::meta_data prop, const entt::meta_any& old_val, const entt::meta_any& new_val)
    : instance(inst), property(prop), old_value(old_val), new_value(new_val)
{
    name = "Property Action " + entt::get_pretty_name(property);
}

void property_action_t::do_action()
{
    entt::meta_any inst;
    instance(inst);
    if(!inst)
    {
        return;
    }
    property.set(inst, new_value);
}

void property_action_t::undo_action()
{
    entt::meta_any inst;
    instance(inst);
    if(!inst)
    {
        return;
    }
    property.set(inst, old_value);
}

auto property_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const property_action_t&>(previous);
    entt::meta_any inst;
    instance(inst);
    entt::meta_any prev_inst;
    prev.instance(prev_inst);
    return inst == prev_inst && property == prev.property;
}

void property_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const property_action_t&>(previous);
    old_value = prev.old_value;
}

auto property_action_t::is_valid() const -> bool
{
    entt::meta_any inst;
    instance(inst);
    if(!inst)
    {
        return false;
    }
    return true;
}

var_action_t::var_action_t(instance_getter inst, const entt::meta_any& old_val, const entt::meta_any& new_val)
    : instance(inst), old_value(old_val), new_value(new_val)
{
    name = "Var Action";
}

void var_action_t::do_action()
{
    entt::meta_any inst;
    instance(inst);
    if(!inst)
    {
        return;
    }
    inst.assign(new_value);
}

void var_action_t::undo_action()
{
    entt::meta_any inst;
    instance(inst);
    if(!inst)
    {
        return;
    }

    inst.assign(old_value);
}

auto var_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const var_action_t&>(previous);
    entt::meta_any inst;
    instance(inst);
    entt::meta_any prev_inst;
    prev.instance(prev_inst);
    return inst == prev_inst;
}

void var_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const var_action_t&>(previous);
    old_value.assign(prev.old_value);
}

auto var_action_t::is_valid() const -> bool
{
    entt::meta_any inst;
    instance(inst);
    if(!inst)
    {
        return false;
    }
    return true;
}


} // namespace unravel
