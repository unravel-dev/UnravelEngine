#include "inspector_panel.h"
#include "../panels_defs.h"
#include "inspectors/inspectors.h"

#include <editor/editing/editing_manager.h>
#include <editor/imgui/integration/imgui.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/id_component.h>
#include <engine/meta/ecs/entity.hpp>
#include <uuid/uuid.h>
#include <engine/assets/asset_manager.h>
#include <algorithm>
#include <functional>

namespace unravel
{

namespace
{

auto should_use_prefab_inspection(rttr::variant& selected) -> bool
{
    if(!selected.is_type<entt::handle>())
    {
        return false;
    }
    
    auto entity = selected.get_value<entt::handle>();
    if(!entity)
    {
        return false;
    }
    
    return true;
}

/**
 * @brief Inspects an object with automatic prefab comparison if applicable
 * @param ctx The runtime context
 * @param object The object to inspect
 */
auto inspect_object_with_prefab_check(rtti::context& ctx, rttr::variant& object) -> void
{
    auto& override_ctx = ctx.get_cached<prefab_override_context>();
    
    // Check if this object should use prefab inspection
    if(should_use_prefab_inspection(object))
    {
        auto entity = object.get_value<entt::handle>();

        if(override_ctx.begin_prefab_inspection(entity))
        {
            auto result = inspect_var(ctx, object);

            if(result.edit_finished)
            {
                auto& em = ctx.get_cached<editing_manager>();
                em.add_action("Property Edit",[](){});
            }
            
            override_ctx.end_prefab_inspection();
            return;
        }
    }
    // Fall back to normal inspection (empty reference)
    auto result = inspect_var(ctx, object, var_info{});
    
    if(result.edit_finished)
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.add_action("Property Edit",[](){});
    }
}

} // namespace

inspector_panel::inspector_panel(imgui_panels* parent) : entity_panel(parent)
{
}

void inspector_panel::init(rtti::context& ctx)
{
    ctx.add<inspector_registry>();
    ctx.add<prefab_override_context>();
}

void inspector_panel::deinit(rtti::context& ctx)
{
    ctx.remove<inspector_registry>();
    ctx.remove<prefab_override_context>();
}

void inspector_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    entity_panel::on_frame_ui_render();

    if(ImGui::Begin(name, nullptr, ImGuiWindowFlags_MenuBar))
    {
        // ImGui::WindowTimeBlock block(ImGui::GetFont(ImGui::Font::Mono));

        auto& em = ctx.get_cached<editing_manager>();
        auto& selected = em.get_active_selection();

        if(ImGui::BeginMenuBar())
        {
            bool locked = !!locked_object_;

            if(ImGui::MenuItem(locked ? ICON_MDI_LOCK : ICON_MDI_LOCK_OPEN_VARIANT, nullptr, locked))
            {
                locked = !locked;

                if(locked)
                {
                    locked_object_ = selected;
                }
                else
                {
                    locked_object_ = {};
                }
            }

            ImGui::SetItemTooltipEx("%s", "Lock/Unlock Inspector");

            if(ImGui::MenuItem(ICON_MDI_COGS, nullptr, debug_))
            {
                debug_ = !debug_;
            }

            ImGui::SetItemTooltipEx("%s", "Debug View");

            ImGui::EndMenuBar();
        }

        if(debug_)
        {
            push_debug_view();
        }

        auto selections_count = int(em.get_selections().size());

        if(locked_object_)
        {
            inspect_object_with_prefab_check(ctx, locked_object_);
        }
        else if(em.get_selections().size() > 1)
        {           
            ImGui::Text("%d Items Selected.", selections_count);
        }
        else if(selected)
        {
            inspect_object_with_prefab_check(ctx, selected);
        }

        if(debug_)
        {
            pop_debug_view();
        }
    }
    ImGui::End();
}

} // namespace unravel
