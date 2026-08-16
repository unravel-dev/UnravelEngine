#include "entity_actions.h"
#include "reflection/reflection.h"
#include <editor/editing/editing_manager.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/scene.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/rendering/material.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/meta/ecs/components/script_component.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/ui/ecs/components/ui_document_component.h>
#include <engine/engine.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

namespace
{
auto restore_root_from_serialized_subtree(entt::registry& registry, std::string_view blob) -> entt::handle
{
    if(blob.empty())
    {
        return {};
    }
    // Scratch entity for deserialization. Gameplay never sees it, so its teardown must
    // not announce anything - but it still goes through the funnel, so a partially
    // loaded subtree under it is disposed of the same way as any other.
    scene::scoped_destroy_suppression no_announce;

    entt::entity stub = registry.create();
    entt::handle handle(registry, stub);
    try
    {
        load_from_view(blob, handle);
    }
    catch(const std::exception&)
    {
        scene::destroy_entity(entt::handle(registry, stub));
        return {};
    }
    if(handle)
    {
        if(handle.entity() != stub)
        {
            scene::destroy_entity(entt::handle(registry, stub));
        }
        return handle;
    }
    scene::destroy_entity(entt::handle(registry, stub));
    return {};
}

void collect_subtree_uuid_preorder(entt::const_handle obj, std::vector<hpp::uuid>& out)
{
    if(!obj)
    {
        return;
    }
    auto& trans_comp = obj.get<transform_component>();
    auto* id_comp = obj.try_get<id_component>();
    if(!id_comp || id_comp->id.is_nil())
    {
        return;
    }
    out.push_back(id_comp->id);
    for(auto child : trans_comp.get_children())
    {
        collect_subtree_uuid_preorder(child, out);
    }
}

void apply_subtree_uuid_preorder(entt::handle obj, const std::vector<hpp::uuid>& uids, size_t& next)
{
    if(!obj || next >= uids.size())
    {
        return;
    }
    auto& trans_comp = obj.get<transform_component>();
    auto& id_comp = obj.get_or_emplace<id_component>();
    id_comp.id = uids[next++];
    for(auto child : trans_comp.get_children())
    {
        apply_subtree_uuid_preorder(child, uids, next);
    }
}
} // namespace

create_entities_action_t::create_entities_action_t(std::function<std::vector<entt::handle>()> factory_fn)
    : factory(std::move(factory_fn))
{
    name = "Create Entities";
}

create_entities_action_t::create_entities_action_t(std::function<entt::handle()> factory_fn)
{
    name = "Create Entity";
    auto single = std::move(factory_fn);
    factory = [single = std::move(single)]() -> std::vector<entt::handle>
    {
        auto ent = single ? single() : entt::handle{};
        if(!ent)
        {
            return {};
        }
        return {ent};
    };
}

void create_entities_action_t::do_action()
{
    if(!captured)
    {
        if(!factory)
        {
            return;
        }

        auto produced = factory();
        // Collapse to top-level only in case the factory returns a redundant parent/child pair.
        auto roots = transform_component::get_top_level_entities(produced);

        serialized_roots.reserve(roots.size());
        root_entities.reserve(roots.size());
        parent_entities.reserve(roots.size());
        subtree_uuids.reserve(roots.size());

        for(auto root : roots)
        {
            if(!root)
            {
                continue;
            }
            auto* id_comp = root.try_get<id_component>();
            if(!id_comp || id_comp->id.is_nil())
            {
                continue;
            }

            entt::uhandle parent_uh{};
            if(auto* tr = root.try_get<transform_component>())
            {
                auto parent = tr->get_parent();
                if(parent && parent.try_get<id_component>())
                {
                    parent_uh = entt::make_uhandle(parent);
                }
            }

            subtree_uuids.emplace_back();
            collect_subtree_uuid_preorder(static_cast<entt::const_handle>(root), subtree_uuids.back());
            if(subtree_uuids.back().empty())
            {
                subtree_uuids.pop_back();
                continue;
            }

            std::stringstream ss;
            {
                // Undo snapshot: replayed by this process and never shown to anyone.
                serialization::scoped_output_format compact(serialization::output_format::compact);
                save_to_stream(ss, static_cast<entt::const_handle>(root));
            }
            serialized_roots.emplace_back(ss.str());
            root_entities.emplace_back(entt::make_uhandle(root));
            parent_entities.emplace_back(parent_uh);
        }

        captured = true;
        // Release any state held by the factory - subsequent redos restore from the snapshot.
        factory = {};
        return;
    }

    // Redo path: recreate each root from its serialized snapshot (mirrors delete_entities_action_t::undo_action).
    if(root_entities.empty() || !root_entities.front().registry || serialized_roots.empty())
    {
        return;
    }
    entt::registry& registry = *root_entities.front().registry;
    for(size_t i = 0; i < serialized_roots.size(); ++i)
    {
        auto restored = restore_root_from_serialized_subtree(registry, serialized_roots[i]);
        if(!restored || i >= parent_entities.size())
        {
            continue;
        }
        if(i < subtree_uuids.size() && !subtree_uuids[i].empty())
        {
            size_t next_uid = 0;
            apply_subtree_uuid_preorder(restored, subtree_uuids[i], next_uid);
        }
        const auto& parent_uh = parent_entities[i];
        if(!parent_uh.registry || parent_uh.uuid.is_nil())
        {
            continue;
        }
        auto* tr = restored.try_get<transform_component>();
        if(!tr)
        {
            continue;
        }
        auto desired_parent = parent_uh.resolve();
        if(!desired_parent)
        {
            continue;
        }
        auto current_parent = tr->get_parent();
        if(!current_parent || current_parent != desired_parent)
        {
            tr->set_parent(desired_parent, false);
        }
    }
}

