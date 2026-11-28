#include "inspector_panel.h"
#include "inspectors/multi_edit.h"
#include <engine/audio/audio_clip.h>
#include <engine/rendering/mesh.h>
#include <graphics/texture.h>
#include "../panel_toolbar.h"
#include "../panels_defs.h"
#include "inspectors/inspectors.h"

#include <editor/assets/asset_actions.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/scene.h>
#include <engine/meta/ecs/entity.hpp>
#include <uuid/uuid.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <algorithm>
#include <cctype>
#include <functional>

namespace unravel
{

namespace
{
constexpr float INSPECTOR_SEARCH_FIELD_MIN_WIDTH = 60.0f;
constexpr float INSPECTOR_SEARCH_FIELD_MAX_WIDTH = 100000.0f;
constexpr const char* INSPECTOR_SEARCH_TOOLTIP =
    "Shows the properties whose name holds the text, and what holds them. A component or a group whose name holds "
    "it shows whole.";
constexpr const char* INSPECTOR_OVERRIDDEN_TOOLTIP =
    "Shows only the values something overrides: the prefab, a settings profile or a material instance.";
/// Seconds the Inspector waits for the import of a copy it made before it gives up selecting it.
constexpr double SHIPPED_COPY_IMPORT_SECONDS = 60.0;

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
    ctx.add<property_override_view>();
}

void inspector_panel::deinit(rtti::context& ctx)
{
    end_inspection(ctx);
    ctx.remove<inspector_registry>();
    ctx.remove<inspector_context>();
    ctx.remove<prefab_override_context>();
    ctx.remove<property_override_view>();
}

auto inspector_panel::get_window_flags() const -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_None;
}

void inspector_panel::draw_ui(rtti::context& ctx)
{
    auto& em = ctx.get_cached<editing_manager>();
    select_pending_copy(ctx);
    auto& selected = em.get_active_selection();
    auto& inspector_ctx = ctx.get_cached<inspector_context>();
    if(inspector_ctx.is_filter_reset_requested)
    {
        search_.Clear();
        only_overridden_ = false;
        inspector_ctx.is_filter_reset_requested = false;
    }
    draw_toolbar(selected, inspector_ctx);
    if(debug_)
    {
        push_debug_view();
    }
    em.push_undo_stack_enabled(true);
    inspector_ctx.component_headers = {};
    // The filter holds for the inspected object only: other windows drawing properties show all of them.
    inspector_ctx.filter = make_filter();
    draw_inspected_object(ctx, selected, em.get_selections().size());
    inspector_ctx.filter = {};
    inspector_ctx.component_headers_open_request.reset();
    em.pop_undo_stack_enabled();
    if(debug_)
    {
        pop_debug_view();
    }
}

