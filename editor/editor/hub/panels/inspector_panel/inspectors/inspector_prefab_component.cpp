#include "inspector_prefab_component.h"
#include "imgui_widgets/tooltips.h"
#include "inspectors.h"
#include "prefab_changes_view.h"

#include <editor/editing/editing_manager.h>

#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/scene.h>
#include <engine/meta/ecs/entity.hpp>

#include <vector>

#include <logging/logging.h>

// must be below all
#include <engine/assets/impl/asset_writer.h>
#include <editor/imgui/integration/imgui_messagebox.h>
#include <engine/engine.h>

namespace unravel
{

namespace
{

/// Whether a registry pointer belongs to a scene that is alive. Checked by address, never by
/// dereferencing: a handle whose registry is gone is exactly the thing being guarded against.
auto is_live_registry(const entt::registry* registry) -> bool
{
    if(registry == nullptr)
    {
        return false;
    }
    for(const auto* scn : scene::get_all_scenes())
    {
        if(scn != nullptr && scn->registry.get() == registry)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief The entity this component is being inspected on.
 *
 * Taken from the proxy chain - the entity inspector hands each component a proxy whose parent
 * resolves to the entity - and only falls back to the component's own owner handle. A save
 * once crashed on an owner handle whose registry pointer was bad; the proxy entity is what
 * the inspector is actually drawing, so it is the one to trust, and a disagreement between
 * the two is logged so the next occurrence says where the stale owner came from.
 */
auto resolve_inspected_entity(const meta_any_proxy& var_proxy, prefab_component& data) -> entt::handle
{
    entt::handle inspected{};
    if(var_proxy.impl && var_proxy.impl->parent && var_proxy.impl->parent->getter)
    {
        entt::meta_any any;
        if(var_proxy.impl->parent->getter(any) && any)
        {
            if(auto* handle = any.try_cast<entt::handle>())
            {
                inspected = *handle;
            }
        }
    }

    const auto owner = data.get_owner();
    const bool owner_is_live = is_live_registry(owner.registry()) && owner.valid();

    if(inspected && is_live_registry(inspected.registry()) && inspected.valid() &&
       inspected.all_of<prefab_component>())
    {
        if(!owner_is_live || owner != inspected)
        {
            APPLOG_WARNING("prefab_component owner handle (registry {}, entity {}) does not match the inspected "
                           "entity (registry {}, entity {}); using the inspected entity.",
                           fmt::ptr(owner.registry()),
                           static_cast<uint32_t>(owner.entity()),
                           fmt::ptr(inspected.registry()),
                           static_cast<uint32_t>(inspected.entity()));
        }
        return inspected;
    }

    return owner_is_live ? owner : entt::handle{};
}

} // namespace

namespace
{
/**
 * @brief Writes an instance into its prefab file and re-homes what it carried: what was stated
 *        here about the nested content is the document's statement now, what was stated about
 *        this instance's own content is its content. One override survives - a nested root's
 *        own placement, which is the *containing* document's to state; dropping it would let
 *        the container's next replay snap the instance back to where it was before the move.
 */
void apply_instance_to_prefab(entt::handle root)
{
    auto* prefab_comp = root ? root.try_get<prefab_component>() : nullptr;
    if(prefab_comp == nullptr)
    {
        return;
    }
    prefab_comp->changed = false;
    auto prefab_path = fs::resolve_protocol(prefab_comp->source.id());
    asset_writer::atomic_save_to_file(prefab_path.string(), root);

    prefab_statements kept;
    if(is_nested_instance(root))
    {
        const auto* root_id = root.try_get<prefab_id_component>();
        for(const auto& override_data : prefab_comp->local.overrides)
        {
            const bool is_root = root_id != nullptr && override_data.instance_path.empty() &&
                                 override_data.entity_uuid == root_id->id;
            const bool is_placement =
                override_data.component_path.rfind("transform_component/local_transform/position", 0) == 0 ||
                override_data.component_path.rfind("transform_component/local_transform/rotation", 0) == 0;
            if(is_root && is_placement)
            {
                kept.overrides.insert(override_data);
            }
        }
    }
    prefab_comp->from_document = fold_document_statements(root);
    prefab_comp->local = std::move(kept);
    clear_local_statements_below(root);
}
} // namespace

auto inspector_prefab_component::inspect(rtti::context& ctx,
                                        entt::meta_any& var,
                                        const meta_any_proxy& var_proxy,
                                        const var_info& info,
                                        const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<prefab_component&>();
    inspect_result result{};

    auto root_prefab_entity = resolve_inspected_entity(var_proxy, data);
    if(!root_prefab_entity)
    {
        // Nothing below may run through a handle that cannot be trusted - least of all a save.
        APPLOG_ERROR("prefab_component inspector: no live entity for this component; owner handle registry {} "
                     "entity {}. Skipping.",
                     fmt::ptr(data.get_owner().registry()),
                     static_cast<uint32_t>(data.get_owner().entity()));
        ImGui::TextDisabled("This prefab component is not attached to a live entity.");
        return result;
    }

    // Everything from this instance down, with reverts - shared with the authoring roots
    // (prefab mode, the content browser's prefab inspector), which have no prefab_component
    // of their own to hang it from.
    result |= draw_prefab_changes(ctx, root_prefab_entity);

    if(ImGui::Button(ICON_MDI_CONTENT_SAVE " Apply All to Prefab", ImVec2(-1, ImGui::GetFrameHeight())))
    {
        if(is_nested_instance(root_prefab_entity))
        {
            // A nested instance: what goes into its file is everything stated about it here
            // *and* by the containing prefab - the container's authoring becomes the nested
            // asset's content, for every instance of it anywhere. Worth asking.
            ImBox::ShowConfirmation(
                "Apply to nested prefab?",
                "This instance sits inside another prefab instance. Applying writes everything\n"
                "stated about it - here and by the containing prefab - into its own prefab file,\n"
                "for every instance of that prefab everywhere.",
                [root = entt::make_uhandle(root_prefab_entity)](ImBox::ModalResult answer)
                {
                    if(!ImBox::IsConfirmation(answer))
                    {
                        return;
                    }
                    auto entity = root.resolve();
                    if(!entity)
                    {
                        return;
                    }
                    auto& ctx = engine::context();
                    apply_instance_to_prefab(entity);
                    ctx.get_cached<editing_manager>().sync_after_override_change(ctx, entity);
                });
        }
        else
        {
            apply_instance_to_prefab(root_prefab_entity);
            result.changed = true;
        }
    }
    ImGui::SetItemTooltipEx("Save this instance - changes, additions and removals included -\n"
                            "into its prefab file. Every other instance of it will follow.");

    ImGui::NewLine();

    result |= inspect_var_properties(ctx, var, var_proxy, info, custom);

    // One sync from the outermost instance covers every mutation above: its replay restates
    // each containing document's halves - a nested root's placement included - and cascades
    // into every nested instance's own sync, which is what restores reverted values and
    // restored entities from whichever prefab owns them.
    if(result.changed)
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.sync_after_override_change(ctx, root_prefab_entity);
    }

    return result;
}

} // namespace unravel