void create_entities_action_t::undo_action()
{
    if(!captured)
    {
        return;
    }

    auto& ctx = engine::context();
    auto& em = ctx.get_cached<editing_manager>();

    // Re-snapshot each root before destroying it so that any out-of-band edits made since creation
    // (or since the last redo) are preserved on the next redo - parent may also have changed.
    for(size_t i = 0; i < root_entities.size(); ++i)
    {
        auto target = root_entities[i].resolve();
        if(!target)
        {
            continue;
        }

        entt::uhandle parent_uh{};
        if(auto* tr = target.try_get<transform_component>())
        {
            auto parent = tr->get_parent();
            if(parent && parent.try_get<id_component>())
            {
                parent_uh = entt::make_uhandle(parent);
            }
        }
        if(i < parent_entities.size())
        {
            parent_entities[i] = parent_uh;
        }

        if(i < subtree_uuids.size())
        {
            subtree_uuids[i].clear();
            collect_subtree_uuid_preorder(static_cast<entt::const_handle>(target), subtree_uuids[i]);
        }
        if(i < serialized_roots.size())
        {
            std::stringstream ss;
            {
                serialization::scoped_output_format compact(serialization::output_format::compact);
                save_to_stream(ss, static_cast<entt::const_handle>(target));
            }
            serialized_roots[i] = ss.str();
        }

        em.unselect(target);
        prefab_override_context::mark_entity_as_removed(target);
        scene::destroy_entity(target);
    }
}

auto create_entities_action_t::is_mergeable(const editing_action_t& /*previous*/) const -> bool
{
    return false;
}

auto create_entities_action_t::is_valid() const -> bool
{
    if(!captured)
    {
        // Pre-execution: valid as long as we have a factory to run.
        return static_cast<bool>(factory);
    }
    if(serialized_roots.empty() || serialized_roots.size() != root_entities.size()
       || parent_entities.size() != root_entities.size())
    {
        return false;
    }
    if(subtree_uuids.size() != serialized_roots.size())
    {
        return false;
    }
    for(const auto& root_uh : root_entities)
    {
        if(!root_uh.registry || root_uh.uuid.is_nil())
        {
            return false;
        }
    }
    for(const auto& uids : subtree_uuids)
    {
        if(uids.empty())
        {
            return false;
        }
    }
    return true;
}

delete_entities_action_t::delete_entities_action_t(std::vector<entt::handle> entities)
{
    name = "Delete Entities";
    if(entities.empty())
    {
        return;
    }

    auto roots = transform_component::get_top_level_entities(entities);
    serialized_roots.reserve(roots.size());
    root_entities.reserve(roots.size());
    parent_entities.reserve(roots.size());
    subtree_uuids.reserve(roots.size());
    for(auto root : roots)
    {
        if(!root)
        {
            continue;
        }
        auto* id_comp = root.try_get<id_component>();
        if(!id_comp || id_comp->id.is_nil())
        {
            continue;
        }
        entt::uhandle parent_uh{};
        if(auto* tr = root.try_get<transform_component>())
        {
            auto parent = tr->get_parent();
            if(parent && parent.try_get<id_component>())
            {
                parent_uh = entt::make_uhandle(parent);
            }
        }
        subtree_uuids.emplace_back();
        collect_subtree_uuid_preorder(static_cast<entt::const_handle>(root), subtree_uuids.back());
        if(subtree_uuids.back().empty())
        {
            subtree_uuids.pop_back();
            continue;
        }
        std::stringstream ss;
        {
            serialization::scoped_output_format compact(serialization::output_format::compact);
            save_to_stream(ss, static_cast<entt::const_handle>(root));
        }
        serialized_roots.emplace_back(ss.str());
        root_entities.emplace_back(entt::make_uhandle(root));
        parent_entities.emplace_back(parent_uh);
    }
}