void inspector_panel::draw_toolbar(const entt::meta_any& selected, inspector_context& inspector_ctx)
{
    if(panel_toolbar::begin_strip("##inspector_toolbar"))
    {
        draw_lock_toggle(selected);
        draw_search_field();
        panel_toolbar::align_right();
        draw_overridden_toggle();
        draw_fold_buttons(inspector_ctx);
        panel_toolbar::separator();
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

void inspector_panel::draw_fold_buttons(inspector_context& inspector_ctx)
{
    const component_header_counts& headers = inspector_ctx.component_headers;
    panel_toolbar::begin_segment();
    ImGui::BeginDisabled(headers.folded == 0);
    if(panel_toolbar::button("##expand_all", ICON_MDI_UNFOLD_MORE_HORIZONTAL, "Expand all components"))
    {
        inspector_ctx.component_headers_open_request = true;
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(headers.open == 0);
    if(panel_toolbar::button("##collapse_all", ICON_MDI_UNFOLD_LESS_HORIZONTAL, "Collapse all components"))
    {
        inspector_ctx.component_headers_open_request = false;
    }
    ImGui::EndDisabled();
    panel_toolbar::end_segment();
}

void inspector_panel::draw_search_field()
{
    // The last item of the left group: it takes the room the right group leaves.
    panel_toolbar::search_field(search_,
                                ICON_MDI_MAGNIFY " Search...",
                                INSPECTOR_SEARCH_TOOLTIP,
                                INSPECTOR_SEARCH_FIELD_MIN_WIDTH,
                                INSPECTOR_SEARCH_FIELD_MAX_WIDTH);
}

void inspector_panel::draw_overridden_toggle()
{
    if(panel_toolbar::toggle("##only_overridden", ICON_MDI_FORMAT_BOLD, only_overridden_, INSPECTOR_OVERRIDDEN_TOOLTIP))
    {
        only_overridden_ = !only_overridden_;
    }
}

auto inspector_panel::make_filter() const -> inspector_filter
{
    inspector_filter filter;
    filter.text = search_.InputBuf;
    std::transform(filter.text.begin(),
                   filter.text.end(),
                   filter.text.begin(),
                   [](unsigned char c)
                   {
                       return static_cast<char>(std::tolower(c));
                   });
    // Spaces around the text are not part of what is searched.
    const auto first = filter.text.find_first_not_of(' ');
    const auto last = filter.text.find_last_not_of(' ');
    filter.text = first == std::string::npos ? std::string{} : filter.text.substr(first, last - first + 1);
    filter.only_overridden = only_overridden_;
    return filter;
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
    const entt::meta_any* shown = locked_object_ ? &locked_object_ : (selections_count == 1 && selected ? &selected : nullptr);
    follow_shown_object(ctx, shown);
    if(locked_object_)
    {
        inspect_object(ctx, locked_object_);
        return;
    }
    if(selections_count > 1)
    {
        inspect_selected_entities(ctx, selected, selections_count);
        return;
    }
    if(selected)
    {
        inspect_object(ctx, selected);
    }
}

void inspector_panel::inspect_object(rtti::context& ctx, entt::meta_any& object)
{
    const std::string key = editor_actions::get_asset_key(object);
    const bool is_shipped = !key.empty() && !asset_actions::is_project_asset(key);
    if(is_shipped)
    {
        draw_shipped_asset_banner(ctx, key);
    }
    ImGui::PushReadonly(is_shipped);
    inspect_object_with_prefab_check(ctx, object);
    ImGui::PopReadonly();
}

void inspector_panel::draw_shipped_asset_banner(rtti::context& ctx, const std::string& key)
{
    const std::string shipper = asset_actions::get_asset_shipper(key);
    const std::string obstacle = asset_actions::find_project_copy_obstacle(key);
    std::string message = "Every project shares this asset, and an update replaces it, so its values are shown here and "
                          "cannot be changed.";
    message += obstacle.empty() ? std::string(" Duplicate it into the project to edit a copy.") : " " + obstacle;
    // A material's values need no copy to differ on one object: its users override them.
    if(ex::is_format<material>(fs::path(key).extension().string()))
    {
        message += " A model that uses it overrides its values and textures in its own material slot, without a copy.";
    }
    const auto draw_actions = [&]()
    {
        const bool is_importing = pending_copy_.source == key;
        ImGui::BeginDisabled(is_importing);
        if(ImGui::Button(ICON_MDI_CONTENT_DUPLICATE " Duplicate Into Project"))
        {
            const std::string copy = editor_actions::duplicate_asset_into_project(ctx, key);
            if(!copy.empty())
            {
                pending_copy_ = {copy, key, ImGui::GetTime() + SHIPPED_COPY_IMPORT_SECONDS};
            }
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltipEx("Copies it, with its import settings, into the folder the Content Browser shows. The "
                                "Inspector shows the copy once it is imported.");
        if(is_importing)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Importing the copy...");
        }
    };
    draw_inspector_banner("##shipped_asset_banner",
                          ICON_MDI_LOCK_OUTLINE,
                          imgui_style::get_muted_text_color_u32(),
                          "Ships with " + shipper,
                          message,
                          obstacle.empty() ? std::function<void()>(draw_actions) : std::function<void()>{});
}

void inspector_panel::select_pending_copy(rtti::context& ctx)
{
    if(pending_copy_.key.empty())
    {
        return;
    }
    // The copy takes the place of the asset it was made of. An Inspector that shows something else by now, or a
    // copy that never arrived, leaves the selection alone.
    auto& em = ctx.get_cached<editing_manager>();
    const entt::meta_any& shown = locked_object_ ? locked_object_ : em.get_active_selection();
    const bool shows_source = editor_actions::get_asset_key(shown) == pending_copy_.source;
    if(!shows_source || ImGui::GetTime() > pending_copy_.deadline)
    {
        pending_copy_ = {};
        return;
    }
    // There is no asset to show until the asset watcher has imported the file.
    if(locked_object_)
    {
        // A locked Inspector shows what it is locked on, whatever is selected: it locks on the copy, and the
        // selection, which the user may have moved since, stays.
        entt::meta_any copy = editor_actions::find_asset_object(ctx, pending_copy_.key);
        if(!copy)
        {
            return;
        }
        locked_object_ = std::move(copy);
    }
    else if(!editor_actions::select_asset(ctx, pending_copy_.key))
    {
        return;
    }
    pending_copy_ = {};
}

namespace
{

/// The key and uid of a selected asset whose import settings an Inspector edits; nothing for anything else.
auto get_import_asset_identity(const entt::meta_any& selected) -> hpp::optional<multi_edit::asset_identity>
{
    if(const auto* texture = selected.try_cast<asset_handle<gfx::texture>>())
    {
        return multi_edit::asset_identity{texture->id(), texture->uid()};
    }
    if(const auto* clip = selected.try_cast<asset_handle<audio_clip>>())
    {
        return multi_edit::asset_identity{clip->id(), clip->uid()};
    }
    if(const auto* model = selected.try_cast<asset_handle<mesh>>())
    {
        return multi_edit::asset_identity{model->id(), model->uid()};
    }
    return hpp::nullopt;
}

} // namespace

void inspector_panel::inspect_selected_assets(rtti::context& ctx, entt::meta_any& selected, size_t selections_count)
{
    // Several assets of one type whose import settings the Inspector edits draw as the one selected last; Apply of its
    // settings reaches them all. Any other selection has no common rows.
    auto& em = ctx.get_cached<editing_manager>();
    std::vector<multi_edit::asset_identity> others;
    const auto shown = get_import_asset_identity(selected);
    // An asset the project does not own keeps its settings: a selection holding one has no common Apply.
    bool has_shipped = shown && !asset_actions::is_project_asset(shown->key);
    for(const auto& object : em.get_selections())
    {
        const auto other = object.type() == selected.type() ? get_import_asset_identity(object) : hpp::nullopt;
        if(other && shown && other->uid != shown->uid)
        {
            has_shipped = has_shipped || !asset_actions::is_project_asset(other->key);
            others.push_back(*other);
        }
    }
    if(!shown || has_shipped || others.size() + 1 != selections_count)
    {
        ImGui::Text("%d Items Selected.", static_cast<int>(selections_count));
        return;
    }
    ImGui::TextDisabled("%d assets selected: Apply of import settings reaches every one of them.",
                        static_cast<int>(selections_count));
    multi_edit::begin_assets(std::move(others));
    inspect_object(ctx, selected);
    multi_edit::end_assets();
}

void inspector_panel::inspect_selected_entities(rtti::context& ctx, entt::meta_any& selected, size_t selections_count)
{
    // Several entities draw as the one selected last, and edits reach them all. A selection holding anything else
    // has no common rows.
    auto& em = ctx.get_cached<editing_manager>();
    const auto entities = em.try_get_selections_as_copy<entt::handle>();
    const auto* shown = selected.try_cast<entt::handle>();
    if(shown == nullptr || entities.size() != selections_count)
    {
        inspect_selected_assets(ctx, selected, selections_count);
        return;
    }
    std::vector<entt::handle> others;
    std::copy_if(entities.begin(),
                 entities.end(),
                 std::back_inserter(others),
                 [&](const entt::handle& entity)
                 {
                     return entity && entity != *shown && entity.registry() == shown->registry();
                 });
    ImGui::TextDisabled("%d entities selected: edits reach every one of them.", static_cast<int>(others.size() + 1));
    multi_edit::begin(*shown, std::move(others));
    inspect_object_with_prefab_check(ctx, selected);
    multi_edit::end();
}

void inspector_panel::follow_shown_object(rtti::context& ctx, const entt::meta_any* shown)
{
    const bool is_same = shown != nullptr ? (shown_object_ && *shown == shown_object_) : !shown_object_;
    if(is_same)
    {
        return;
    }
    end_inspection(ctx);
    if(shown != nullptr)
    {
        shown_object_ = *shown;
    }
}

void inspector_panel::end_inspection(rtti::context& ctx)
{
    if(!shown_object_)
    {
        return;
    }
    if(auto shown_inspector = get_inspector(ctx, shown_object_.type()))
    {
        shown_inspector->end_inspection(ctx);
    }
    shown_object_ = {};
}

} // namespace unravel
