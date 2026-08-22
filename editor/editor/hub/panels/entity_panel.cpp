#include "entity_panel.h"
#include "panel.h"
#include "reflection/reflection.h"
#include <memory>
#include <editor/editing/editing_manager.h>
#include <editor/editing/actions/entity_actions.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/engine.h>
#include <editor/editing/authoring_root.h>


namespace unravel
{

entity_panel::entity_panel(imgui_panels* parent, const char* name) : panel_base(name), parent_(parent)
{
}

void entity_panel::duplicate_entities(const std::vector<entt::handle>& entities)
{
    auto& ctx = engine::context();
    auto& em = ctx.get_cached<editing_manager>();
    em.queue_action("Duplicate Entities",
        [entities]() mutable
        {
            auto& ctx = engine::context();
            auto& ec = ctx.get_cached<ecs>();
            auto& em = ctx.get_cached<editing_manager>();
            em.unselect(false);

            // Get the active scene based on edit mode
            auto* active_scene = em.get_active_scene(ctx);
            if(!active_scene)
            {
                return;
            }

            for(auto entity : entities)
            {
                if(!entity.valid())
                {
                    return;
                }
                auto object = active_scene->clone_entity(entity);

                em.select(object, editing_manager::select_mode::shift);
            }
        });
}

void entity_panel::focus_entities(entt::handle camera, const std::vector<entt::handle>& entities)
{
    auto& ctx = engine::context();
    auto& em = ctx.get_cached<editing_manager>();
    em.queue_action("Focus Entities",
        [camera, entities]() mutable
        {
            defaults::focus_camera_on_entities(camera, entities, 0.4f);
        });
}

void entity_panel::delete_entities(const std::vector<entt::handle>& entities)
{
    auto& ctx = engine::context();
    auto& em = ctx.get_cached<editing_manager>();
    
    em.push_undo_stack_enabled(true);
    for(auto entity : entities)
    {
        em.unselect(entity);
    }
    em.queue_action("Delete Entities", std::make_shared<delete_entities_action_t>(entities));
    em.pop_undo_stack_enabled();
}


/**
 * @brief Gets the entity name from tag component
 * @param entity The entity to get the name for
 * @return The entity name, or a fallback string if no name is set
 */
auto entity_panel::get_entity_name(entt::handle entity) -> std::string
{
    if(!entity)
    {
        return "Unknown";
    }

    auto* tag_comp = entity.try_get<tag_component>();
    if(tag_comp && !tag_comp->name.empty())
    {
        return tag_comp->name;
    }

    // Fallback to entity ID if no name
    return "Entity_" + std::to_string(static_cast<uint32_t>(entity.entity()));
}

auto entity_panel::get_entity_icon(entt::handle entity) -> std::string
{
    bool is_bone = entity.all_of<bone_component>();
    bool has_source = entity.any_of<prefab_component>();

    auto icon = has_source ? ICON_MDI_CUBE " " : ICON_MDI_CUBE_OUTLINE " ";
    if(is_bone)
    {
        icon = ICON_MDI_BONE " ";
    }

    return icon;
}

auto entity_panel::get_entity_prefab_role(entt::handle entity) -> prefab_role
{
    if(!entity)
    {
        return prefab_role::none;
    }
    // The root of the document being edited is the document, not an instance of it.
    if(is_authoring_root(entity))
    {
        return prefab_role::none;
    }

    const auto* prefab_comp = entity.try_get<prefab_component>();

    // Whatever instance contains this one, if any. Nothing without one is local *to* anything
    // - an instance placed straight into a scene is just an instance, and the most common
    // thing on screen, so it must not be flagged as an exception.
    const auto* trans_comp = entity.try_get<transform_component>();
    auto current = trans_comp != nullptr ? trans_comp->get_parent() : entt::handle{};
    while(current && !current.all_of<prefab_component>())
    {
        const auto* parent_trans = current.try_get<transform_component>();
        current = parent_trans != nullptr ? parent_trans->get_parent() : entt::handle{};
    }
    const bool is_nested = static_cast<bool>(current);

    if(prefab_comp != nullptr)
    {
        if(!is_nested)
        {
            return prefab_role::instance;
        }

        // A nil instance id is the whole distinction: an instance the containing prefab
        // supplies is a slot in that file and carries its id, while one added or cloned here
        // is nobody's slot and never gets one until this subtree is itself saved as a prefab.
        return prefab_comp->instance_id.is_nil() ? prefab_role::local_instance : prefab_role::linked_instance;
    }

    if(!is_nested)
    {
        return prefab_role::none;
    }

    // Inside an instance: the asset's entities arrived with a prefab id, anything else was
    // added here.
    return entity.all_of<prefab_id_component>() ? prefab_role::asset_content : prefab_role::local_content;
}

auto entity_panel::describe_prefab_role(prefab_role role) -> const char*
{
    switch(role)
    {
        case prefab_role::instance:
            return "An instance of a prefab.";
        case prefab_role::linked_instance:
            return "Comes from the prefab that contains it.\n"
                   "Edits to that prefab reach it, and it goes if the prefab drops it.";
        case prefab_role::local_instance:
            return "Added here, not part of any prefab file.\n"
                   "No prefab above it will replace or remove it.";
        case prefab_role::asset_content:
            return "Comes from this instance's prefab.";
        case prefab_role::local_content:
            return "Added inside this instance. Its prefab knows nothing about it.";
        case prefab_role::none:
            break;
    }
    return "";
}

auto entity_panel::get_entity_display_color(entt::handle entity) -> ImVec4
{
    auto& trans_comp = entity.get<transform_component>();
    bool is_bone = entity.all_of<bone_component>();
    bool is_submesh = entity.all_of<submesh_component>();
    bool is_active_global = trans_comp.is_active_global();
    bool has_source = entity.any_of<prefab_component, prefab_id_component>();
    bool has_broken_source = false;

    if(auto pfb = entity.try_get<prefab_component>())
    {
        if(!pfb->source)
        {
            has_source = false;
            has_broken_source = true;
        }
    }

    auto col = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    // Blue for anything a prefab supplies, amber for anything added where it stands. The
    // second is the one worth spotting: it is what no prefab above will ever update, and what
    // used to be indistinguishable from the rest of a prefab subtree.
    const auto role = get_entity_prefab_role(entity);
    const bool is_local = role == prefab_role::local_instance || role == prefab_role::local_content;

    col = ImLerp(col, ImVec4(0.5f, 0.85f, 1.0f, 1.0f), float(has_source && !is_local) * 0.5f);
    col = ImLerp(col, ImVec4(1.0f, 0.78f, 0.35f, 1.0f), float(is_local) * 0.55f);
    col = ImLerp(col, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), float(has_broken_source) * 0.5f);
    col = ImLerp(col, ImVec4(0.5f, 0.85f, 1.0f, 1.0f), float(is_bone) * 0.5f);
    col = ImLerp(col, ImVec4(0.8f, 0.4f, 0.4f, 1.0f), float(is_submesh) * 0.5f);
    col = ImLerp(col, ImVec4(col.x * 0.75f, col.y * 0.75f, col.z * 0.75f, col.w * 0.75f), float(!is_active_global));

    return col;
}

} // namespace unravel