void delete_entities_action_t::do_action()
{
    if(!is_valid())
    {
        return;
    }
    auto& ctx = engine::context();
    auto& em = ctx.get_cached<editing_manager>();
    for(const auto& root_uh : root_entities)
    {
        auto target = root_uh.resolve();
        if(!target)
        {
            continue;
        }
        em.unselect(target);
        prefab_override_context::mark_entity_as_removed(target);
        scene::destroy_entity(target);
    }
}

void delete_entities_action_t::undo_action()
{
    if(root_entities.empty() || !root_entities.front().registry || serialized_roots.empty())
    {
        return;
    }
    entt::registry& registry = *root_entities.front().registry;
    for(size_t i = 0; i < serialized_roots.size(); ++i)
    {
        auto restored = restore_root_from_serialized_subtree(registry, serialized_roots[i]);
        if(!restored || i >= parent_entities.size())
        {
            continue;
        }
        if(i < subtree_uuids.size() && !subtree_uuids[i].empty())
        {
            size_t next_uid = 0;
            apply_subtree_uuid_preorder(restored, subtree_uuids[i], next_uid);
        }
        const auto& parent_uh = parent_entities[i];
        if(!parent_uh.registry || parent_uh.uuid.is_nil())
        {
            continue;
        }
        auto* tr = restored.try_get<transform_component>();
        if(!tr)
        {
            continue;
        }
        auto desired_parent = parent_uh.resolve();
        if(!desired_parent)
        {
            continue;
        }
        auto current_parent = tr->get_parent();
        if(!current_parent || current_parent != desired_parent)
        {
            tr->set_parent(desired_parent, false);
        }
    }
}

auto delete_entities_action_t::is_valid() const -> bool
{
    if(serialized_roots.empty() || serialized_roots.size() != root_entities.size()
       || parent_entities.size() != root_entities.size())
    {
        return false;
    }
    if(subtree_uuids.size() != serialized_roots.size())
    {
        return false;
    }
    for(const auto& root_uh : root_entities)
    {
        if(!root_uh.registry || root_uh.uuid.is_nil())
        {
            return false;
        }
    }
    for(const auto& uids : subtree_uuids)
    {
        if(uids.empty())
        {
            return false;
        }
    }
    return true;
}

auto delete_entities_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    return false;
}

entity_add_component_action_t::entity_add_component_action_t(entt::handle ent, const entt::meta_type& ctype)
    : entity(entt::make_uhandle(ent)), component_type(ctype)
{
    name = "Add Component " + entt::get_pretty_name(component_type);
}

void entity_add_component_action_t::do_action()
{
    if(auto ent = entity.resolve())
    {
        do_was_successful = component_type.invoke("component_add"_hs, {}, ent).cast<bool>();
    }
}

void entity_add_component_action_t::undo_action()
{
    if(auto ent = entity.resolve())
    {
        component_type.invoke("component_remove"_hs, {}, ent);
    }
}

auto entity_add_component_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    return false;
}

auto entity_add_component_action_t::is_valid() const -> bool
{
    auto ent = entity.resolve();
    return ent.valid() && component_type;
}

void entity_add_component_action_t::draw_in_inspector(rtti::context& ctx)
{
    //draw_in_inspector_impl(ctx, component_type, component_type, {});
}

entity_remove_component_action_t::entity_remove_component_action_t(entt::handle ent, const entt::meta_type& ctype)
    : entity(entt::make_uhandle(ent)), component_type(ctype)
{
    name = "Remove Component " + entt::get_pretty_name(component_type);
}

void entity_remove_component_action_t::do_action()
{
    if(auto ent = entity.resolve())
    {
        stream = {};
        component_type.invoke("component_save"_hs, {}, ent, entt::forward_as_meta(stream));
        do_was_successful = component_type.invoke("component_remove"_hs, {}, ent).cast<bool>();
    }
}

