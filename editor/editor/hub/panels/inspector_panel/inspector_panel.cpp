#include "inspector_panel.h"
#include "../panel_toolbar.h"
#include "../panels_defs.h"
#include "inspectors/inspectors.h"

#include <editor/editing/editing_manager.h>
#include <editor/imgui/integration/imgui.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/scene.h>
#include <engine/meta/ecs/entity.hpp>
#include <uuid/uuid.h>
#include <engine/assets/asset_manager.h>
#include <algorithm>
#include <functional>

namespace unravel
{

namespace
{

auto should_use_prefab_inspection(entt::meta_any& selected) -> bool
{
    if(selected.type() != entt::resolve<entt::handle>())
    {
        return false;
    }
    
    auto entity = selected.cast<entt::handle>();
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
auto inspect_object_with_prefab_check(rtti::context& ctx, entt::meta_any& object) -> void
{
    auto& override_ctx = ctx.get_cached<prefab_override_context>();
    auto& inspector_ctx = ctx.get_cached<inspector_context>();
    // Check if this object should use prefab inspection
    if(should_use_prefab_inspection(object))
    {
        auto entity = object.cast<entt::handle>();

        if(override_ctx.begin_prefab_inspection(entity))
        {
            auto name = entity_panel::get_entity_name(entity);
            auto proxy = make_entity_proxy(object, name);
            auto result = inspect_var(ctx, object, proxy);
            
            override_ctx.end_prefab_inspection();
            return;
        }
    }

    std::string name;
    if(object.type() == entt::resolve<entt::handle>())
    {
        auto entity = object.cast<entt::handle>();
        name = entity_panel::get_entity_name(entity);
    }

    // Fall back to normal inspection (empty reference)
    auto proxy = make_entity_proxy(object, name);
    auto result = inspect_var(ctx, object, proxy);

}

} // namespace

inspector_panel::inspector_panel(imgui_panels* parent, const char* name) : entity_panel(parent, name)
{
}

void inspector_panel::init(rtti::context& ctx)
{
    ctx.add<inspector_registry>();
    ctx.add<inspector_context>();
    ctx.add<prefab_override_context>();
}

void inspector_panel::deinit(rtti::context& ctx)
{
    ctx.remove<inspector_registry>();
    ctx.remove<inspector_context>();
    ctx.remove<prefab_override_context>();
}

auto inspector_panel::get_window_flags() const -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_None;
}

void inspector_panel::draw_ui(rtti::context& ctx)
{
    auto& em = ctx.get_cached<editing_manager>();
    auto& selected = em.get_active_selection();
    draw_toolbar(selected);
    if(debug_)
    {
        push_debug_view();
    }
    em.push_undo_stack_enabled(true);
    draw_inspected_object(ctx, selected, em.get_selections().size());
    em.pop_undo_stack_enabled();
    if(debug_)
    {
        pop_debug_view();
    }
}

void inspector_panel::draw_toolbar(const entt::meta_any& selected)
{
    if(panel_toolbar::begin_strip("##inspector_toolbar"))
    {
        draw_lock_toggle(selected);
        panel_toolbar::align_right();
        draw_debug_toggle();
    }
    panel_toolbar::end_strip();
}

void inspector_panel::draw_lock_toggle(const entt::meta_any& selected)
{
    const bool is_locked = !!locked_object_;
    const char* text = is_locked ? ICON_MDI_LOCK " Locked" : ICON_MDI_LOCK_OPEN_VARIANT " Lock";
    const char* tooltip = is_locked ? "Follow the selection again" : "Keep showing this object, whatever gets selected";
    // The label changes with the state; the longer one keeps the button from resizing.
    const char* width_text = ICON_MDI_LOCK " Locked";
    if(!panel_toolbar::toggle("##lock", text, is_locked, tooltip, width_text))
    {
        return;
    }
    locked_object_ = is_locked ? entt::meta_any{} : selected;
}

void inspector_panel::draw_debug_toggle()
{
    if(panel_toolbar::toggle("##debug_view", ICON_MDI_COGS, debug_, "Debug View"))
    {
        debug_ = !debug_;
    }
}

void inspector_panel::draw_inspected_object(rtti::context& ctx, entt::meta_any& selected, size_t selections_count)
{
    if(locked_object_)
    {
        inspect_object_with_prefab_check(ctx, locked_object_);
        return;
    }
    if(selections_count > 1)
    {
        ImGui::Text("%d Items Selected.", static_cast<int>(selections_count));
        return;
    }
    if(selected)
    {
        inspect_object_with_prefab_check(ctx, selected);
    }
}

} // namespace unravel
