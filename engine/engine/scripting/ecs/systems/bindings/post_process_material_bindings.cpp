#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/rendering/ecs/components/post_process_material_component.h>

#include <algorithm>

namespace unravel
{
namespace
{

/// The entry @p index of entity @p id's Post Process Materials, or null.
auto find_post_process_entry(entt::entity id, int32_t index) -> post_process_material_entry*
{
    auto comp = safe_get_component<post_process_material_component>(id);
    if(!comp || index < 0 || size_t(index) >= comp->entries.size())
    {
        return nullptr;
    }
    return &comp->entries[size_t(index)];
}

auto find_post_process_material(const hpp::uuid& uid) -> asset_handle<material>
{
    if(uid.is_nil())
    {
        return {};
    }
    return engine::context().get_cached<asset_manager>().get_asset<material>(uid);
}

auto internal_m2n_post_materials_count(entt::entity id) -> int32_t
{
    auto comp = safe_get_component<post_process_material_component>(id);
    return comp ? int32_t(comp->entries.size()) : 0;
}

auto internal_m2n_post_materials_index_of(entt::entity id, const hpp::uuid& uid) -> int32_t
{
    auto comp = safe_get_component<post_process_material_component>(id);
    if(!comp || uid.is_nil())
    {
        return -1;
    }
    const auto it = std::find_if(comp->entries.begin(),
                                 comp->entries.end(),
                                 [&](const post_process_material_entry& entry)
                                 {
                                     return entry.get_material().uid() == uid;
                                 });
    return it == comp->entries.end() ? -1 : int32_t(it - comp->entries.begin());
}

auto internal_m2n_post_materials_add(entt::entity id, const hpp::uuid& uid, float weight) -> int32_t
{
    auto comp = safe_get_component<post_process_material_component>(id);
    const auto mat = find_post_process_material(uid);
    if(!comp || !mat)
    {
        return -1;
    }
    // An entry the component has keeps its overrides and takes the new weight.
    auto& entry = comp->add(mat);
    entry.weight = std::clamp(weight, 0.0f, 1.0f);
    return int32_t(&entry - comp->entries.data());
}

auto internal_m2n_post_materials_remove(entt::entity id, const hpp::uuid& uid) -> bool
{
    auto comp = safe_get_component<post_process_material_component>(id);
    const auto mat = find_post_process_material(uid);
    return comp && mat && comp->remove(mat);
}

auto internal_m2n_post_materials_get_material(entt::entity id, int32_t index) -> hpp::uuid
{
    auto* entry = find_post_process_entry(id, index);
    return entry ? entry->get_material().uid() : hpp::uuid{};
}

auto internal_m2n_post_materials_get_weight(entt::entity id, int32_t index) -> float
{
    auto* entry = find_post_process_entry(id, index);
    return entry ? entry->weight : 0.0f;
}

void internal_m2n_post_materials_set_weight(entt::entity id, int32_t index, float weight)
{
    if(auto* entry = find_post_process_entry(id, index))
    {
        entry->weight = std::clamp(weight, 0.0f, 1.0f);
    }
}

auto internal_m2n_post_materials_get_enabled(entt::entity id, int32_t index) -> bool
{
    auto* entry = find_post_process_entry(id, index);
    return entry != nullptr && entry->enabled;
}

void internal_m2n_post_materials_set_enabled(entt::entity id, int32_t index, bool enabled)
{
    if(auto* entry = find_post_process_entry(id, index))
    {
        entry->enabled = enabled;
    }
}

} // namespace

void register_post_process_material_script_bindings()
{
    auto reg = dotnet::internal_call_registry("Unravel.Core.PostProcessMaterialComponent");
    reg.add_internal_call("internal_m2n_post_materials_count", script_internal_call(internal_m2n_post_materials_count));
    reg.add_internal_call("internal_m2n_post_materials_index_of", script_internal_call(internal_m2n_post_materials_index_of));
    reg.add_internal_call("internal_m2n_post_materials_add", script_internal_call(internal_m2n_post_materials_add));
    reg.add_internal_call("internal_m2n_post_materials_remove", script_internal_call(internal_m2n_post_materials_remove));
    reg.add_internal_call("internal_m2n_post_materials_get_material", script_internal_call(internal_m2n_post_materials_get_material));
    reg.add_internal_call("internal_m2n_post_materials_get_weight", script_internal_call(internal_m2n_post_materials_get_weight));
    reg.add_internal_call("internal_m2n_post_materials_set_weight", script_internal_call(internal_m2n_post_materials_set_weight));
    reg.add_internal_call("internal_m2n_post_materials_get_enabled", script_internal_call(internal_m2n_post_materials_get_enabled));
    reg.add_internal_call("internal_m2n_post_materials_set_enabled", script_internal_call(internal_m2n_post_materials_set_enabled));
}

} // namespace unravel