void entity_remove_component_action_t::undo_action()
{
    if(auto ent = entity.resolve())
    {
        component_type.invoke("component_add"_hs, {}, ent);
        stream.seekg(0);
        component_type.invoke("component_load"_hs, {}, ent, entt::forward_as_meta(stream));
    }
}

auto entity_remove_component_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    return false;
}

auto entity_remove_component_action_t::is_valid() const -> bool
{
    auto ent = entity.resolve();
    return ent.valid() && component_type;
}

void entity_remove_component_action_t::draw_in_inspector(rtti::context& ctx)
{
    //draw_in_inspector_impl(ctx, component_type, component_type, {});
}

// Transform Move Action Implementation
entity_set_active_action_t::entity_set_active_action_t(entt::handle ent, bool old_active, bool new_active)
    : entity(entt::make_uhandle(ent)), old_active(old_active), new_active(new_active)
{
    name = "Set Active";
}

void entity_set_active_action_t::do_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_active(new_active);
            prefab_override_context::mark_active_as_changed(ent);
        }
    }
}

void entity_set_active_action_t::undo_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto transform = ent.try_get<transform_component>())
        {
            transform->set_active(old_active);
            prefab_override_context::mark_active_as_changed(ent);
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
    auto ent = entity.resolve();
    return ent.valid() && ent.try_get<transform_component>();
}

void entity_set_active_action_t::draw_in_inspector(rtti::context& ctx)
{
    draw_in_inspector_impl(ctx, old_active, new_active, {});
}

entity_set_name_action_t::entity_set_name_action_t(entt::handle ent, const std::string& old_name, const std::string& new_name)
    : entity(entt::make_uhandle(ent)), old_name(old_name), new_name(new_name)
{
    name = "Set Name";
}

void entity_set_name_action_t::do_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto tag = ent.try_get<tag_component>())
        {
            tag->name = new_name;
            prefab_override_context::mark_property_as_changed(ent, entt::resolve<tag_component>(), "name");
        }
    }
}

void entity_set_name_action_t::undo_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto tag = ent.try_get<tag_component>())
        {
            tag->name = old_name;
            prefab_override_context::mark_property_as_changed(ent, entt::resolve<tag_component>(), "name");
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
    auto ent = entity.resolve();
    return ent.valid() && ent.try_get<tag_component>();
}

entity_set_tag_action_t::entity_set_tag_action_t(entt::handle ent, const std::string& old_tag, const std::string& new_tag)  
    : entity(entt::make_uhandle(ent)), old_tag(old_tag), new_tag(new_tag)
{
    name = "Set Tag";
}
    
void entity_set_tag_action_t::do_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto tag = ent.try_get<tag_component>())
        {
            tag->tag = new_tag;
            prefab_override_context::mark_property_as_changed(ent, entt::resolve<tag_component>(), "tag");
        }
    }
}

