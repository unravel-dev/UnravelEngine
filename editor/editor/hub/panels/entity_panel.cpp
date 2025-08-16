#include "entity_panel.h"
#include "panel.h"
#include "reflection/reflection.h"
#include <editor/editing/editing_manager.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/engine.h>


namespace unravel
{

entity_panel::entity_panel(imgui_panels* parent) : parent_(parent)
{
}
void entity_panel::on_frame_ui_render()
{
}

void entity_panel::duplicate_entities(const std::vector<entt::handle>& entities)
{
    auto& ctx = engine::context();
    auto& em = ctx.get_cached<editing_manager>();
    em.add_action("Duplicate Entities",
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
    em.add_action("Focus Entities",
        [camera, entities]() mutable
        {
            defaults::focus_camera_on_entities(camera, entities, 0.4f);
        });
}

void entity_panel::delete_entities(const std::vector<entt::handle>& entities)
{
    auto& ctx = engine::context();
    auto& em = ctx.get_cached<editing_manager>();
    em.add_action("Delete Entities",
        [entities]() mutable
        {
            auto& ctx = engine::context();
            auto& em = ctx.get_cached<editing_manager>();
            for(auto entity : entities)
            {
                if(!entity.valid())
                {
                    return;
                }

                em.unselect(entity);

                prefab_override_context::mark_entity_as_removed(entity);

                entity.destroy();
            }
        });
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

    col = ImLerp(col, ImVec4(0.5f, 0.85f, 1.0f, 1.0f), float(has_source) * 0.5f);
    col = ImLerp(col, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), float(has_broken_source) * 0.5f);
    col = ImLerp(col, ImVec4(0.5f, 0.85f, 1.0f, 1.0f), float(is_bone) * 0.5f);
    col = ImLerp(col, ImVec4(0.8f, 0.4f, 0.4f, 1.0f), float(is_submesh) * 0.5f);
    col = ImLerp(col, ImVec4(col.x * 0.75f, col.y * 0.75f, col.z * 0.75f, col.w * 0.75f), float(!is_active_global));

    return col;
}

void entity_panel::set_entity_name(entt::handle entity, const std::string& name)
{
    auto& comp = entity.get_or_emplace<tag_component>();
    comp.name = name;

    prefab_override_context::mark_property_as_changed(entity, rttr::type::get<tag_component>(), "name");
}

} // namespace unravel
