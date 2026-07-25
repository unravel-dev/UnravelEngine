#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material.h>

namespace unravel
{
namespace
{

auto get_entry(submesh_component* comp, int index) -> submesh_entry*
{
    if(!comp || index < 0 || static_cast<size_t>(index) >= comp->entries.size())
    {
        return nullptr;
    }
    return &comp->entries[static_cast<size_t>(index)];
}

auto internal_m2n_submesh_get_entry_count(entt::entity id) -> int
{
    if(auto comp = safe_get_component<submesh_component>(id))
    {
        return static_cast<int>(comp->entries.size());
    }
    return 0;
}

auto internal_m2n_submesh_get_submesh_index(entt::entity id, int index) -> uint32_t
{
    if(auto* entry = get_entry(safe_get_component<submesh_component>(id), index))
    {
        return entry->submesh_index;
    }
    return 0;
}

auto internal_m2n_submesh_get_stable_id(entt::entity id, int index) -> uint32_t
{
    if(auto* entry = get_entry(safe_get_component<submesh_component>(id), index))
    {
        return entry->stable_id;
    }
    return 0;
}

auto internal_m2n_submesh_get_enabled(entt::entity id, int index) -> bool
{
    if(auto* entry = get_entry(safe_get_component<submesh_component>(id), index))
    {
        return entry->enabled;
    }
    return false;
}

void internal_m2n_submesh_set_enabled(entt::entity id, int index, bool enabled)
{
    if(auto* entry = get_entry(safe_get_component<submesh_component>(id), index))
    {
        entry->enabled = enabled;
    }
}

auto internal_m2n_submesh_get_casts_shadow(entt::entity id, int index) -> bool
{
    if(auto* entry = get_entry(safe_get_component<submesh_component>(id), index))
    {
        return entry->casts_shadow;
    }
    return false;
}

void internal_m2n_submesh_set_casts_shadow(entt::entity id, int index, bool casts_shadow)
{
    if(auto* entry = get_entry(safe_get_component<submesh_component>(id), index))
    {
        entry->casts_shadow = casts_shadow;
    }
}

auto internal_m2n_submesh_get_material_override(entt::entity id, int index) -> hpp::uuid
{
    if(auto* entry = get_entry(safe_get_component<submesh_component>(id), index))
    {
        return entry->material_override.uid();
    }
    return {};
}

void internal_m2n_submesh_set_material_override(entt::entity id, int index, const hpp::uuid& uid)
{
    if(auto* entry = get_entry(safe_get_component<submesh_component>(id), index))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();
        entry->material_override = am.get_asset<material>(uid);
    }
}

} // namespace

void register_submesh_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.SubmeshComponent");
    reg.add_internal_call("internal_m2n_submesh_get_entry_count",
                          dotnet_internal_call(internal_m2n_submesh_get_entry_count));
    reg.add_internal_call("internal_m2n_submesh_get_submesh_index",
                          dotnet_internal_call(internal_m2n_submesh_get_submesh_index));
    reg.add_internal_call("internal_m2n_submesh_get_stable_id",
                          dotnet_internal_call(internal_m2n_submesh_get_stable_id));
    reg.add_internal_call("internal_m2n_submesh_get_enabled",
                          dotnet_internal_call(internal_m2n_submesh_get_enabled));
    reg.add_internal_call("internal_m2n_submesh_set_enabled",
                          dotnet_internal_call(internal_m2n_submesh_set_enabled));
    reg.add_internal_call("internal_m2n_submesh_get_casts_shadow",
                          dotnet_internal_call(internal_m2n_submesh_get_casts_shadow));
    reg.add_internal_call("internal_m2n_submesh_set_casts_shadow",
                          dotnet_internal_call(internal_m2n_submesh_set_casts_shadow));
    reg.add_internal_call("internal_m2n_submesh_get_material_override",
                          dotnet_internal_call(internal_m2n_submesh_get_material_override));
    reg.add_internal_call("internal_m2n_submesh_set_material_override",
                          dotnet_internal_call(internal_m2n_submesh_set_material_override));
}

} // namespace unravel