void entity_set_tag_action_t::undo_action()
{
    if(auto ent = entity.resolve())
    {
        if(auto tag = ent.try_get<tag_component>())
        {
            tag->tag = old_tag;
            prefab_override_context::mark_property_as_changed(ent, entt::resolve<tag_component>(), "tag");
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
    auto ent = entity.resolve();
    return ent.valid() && ent.try_get<tag_component>();
}

void entity_set_tag_action_t::draw_in_inspector(rtti::context& ctx)
{
    draw_in_inspector_impl(ctx, old_tag, new_tag, {});
}

entity_set_materials_action_t::entity_set_materials_action_t(entt::handle ent, const std::vector<asset_handle<material>>& old_materials, const asset_handle<material>& new_material)
    : entity(entt::make_uhandle(ent)), old_materials(old_materials)
{
    new_materials.resize(old_materials.size(), new_material);
    name = "Set Materials";
}

void entity_set_materials_action_t::do_action()
{
    if(auto ent = entity.resolve(); ent && ent.all_of<model_component>())
    {
        auto& model_comp = ent.get<model_component>();
        auto model_copy = model_comp.get_model();
        for(size_t i = 0; i < new_materials.size() && i < model_copy.get_materials().size(); ++i)
        {
            model_copy.set_material(new_materials[i], i);
        }
        model_comp.set_model(model_copy);
        prefab_override_context::mark_material_as_changed(ent);
    }
}

void entity_set_materials_action_t::undo_action()
{
    if(auto ent = entity.resolve(); ent && ent.all_of<model_component>() && !old_materials.empty())
    {
        auto& model_comp = ent.get<model_component>();
        auto model_copy = model_comp.get_model();
        for(size_t i = 0; i < old_materials.size() && i < model_copy.get_materials().size(); ++i)
        {
            model_copy.set_material(old_materials[i], i);
        }
        model_comp.set_model(model_copy);
        prefab_override_context::mark_material_as_changed(ent);
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
    auto ent = entity.resolve();
    return ent.valid() && ent.all_of<model_component>() && new_materials.size() == old_materials.size()
           && new_materials.size() > 0 && new_materials[0].is_valid();
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
    : entity(entt::make_uhandle(ent)), old_area(old_area), new_area(new_area)
{
    name = "Set Text Bounds";
}

void entity_set_text_bounds_action_t::do_action()
{
    if(auto ent = entity.resolve(); ent && ent.all_of<text_component>())
    {
        auto text_comp = ent.try_get<text_component>();
        if(text_comp)
        {
            text_comp->set_area(new_area);
            prefab_override_context::mark_text_area_as_changed(ent);
        }
    }
}

void entity_set_text_bounds_action_t::undo_action()
{
    if(auto ent = entity.resolve(); ent && ent.all_of<text_component>())
    {
        auto text_comp = ent.try_get<text_component>();
        if(text_comp)
        {
            text_comp->set_area(old_area);
            prefab_override_context::mark_text_area_as_changed(ent);
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
    auto ent = entity.resolve();
    return ent.valid() && ent.all_of<text_component>();
}

void entity_set_text_bounds_action_t::draw_in_inspector(rtti::context& ctx)
{
    entt::meta_any old_area_any = old_area;
    entt::meta_any new_area_any = new_area;
    draw_in_inspector_impl(ctx, old_area_any, new_area_any, {});
}


entity_set_ui_document_component_bounds_action_t::entity_set_ui_document_component_bounds_action_t(entt::handle ent, const usize32_t& old_size, const usize32_t& new_size)
    : entity(entt::make_uhandle(ent)), old_size(old_size), new_size(new_size)
{
    name = "Set UI Document Component Bounds";
}

void entity_set_ui_document_component_bounds_action_t::do_action()
{
    if(auto ent = entity.resolve(); ent && ent.all_of<ui_document_component>())
    {
        auto ui_document_comp = ent.try_get<ui_document_component>();
        if(ui_document_comp)
        {
            ui_document_comp->size = new_size;
            prefab_override_context::mark_ui_document_size_as_changed(ent);
        }
    }
}

void entity_set_ui_document_component_bounds_action_t::undo_action()
{
    if(auto ent = entity.resolve(); ent && ent.all_of<ui_document_component>())
    {
        auto ui_document_comp = ent.try_get<ui_document_component>();
        if(ui_document_comp)
        {
            ui_document_comp->size = old_size;
            prefab_override_context::mark_ui_document_size_as_changed(ent);
        }
    }
}

auto entity_set_ui_document_component_bounds_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const entity_set_ui_document_component_bounds_action_t&>(previous);
    return entity == prev.entity;
}

void entity_set_ui_document_component_bounds_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const entity_set_ui_document_component_bounds_action_t&>(previous);
    old_size = prev.old_size;
}

auto entity_set_ui_document_component_bounds_action_t::is_valid() const -> bool
{
    auto ent = entity.resolve();
    return ent.valid() && ent.all_of<ui_document_component>();
}

void entity_set_ui_document_component_bounds_action_t::draw_in_inspector(rtti::context& ctx)
{
    entt::meta_any old_size_any = old_size;
    entt::meta_any new_size_any = new_size;
    draw_in_inspector_impl(ctx, old_size_any, new_size_any, {});
}

// Script component action implementations
entity_add_script_component_action_t::entity_add_script_component_action_t(entt::handle ent, const std::string& type_name)
    : entity(entt::make_uhandle(ent)), script_type_name(type_name)
{
    name = "Add Script Component " + script_type_name;
}

void entity_add_script_component_action_t::do_action()
{
    if(auto ent = entity.resolve(); ent && !script_type_name.empty())
    {
        auto& ctx = engine::context();
        auto& script_sys = ctx.get_cached<script_system>();
        auto script_type = script_sys.get_type_by_fullname(script_type_name);

        if(script_type.valid())
        {
            auto script_comp = ent.try_get<script_component>();
            if(!script_comp)
            {
                script_comp = &ent.emplace<script_component>();
            }
            auto script_obj = script_comp->add_script_component(script_type);
            do_was_successful = script_obj.pinned != nullptr;
        }
    }
}

void entity_add_script_component_action_t::undo_action()
{
    if(auto ent = entity.resolve(); ent && !script_type_name.empty() && do_was_successful)
    {
        auto& ctx = engine::context();
        auto& script_sys = ctx.get_cached<script_system>();
        auto script_type = script_sys.get_type_by_fullname(script_type_name);

        if(script_type.valid())
        {
            auto script_comp = ent.try_get<script_component>();
            if(script_comp)
            {
                script_comp->remove_script_component(script_type);
                script_comp->process_pending_deletions();
            }
        }
    }
}

auto entity_add_script_component_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    return false;
}

auto entity_add_script_component_action_t::is_valid() const -> bool
{
    auto ent = entity.resolve();
    return ent.valid() && !script_type_name.empty();
}

void entity_add_script_component_action_t::draw_in_inspector(rtti::context& ctx)
{
    // Could implement visual representation if needed
}

entity_remove_script_component_action_t::entity_remove_script_component_action_t(entt::handle ent, const std::string& type_name, int index)
    : entity(entt::make_uhandle(ent)), script_type_name(type_name), script_index(index)
{
    name = "Remove Script Component " + script_type_name;
    if (index >= 0)
    {
        name += " [" + std::to_string(index) + "]";
    }
}

void entity_remove_script_component_action_t::do_action()
{
    if(auto ent = entity.resolve(); ent && !script_type_name.empty())
    {
        auto& ctx = engine::context();
        auto& script_sys = ctx.get_cached<script_system>();
        auto script_type = script_sys.get_type_by_fullname(script_type_name);

        if(script_type.valid())
        {
            auto script_comp = ent.try_get<script_component>();
            if (script_comp)
            {
                const auto& comps = script_comp->get_script_components();
                
                // If index is specified and valid, remove the specific component at that index
                if (script_index >= 0 && script_index < static_cast<int>(comps.size()))
                {
                    const auto& script_obj = comps[script_index];
                    
                    // Verify this is the correct type
                    if (script_obj.pinned)
                    {
                        auto obj = script_obj.pinned->get_object();
                        if(obj.get_type().get_fullname() == script_type_name)
                        {
                            // Serialize the script object before removing it
                            removed_script_object_data = {};
                            save_to_stream(removed_script_object_data, ent, script_obj);
                            
                            // Remove the specific script component instance
                            do_was_successful = script_comp->remove_script_component(obj);
                            if (do_was_successful)
                            {
                                script_comp->process_pending_deletions();
                            }
                        }
                    }
                }
                else
                {
                    // Fallback to old behavior: remove first component of this type
                    auto script_obj = script_comp->get_script_component(script_type);
                    if (script_obj.pinned)
                    {
                        // Serialize the script object before removing it
                        removed_script_object_data = {};
                        save_to_stream(removed_script_object_data, ent, script_obj);
                    }
                    
                    do_was_successful = script_comp->remove_script_component(script_type);
                    if (do_was_successful)
                    {
                        script_comp->process_pending_deletions();
                    }
                }
            }
        }
    }
}

void entity_remove_script_component_action_t::undo_action()
{
    if(auto ent = entity.resolve(); ent && !script_type_name.empty() && do_was_successful)
    {
        auto& ctx = engine::context();
        auto& script_sys = ctx.get_cached<script_system>();
        auto script_type = script_sys.get_type_by_fullname(script_type_name);

        if(script_type.valid())
        {
            auto& script_comp = ent.get_or_emplace<script_component>();
            if(!removed_script_object_data.str().empty())
            {
                try
                {
                    script_component::script_object restored_obj;
                    removed_script_object_data.seekg(0);
                    load_from_stream(removed_script_object_data, ent, restored_obj);

                    // Add the restored script object
                    script_comp.add_script_component(restored_obj);

                }
                catch(...)
                {
                    // Fallback: create new instance if deserialization fails
                    script_comp.add_script_component(script_type);
                }
            }
            else
            {
                // Fallback: create new instance of the type
                script_comp.add_script_component(script_type);
            }
        }
    }
}

auto entity_remove_script_component_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    return false;
}

auto entity_remove_script_component_action_t::is_valid() const -> bool
{
    auto ent = entity.resolve();
    return ent.valid() && !script_type_name.empty();
}

void entity_remove_script_component_action_t::draw_in_inspector(rtti::context& ctx)
{
    // Could implement visual representation if needed
}

} // namespace unravel
