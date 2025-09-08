#include "entity_actions.h"
#include "reflection/reflection.h"
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/rendering/material.h>

namespace unravel
{

entity_add_component_action_t::entity_add_component_action_t(entt::handle ent, const entt::meta_type& ctype)
    : entity(ent), component_type(ctype)
{
    name = "Add Component " + entt::get_pretty_name(component_type);
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

entity_remove_component_action_t::entity_remove_component_action_t(entt::handle ent, const entt::meta_type& ctype)
    : entity(ent), component_type(ctype)
{
    name = "Remove Component " + entt::get_pretty_name(component_type);
}

void entity_remove_component_action_t::do_action()
{
    if (entity)
    {
        stream = {};
        component_type.invoke("component_save"_hs, {}, entity, entt::forward_as_meta(stream));
        do_was_successful = component_type.invoke("component_remove"_hs, {}, entity).cast<bool>();
    }
}

void entity_remove_component_action_t::undo_action()
{
    if (entity)
    {
        component_type.invoke("component_add"_hs, {}, entity);
        // std::stringstream stream_copy(stream.str());
        stream.seekg(0);
        component_type.invoke("component_load"_hs, {}, entity, entt::forward_as_meta(stream));
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

entity_set_materials_action_t::entity_set_materials_action_t(entt::handle ent, const std::vector<asset_handle<material>>& old_materials, const asset_handle<material>& new_material)
    : entity(ent), old_materials(old_materials)
{
    new_materials.resize(old_materials.size(), new_material);
    name = "Set Materials";
}

void entity_set_materials_action_t::do_action()
{
    if (entity && entity.all_of<model_component>())
    {
        auto& model_comp = entity.get<model_component>();
        auto model_copy = model_comp.get_model();
        
        // Apply new material to all submeshes
        for(size_t i = 0; i < new_materials.size() && i < model_copy.get_materials().size(); ++i)
        {
            model_copy.set_material(new_materials[i], i);
        }
        
        // Update the model in the component
        model_comp.set_model(model_copy);
        
        // Mark as changed for prefab system
        prefab_override_context::mark_material_as_changed(entity);
    }
}

void entity_set_materials_action_t::undo_action()
{
    if (entity && entity.all_of<model_component>() && !old_materials.empty())
    {
        auto& model_comp = entity.get<model_component>();
        auto model_copy = model_comp.get_model();
        
        // Restore original materials
        for(size_t i = 0; i < old_materials.size() && i < model_copy.get_materials().size(); ++i)
        {
            model_copy.set_material(old_materials[i], i);
        }
        
        // Update the model in the component
        model_comp.set_model(model_copy);
        
        // Mark as changed for prefab system
        prefab_override_context::mark_material_as_changed(entity);
    }
}

auto entity_set_materials_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const entity_set_materials_action_t&>(previous);
    return entity == prev.entity;
}

void entity_set_materials_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const entity_set_materials_action_t&>(previous);
    old_materials = prev.old_materials;
}

auto entity_set_materials_action_t::is_valid() const -> bool
{
    return entity.valid() && entity.all_of<model_component>() && new_materials.size() == old_materials.size() && new_materials.size() > 0 && new_materials[0].is_valid();
}

void entity_set_materials_action_t::draw_in_inspector(rtti::context& ctx)
{

    entt::meta_any old_materials_any = old_materials;
    entt::meta_any new_material_any = new_materials;
    draw_in_inspector_impl(ctx, old_materials_any, new_material_any, {});

    // For now, we'll keep this simple since materials are complex objects
    // Could be enhanced to show material names/paths in the future
}

entity_set_text_bounds_action_t::entity_set_text_bounds_action_t(entt::handle ent, const fsize_t& old_area, const fsize_t& new_area)
    : entity(ent), old_area(old_area), new_area(new_area)
{
    name = "Set Text Bounds";
}

void entity_set_text_bounds_action_t::do_action()
{
    if (entity && entity.all_of<text_component>())
    {
        // Update the text area
        auto text_comp = entity.try_get<text_component>();
        if (text_comp)
        {
            text_comp->set_area(new_area);
            prefab_override_context::mark_text_area_as_changed(entity);
        }
    }
}

void entity_set_text_bounds_action_t::undo_action()
{
    if (entity && entity.all_of<text_component>())
    {
        // Restore the text area
        auto text_comp = entity.try_get<text_component>();
        if (text_comp)
        {
            text_comp->set_area(old_area);
            prefab_override_context::mark_text_area_as_changed(entity);
        }
    }
}

auto entity_set_text_bounds_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const entity_set_text_bounds_action_t&>(previous);
    return entity == prev.entity;
}

void entity_set_text_bounds_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const entity_set_text_bounds_action_t&>(previous);
    old_area = prev.old_area;
}

auto entity_set_text_bounds_action_t::is_valid() const -> bool
{
    return entity.valid() && entity.all_of<text_component>();
}

void entity_set_text_bounds_action_t::draw_in_inspector(rtti::context& ctx)
{
    entt::meta_any old_area_any = old_area;
    entt::meta_any new_area_any = new_area;
    draw_in_inspector_impl(ctx, old_area_any, new_area_any, {});
}

} // namespace unravel
