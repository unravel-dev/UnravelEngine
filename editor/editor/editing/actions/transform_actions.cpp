#include "transform_actions.h"
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
    if (entity && entity.valid())
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
    if (entity && entity.valid())
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

// Transform Rotate Action Implementation
transform_rotate_action_t::transform_rotate_action_t(entt::handle ent, const math::quat& old_rot, const math::quat& new_rot)
    : entity(ent), old_rotation(old_rot), new_rotation(new_rot)
{
    name = "Rotate Transform";
}

void transform_rotate_action_t::do_action()
{
    if (entity && entity.valid())
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
    if (entity && entity.valid())
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

// Transform Scale Action Implementation
transform_scale_action_t::transform_scale_action_t(entt::handle ent, const math::vec3& old_sc, const math::vec3& new_sc)
    : entity(ent), old_scale(old_sc), new_scale(new_sc)
{
    name = "Scale Transform";
}

void transform_scale_action_t::do_action()
{
    if (entity && entity.valid())
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
    if (entity && entity.valid())
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

// Transform Skew Action Implementation
transform_skew_action_t::transform_skew_action_t(entt::handle ent, const math::vec3& old_sk, const math::vec3& new_sk)
    : entity(ent), old_skew(old_sk), new_skew(new_sk)
{
    name = "Skew Transform";
}

void transform_skew_action_t::do_action()
{
    if (entity && entity.valid())
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
    if (entity && entity.valid())
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

// Property Action Implementation
property_action_t::property_action_t(entt::meta_any& inst, entt::meta_data dat, const entt::meta_any& old_val, const entt::meta_any& new_val)
    : instance(inst.as_ref()), data(dat), old_value(old_val), new_value(new_val)
{
    name = "Property Action " + entt::get_pretty_name(data);
}

void property_action_t::do_action()
{
    data.set(instance, new_value);
}

void property_action_t::undo_action()
{
    data.set(instance, old_value);
}

auto property_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const property_action_t&>(previous);
    return instance == prev.instance && data == prev.data;
}

void property_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const property_action_t&>(previous);
    old_value = prev.old_value;
}

} // namespace unravel
