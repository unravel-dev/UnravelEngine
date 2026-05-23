#include "transform_actions.h"
#include "base/basetypes.hpp"
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <engine/ecs/scene.h>

namespace unravel
{

// Transform Move Action Implementation
transform_move_action_t::transform_move_action_t(entt::handle ent, const math::vec3& old_pos, const math::vec3& new_pos)
    : entity(entt::make_uhandle(ent)), old_position(old_pos), new_position(new_pos)
{
    name = "Position";
}

void transform_move_action_t::do_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_position_local(new_position);
            prefab_override_context::mark_transform_as_changed(ent, true, false, false, false);
        }
    }
}

void transform_move_action_t::undo_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_position_local(old_position);
            prefab_override_context::mark_transform_as_changed(ent, true, false, false, false);
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
    auto ent = entity.resolve();
    return ent.valid() && ent.try_get<transform_component>();
}


void transform_move_action_t::draw_in_inspector(rtti::context& ctx)
{
    auto custom = entt::make_custom<entt::attributes>(entt::attributes{
        {"name", "position"}, 
        {"pretty_name", "Position"}}
    );
    draw_in_inspector_impl(ctx, old_position, new_position, custom);
}

// Transform Move Global Action Implementation
transform_move_global_action_t::transform_move_global_action_t(entt::handle ent, const math::vec3& old_pos, const math::vec3& new_pos)
    : entity(entt::make_uhandle(ent)), old_position(old_pos), new_position(new_pos)
{
    name = "Global Position";
}

void transform_move_global_action_t::do_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_position_global(new_position);
            prefab_override_context::mark_transform_global_as_changed(ent, true, false, false, false);
        }
    }
}

void transform_move_global_action_t::undo_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_position_global(old_position);
            prefab_override_context::mark_transform_global_as_changed(ent, true, false, false, false);
        }
    }
}

auto transform_move_global_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const transform_move_global_action_t&>(previous);
    return entity == prev.entity && name == prev.name;
}

void transform_move_global_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const transform_move_global_action_t&>(previous);
    old_position = prev.old_position; // Extend the range of the move
}

auto transform_move_global_action_t::is_valid() const -> bool
{
    auto ent = entity.resolve();
    return ent.valid() && ent.try_get<transform_component>();
}

void transform_move_global_action_t::draw_in_inspector(rtti::context& ctx)
{
    auto custom = entt::make_custom<entt::attributes>(entt::attributes{
        {"name", "global_position"}, 
        {"pretty_name", "Global Position"}}
    );
    draw_in_inspector_impl(ctx, old_position, new_position, custom);
}

// Transform Rotate Action Implementation
transform_rotate_action_t::transform_rotate_action_t(entt::handle ent, const math::quat& old_rot, const math::quat& new_rot)
    : entity(entt::make_uhandle(ent)), old_rotation(old_rot), new_rotation(new_rot)
{
    name = "Rotation";
}

void transform_rotate_action_t::do_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_rotation_local(new_rotation);
            prefab_override_context::mark_transform_as_changed(ent, false, true, false, false);
        }
    }
}

void transform_rotate_action_t::undo_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_rotation_local(old_rotation);
            prefab_override_context::mark_transform_as_changed(ent, false, true, false, false);
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
    auto ent = entity.resolve();
    return ent.valid() && ent.try_get<transform_component>();
}

void transform_rotate_action_t::draw_in_inspector(rtti::context& ctx)
{
    auto custom = entt::make_custom<entt::attributes>(entt::attributes{
        {"name", "rotation"}, 
        {"pretty_name", "Rotation"}}
    );
    draw_in_inspector_impl(ctx, old_rotation, new_rotation, custom);
}


// Transform Scale Action Implementation
transform_scale_action_t::transform_scale_action_t(entt::handle ent, const math::vec3& old_sc, const math::vec3& new_sc)
    : entity(entt::make_uhandle(ent)), old_scale(old_sc), new_scale(new_sc)
{
    name = "Scale";
}

void transform_scale_action_t::do_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_scale_local(new_scale);
            prefab_override_context::mark_transform_as_changed(ent, false, false, true, false);
        }
    }
}

void transform_scale_action_t::undo_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_scale_local(old_scale);
            prefab_override_context::mark_transform_as_changed(ent, false, false, true, false);
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
    auto ent = entity.resolve();
    return ent.valid() && ent.try_get<transform_component>();
}

void transform_scale_action_t::draw_in_inspector(rtti::context& ctx)
{
    auto custom = entt::make_custom<entt::attributes>(entt::attributes{
        {"name", "scale"}, 
        {"pretty_name", "Scale"}}
    );
    draw_in_inspector_impl(ctx, old_scale, new_scale, custom);
}


// Transform Skew Action Implementation
transform_skew_action_t::transform_skew_action_t(entt::handle ent, const math::vec3& old_sk, const math::vec3& new_sk)
    : entity(entt::make_uhandle(ent)), old_skew(old_sk), new_skew(new_sk)
{
    name = "Skew";
}

void transform_skew_action_t::do_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_skew_local(new_skew);
            prefab_override_context::mark_transform_as_changed(ent, false, false, false, true);
        }
    }
}

void transform_skew_action_t::undo_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_skew_local(old_skew);
            prefab_override_context::mark_transform_as_changed(ent, false, false, false, true);
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
    auto ent = entity.resolve();
    return ent.valid() && ent.try_get<transform_component>();
}

void transform_skew_action_t::draw_in_inspector(rtti::context& ctx)
{
    auto custom = entt::make_custom<entt::attributes>(entt::attributes{
        {"name", "skew"}, 
        {"pretty_name", "Skew"}}
    );
    draw_in_inspector_impl(ctx, old_skew, new_skew, custom);
}

// Transform Set Parent Action Implementation
transform_set_parent_action_t::transform_set_parent_action_t(entt::handle ent, entt::handle old_p, entt::handle new_p)
    : transform_set_parent_action_t(entt::make_uhandle(ent), entt::make_uhandle(old_p), entt::make_uhandle(new_p))
{
    
}

transform_set_parent_action_t::transform_set_parent_action_t(entt::uhandle ent, entt::uhandle old_p, entt::uhandle new_p)
    : entity(ent), old_parent(old_p), new_parent(new_p)
{
    if(new_p.resolve().valid())
    {
        name = "Set Parent";
    }
    else
    {
        name = "Remove Parent";
    }
}

void transform_set_parent_action_t::do_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_parent(new_parent.resolve(), true);
            prefab_override_context::mark_transform_as_changed(ent, true, true, true, true);
        }
    }
}

void transform_set_parent_action_t::undo_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_parent(old_parent.resolve(), true);
            prefab_override_context::mark_transform_as_changed(ent, true, true, true, true);
        }
    }
}

auto transform_set_parent_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    return false;
}

void transform_set_parent_action_t::merge_with(const editing_action_t& previous) {}

auto transform_set_parent_action_t::is_valid() const -> bool
{
    auto ent = entity.resolve();
    return ent.valid() && ent.try_get<transform_component>();
}

void transform_set_parent_action_t::draw_in_inspector(rtti::context& ctx)
{
    auto custom = entt::make_custom<entt::attributes>(entt::attributes{
        {"name", "parent"},
        {"pretty_name", "Parent"}});
    draw_in_inspector_impl(ctx, entt::meta_any{std::in_place_type<entt::handle>, old_parent.resolve()},
                          entt::meta_any{std::in_place_type<entt::handle>, new_parent.resolve()}, custom);
}

} // namespace unravel
