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


void entity_panel::set_entity_name(entt::handle entity, const std::string& name)
{
    auto& comp = entity.get_or_emplace<tag_component>();
    comp.name = name;

    prefab_override_context::mark_property_as_changed(entity, rttr::type::get<tag_component>(), "name");
}

} // namespace unravel
