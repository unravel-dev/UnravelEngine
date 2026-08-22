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
        data.changed = false;
        auto prefab_path = fs::resolve_protocol(data.source.id());
        asset_writer::atomic_save_to_file(prefab_path.string(), root_prefab_entity);

        // Everything just went into the prefab, so no override remains - except a nested
        // root's own placement. That is the *containing* document's to state, not this
        // asset's; the asset cannot absorb it, and dropping the override would let the
        // container's next replay snap the instance back to where it was before the move.
        std::vector<prefab_property_override_data> placement_overrides;
        if(is_nested_instance(root_prefab_entity))
        {
            const auto* root_id = root_prefab_entity.try_get<prefab_id_component>();
            for(const auto& override_data : data.get_all_overrides())
            {
                const bool is_root = root_id != nullptr && override_data.entity_uuid == root_id->id;
                const bool is_placement =
                    override_data.component_path.rfind("transform_component/local_transform/position", 0) == 0 ||
                    override_data.component_path.rfind("transform_component/local_transform/rotation", 0) == 0;
                if(is_root && is_placement)
                {
                    placement_overrides.push_back(override_data);
                }
            }
        }

        data.clear_overrides();
        for(const auto& kept : placement_overrides)
        {
            data.property_overrides.insert(kept);
        }
        result.changed = true;
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
