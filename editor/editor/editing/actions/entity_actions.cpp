#include "entity_actions.h"
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/tag_component.h>

namespace unravel
{

entity_add_component_action_t::entity_add_component_action_t(entt::handle ent, entt::meta_type component_type)
    : entity(ent), component_type(component_type)
{
    name = "Add Component";
}

void entity_add_component_action_t::do_action()
{
    if (entity)
    {
        do_was_successful = component_type.invoke("component_add"_hs, {}, entity).cast<bool>();
    }
}

void entity_add_component_action_t::undo_action()
{
    if (entity)
    {
        component_type.invoke("component_remove"_hs, {}, entity);
    }
}

auto entity_add_component_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    return false;
}

auto entity_add_component_action_t::is_valid() const -> bool
{
    return entity.valid() && component_type;
}

void entity_add_component_action_t::draw_in_inspector(rtti::context& ctx)
{
    //draw_in_inspector_impl(ctx, component_type, component_type, {});
}

entity_remove_component_action_t::entity_remove_component_action_t(entt::handle ent, entt::meta_type component_type)
    : entity(ent), component_type(component_type)
{
    name = "Remove Component";
}

void entity_remove_component_action_t::do_action()
{
    if (entity)
    {
        do_was_successful = component_type.invoke("component_remove"_hs, {}, entity).cast<bool>();
    }
}

void entity_remove_component_action_t::undo_action()
{
    if (entity)
    {
        component_type.invoke("component_add"_hs, {}, entity);
    }
}

auto entity_remove_component_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    return false;
}

auto entity_remove_component_action_t::is_valid() const -> bool
{
    return entity.valid() && component_type;
}

void entity_remove_component_action_t::draw_in_inspector(rtti::context& ctx)
{
    //draw_in_inspector_impl(ctx, component_type, component_type, {});
}

// Transform Move Action Implementation
entity_set_active_action_t::entity_set_active_action_t(entt::handle ent, bool old_active, bool new_active)
    : entity(ent), old_active(old_active), new_active(new_active)
{
    name = "Set Active";
}

void entity_set_active_action_t::do_action()
{
    if (entity)
    {
        if (auto transform = entity.try_get<transform_component>())
        {
            transform->set_active(new_active);
            prefab_override_context::mark_active_as_changed(entity);
        }
    }
}

void entity_set_active_action_t::undo_action()
{
    if (entity)
    {
        if (auto transform = entity.try_get<transform_component>())
        {
            transform->set_active(old_active);
            prefab_override_context::mark_active_as_changed(entity);
        }
    }
}

auto entity_set_active_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const entity_set_active_action_t&>(previous);
    return entity == prev.entity;
}

void entity_set_active_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const entity_set_active_action_t&>(previous);
    old_active = prev.old_active;
}

auto entity_set_active_action_t::is_valid() const -> bool
{
    return entity.valid() && entity.try_get<transform_component>();
}   

void entity_set_active_action_t::draw_in_inspector(rtti::context& ctx)
{
    draw_in_inspector_impl(ctx, old_active, new_active, {});
}

entity_set_name_action_t::entity_set_name_action_t(entt::handle ent, const std::string& old_name, const std::string& new_name)
    : entity(ent), old_name(old_name), new_name(new_name)
{
    name = "Set Name";
}

void entity_set_name_action_t::do_action()
{
    if (entity)
    {
        if (auto tag = entity.try_get<tag_component>())
        {
            tag->name = new_name;
            prefab_override_context::mark_property_as_changed(entity, entt::resolve<tag_component>(), "name");
        }
    }
}

void entity_set_name_action_t::undo_action()
{
    if (entity)
    {
        if (auto tag = entity.try_get<tag_component>())
        {
            tag->name = old_name;
            prefab_override_context::mark_property_as_changed(entity, entt::resolve<tag_component>(), "name");
        }
    }
}

void entity_set_name_action_t::draw_in_inspector(rtti::context& ctx)
{
    draw_in_inspector_impl(ctx, old_name, new_name, {});
}

auto entity_set_name_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const entity_set_name_action_t&>(previous);
    return entity == prev.entity;
}

void entity_set_name_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const entity_set_name_action_t&>(previous);
    old_name = prev.old_name;
}

auto entity_set_name_action_t::is_valid() const -> bool
{
    return entity.valid() && entity.try_get<tag_component>();
}   

entity_set_tag_action_t::entity_set_tag_action_t(entt::handle ent, const std::string& old_tag, const std::string& new_tag)  
    : entity(ent), old_tag(old_tag), new_tag(new_tag)
{
    name = "Set Tag";
}
    
void entity_set_tag_action_t::do_action()
{
    if (entity)
    {
        if (auto tag = entity.try_get<tag_component>())
        {
            tag->tag = new_tag;
            prefab_override_context::mark_property_as_changed(entity, entt::resolve<tag_component>(), "tag");
        }
    }
}

void entity_set_tag_action_t::undo_action()
{
    if (entity)
    {
        if (auto tag = entity.try_get<tag_component>())
        {
            tag->tag = old_tag;
            prefab_override_context::mark_property_as_changed(entity, entt::resolve<tag_component>(), "tag");
        }
    }
}

auto entity_set_tag_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const entity_set_tag_action_t&>(previous);
    return entity == prev.entity;
}

void entity_set_tag_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const entity_set_tag_action_t&>(previous);
    old_tag = prev.old_tag;
}

auto entity_set_tag_action_t::is_valid() const -> bool
{
    return entity.valid() && entity.try_get<tag_component>();
}

void entity_set_tag_action_t::draw_in_inspector(rtti::context& ctx)
{
    draw_in_inspector_impl(ctx, old_tag, new_tag, {});
}

} // namespace unravel
