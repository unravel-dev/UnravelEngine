#include "script_glue_common.h"
#include "script_bindings.h"
#include "material_script_helpers.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/decal_component.h>
#include <engine/rendering/ecs/components/post_process_material_component.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/mesh.h>

namespace unravel
{
namespace
{

auto internal_m2n_model_get_enabled(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->is_enabled();
    }

    return false;
}

void internal_m2n_model_set_enabled(entt::entity id, bool enabled)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        comp->set_enabled(enabled);
    }
}

auto internal_m2n_model_get_casts_shadow(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->casts_shadow();
    }
    return false;
}

auto internal_m2n_model_get_receive_decals(entt::entity id) -> int32_t
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return int32_t(comp->get_decal_receive());
    }
    return 0;
}

auto internal_m2n_model_get_post_process_mask(entt::entity id) -> int32_t
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_post_process_mask();
    }
    return 0;
}

void internal_m2n_model_set_post_process_mask(entt::entity id, int32_t value)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        comp->set_post_process_mask(value);
    }
}

auto internal_m2n_model_get_sort_priority(entt::entity id) -> int32_t
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_sort_priority();
    }
    return 0;
}

void internal_m2n_model_set_sort_priority(entt::entity id, int32_t value)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        comp->set_sort_priority(value);
    }
}

void internal_m2n_model_set_receive_decals(entt::entity id, int32_t mode)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        comp->set_decal_receive(decal_receive(std::clamp(mode, 0, 2)));
    }
}

void internal_m2n_model_set_casts_shadow(entt::entity id, bool casts_shadow)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        comp->set_casts_shadow(casts_shadow);
    }
}

auto internal_m2n_model_get_static(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->is_static();
    }
    return false;
}

void internal_m2n_model_set_static(entt::entity id, bool is_static)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        comp->set_static(is_static);
    }
}

auto internal_m2n_model_get_mesh(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_lod(0).uid();
    }
    return {};
}

void internal_m2n_model_set_mesh(entt::entity id, const hpp::uuid& uid)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();
        auto asset = am.get_asset<mesh>(uid);
        auto model = comp->get_model();
        model.set_lod(asset, 0);
        comp->set_model(model);
    }
}

auto internal_m2n_model_get_lod_selection_bias(entt::entity id) -> float
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_lod_selection_bias();
    }
    return 0.0f;
}

void internal_m2n_model_set_lod_selection_bias(entt::entity id, float bias)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        auto model = comp->get_model();
        model.set_lod_selection_bias(bias);
        comp->set_model(model);
    }
}

auto internal_m2n_model_get_shared_material(entt::entity id, uint32_t index) -> hpp::uuid
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_shared_material(index).uid();
    }

    return {};
}

auto internal_m2n_model_get_shared_material_count(entt::entity id) -> int
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_shared_materials().size();
    }

    return {};
}

void internal_m2n_model_set_shared_material(entt::entity id, const hpp::uuid& uid, uint32_t index)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();
        auto asset = am.get_asset<material>(uid);

        comp->set_shared_material(asset, index);
    }
}

/// A MaterialInstance of no renderer has no owner (Entity.Invalid) and passes its id
/// (rendering_system::create_material_instance) as the slot.
auto is_standalone_instance(entt::entity id) -> bool
{
    return id == entt::entity(0);
}

/// The material instance of no renderer @p instance_id; null once its script released it.
auto find_standalone_instance(uint32_t instance_id) -> std::shared_ptr<material_instance>
{
    return engine::context().get_cached<rendering_system>().find_material_instance(instance_id);
}

/// The slot a decal's MaterialInstance passes (DecalComponent.materialInstance): a decal has one instance, of its
/// material. Mirrors MaterialInstance.DecalSlot in MaterialInstance.cs.
constexpr uint32_t DECAL_MATERIAL_INSTANCE_SLOT = 0xFFFFFFFFu;
/// The first slot a post-process material entry's MaterialInstance passes: entry i passes this plus i
/// (PostProcessMaterialComponent.GetInstance). Mirrors PostProcessMaterialComponent.InstanceSlotBase.
constexpr uint32_t POST_PROCESS_MATERIAL_INSTANCE_SLOT_BASE = 0xFF000000u;

/// Whether @p slot names a post-process material entry rather than a model slot or a decal.
auto is_post_process_entry_slot(uint32_t slot) -> bool
{
    return slot >= POST_PROCESS_MATERIAL_INSTANCE_SLOT_BASE && slot != DECAL_MATERIAL_INSTANCE_SLOT;
}

/// The post-process material entry of entity @p id that @p slot names, or null.
auto find_post_process_entry_of_slot(entt::entity id, uint32_t slot) -> post_process_material_entry*
{
    auto comp = safe_get_component<post_process_material_component>(id);
    const size_t index = slot - POST_PROCESS_MATERIAL_INSTANCE_SLOT_BASE;
    return comp && index < comp->entries.size() ? &comp->entries[index] : nullptr;
}

/// The material instance a script's MaterialInstance stands for: model @p id's of slot @p slot, decal @p id's for
/// DECAL_MATERIAL_INSTANCE_SLOT, or the material instance of no renderer @p slot when @p id is none
/// (is_standalone_instance). Null when it is gone.
auto find_instance(entt::entity id, uint32_t slot) -> const material_instance*
{
    if(is_standalone_instance(id))
    {
        // rendering_system keeps it alive until its script releases it.
        return find_standalone_instance(slot).get();
    }
    if(slot == DECAL_MATERIAL_INSTANCE_SLOT)
    {
        auto decal = safe_get_component<decal_component>(id);
        return decal ? &decal->get_material_instance() : nullptr;
    }
    if(is_post_process_entry_slot(slot))
    {
        auto* entry = find_post_process_entry_of_slot(id, slot);
        return entry ? &entry->get_material_instance() : nullptr;
    }
    auto comp = safe_get_component<model_component>(id);
    return comp ? &comp->get_material_instance(slot) : nullptr;
}

/**
 * @brief Changes the material instance find_instance() finds: one of no renderer directly with @p change_standalone,
 * a model's with @p change_model through its component, which keeps the model's draws up to date.
 */
template<typename StandaloneChange, typename ModelChange>
void change_instance(entt::entity id, uint32_t slot, StandaloneChange&& change_standalone, ModelChange&& change_model)
{
    if(is_standalone_instance(id))
    {
        if(const auto instance = find_standalone_instance(slot))
        {
            change_standalone(*instance);
        }
        return;
    }
    if(slot == DECAL_MATERIAL_INSTANCE_SLOT)
    {
        // Through the component, which resolves the instance its draws read.
        if(auto decal = safe_get_component<decal_component>(id))
        {
            auto instance = decal->get_material_instance();
            change_standalone(instance);
            decal->set_material_instance(instance);
        }
        return;
    }
    if(is_post_process_entry_slot(slot))
    {
        // Through the entry, which resolves the instance its pass reads.
        if(auto* entry = find_post_process_entry_of_slot(id, slot))
        {
            auto instance = entry->get_material_instance();
            change_standalone(instance);
            entry->set_material_instance(instance);
        }
        return;
    }
    if(auto comp = safe_get_component<model_component>(id))
    {
        change_model(*comp);
    }
}

/// The graph material @p instance overrides, once it is loaded (as an override waits for it); null for another.
auto get_instance_shared_graph(const material_instance& instance) -> std::shared_ptr<graph_material>
{
    const auto shared = instance.get_shared_material().get();
    return shared && !asset_handle<material>::is_empty(shared) ? safe_pointer_cast<graph_material>(shared) : nullptr;
}

/// Where script property @p property of the slot's material instance lives: its shared graph's parameter or texture
/// (resolve_script_property); no target while the instance is gone or overrides no graph material.
auto resolve_instance_property(const material_instance* instance, int32_t property) -> standard_property_target
{
    const auto shared = instance != nullptr ? get_instance_shared_graph(*instance) : nullptr;
    return shared ? resolve_script_property(*shared, property) : standard_property_target{};
}

/// The value slot @p slot of the model draws with for @p property: its material instance's override, else the shared
/// material's.
auto get_instance_value(entt::entity id, uint32_t slot, int32_t property) -> entt::meta_any
{
    const auto* instance = find_instance(id, slot);
    const auto shared = instance != nullptr ? get_instance_shared_graph(*instance) : nullptr;
    if(!shared)
    {
        return {};
    }
    if(is_mode_property(property))
    {
        // The modes are the shared graph's.
        return get_script_material_value(*shared, property);
    }
    const auto target = resolve_script_property(*shared, property);
    if(target.property == nullptr)
    {
        return {};
    }
    if(target.property->type == standard_property::kind::texture)
    {
        return instance->get_graph_texture(target.property->name);
    }
    const auto parameter = instance->get_graph_parameter(target.property->name);
    return parameter ? make_script_value(*target.property, apply_standard_inversion(target, *parameter)) : entt::meta_any{};
}

/// Overrides @p property of the slot's shared material for this model only: the parameter or texture the shared graph
/// holds it in (Roughness as 1 - Glossiness on a specular material). The alpha and cull modes are the graph's.
void set_instance_value(entt::entity id, uint32_t slot, int32_t property, const entt::meta_any& value)
{
    const auto* instance = find_instance(id, slot);
    const auto shared = instance != nullptr ? get_instance_shared_graph(*instance) : nullptr;
    if(!shared)
    {
        return;
    }
    const auto target = resolve_script_property(*shared, property);
    if(target.property == nullptr)
    {
        warn_script_property_missing(instance->get_shared_material(), *shared, property);
        return;
    }
    const std::string name = target.property->name;
    if(const auto* texture = value.try_cast<texture_handle>())
    {
        change_instance(
            id,
            slot,
            [&](material_instance& instance)
            {
                instance.set_graph_texture(name, *texture);
            },
            [&](model_component& comp)
            {
                comp.set_material_instance_texture(slot, name, *texture);
            });
        return;
    }
    const auto vector = get_script_vector(value);
    if(!vector)
    {
        return;
    }
    const auto parameter = make_standard_parameter_value(*target.property, apply_standard_inversion(target, *vector));
    change_instance(
        id,
        slot,
        [&](material_instance& instance)
        {
            instance.set_graph_parameter(name, parameter);
        },
        [&](model_component& comp)
        {
            comp.set_material_instance_parameter(slot, name, parameter);
        });
}

void internal_m2n_material_instance_set_color(entt::entity id, uint32_t slot, int32_t property, const managed_color& value)
{
    set_instance_value(id, slot, property, make_color_value(value));
}

auto internal_m2n_material_instance_get_color(entt::entity id, uint32_t slot, int32_t property) -> managed_color
{
    return read_color_value(get_instance_value(id, slot, property));
}

void internal_m2n_material_instance_set_float(entt::entity id, uint32_t slot, int32_t property, float value)
{
    set_instance_value(id, slot, property, value);
}

auto internal_m2n_material_instance_get_float(entt::entity id, uint32_t slot, int32_t property) -> float
{
    return read_float_value(get_instance_value(id, slot, property));
}

void internal_m2n_material_instance_set_vector(entt::entity id,
                                               uint32_t slot,
                                               int32_t property,
                                               const managed_vector2& value)
{
    set_instance_value(id, slot, property, make_vector_value(value));
}

auto internal_m2n_material_instance_get_vector(entt::entity id, uint32_t slot, int32_t property) -> managed_vector2
{
    return read_vector_value(get_instance_value(id, slot, property));
}

void internal_m2n_material_instance_set_texture(entt::entity id, uint32_t slot, int32_t property, const hpp::uuid& uid)
{
    set_instance_value(id, slot, property, make_texture_value(uid));
}

auto internal_m2n_material_instance_get_texture(entt::entity id, uint32_t slot, int32_t property) -> hpp::uuid
{
    return read_texture_value(get_instance_value(id, slot, property));
}

void internal_m2n_material_instance_set_mode(entt::entity /*id*/, uint32_t /*slot*/, int32_t property, int32_t /*value*/)
{
    warn_script_mode_ignored(property);
}

auto internal_m2n_material_instance_get_mode(entt::entity id, uint32_t slot, int32_t property) -> int32_t
{
    return read_mode_value(get_instance_value(id, slot, property));
}

auto internal_m2n_material_instance_is_overridden(entt::entity id, uint32_t slot, int32_t property) -> bool
{
    const auto* instance = find_instance(id, slot);
    // The modes are the graph's: never overridden.
    const auto target = resolve_instance_property(instance, property);
    return target.property != nullptr && (instance->is_graph_parameter_overridden(target.property->name) ||
                                          instance->is_graph_texture_overridden(target.property->name));
}

void internal_m2n_material_instance_revert(entt::entity id, uint32_t slot, int32_t property)
{
    const auto target = resolve_instance_property(find_instance(id, slot), property);
    if(target.property == nullptr)
    {
        return;
    }
    const std::string name = target.property->name;
    change_instance(
        id,
        slot,
        [&](material_instance& instance)
        {
            instance.revert_graph_parameter(name);
            instance.revert_graph_texture(name);
        },
        [&](model_component& comp)
        {
            comp.revert_material_instance_graph_value(slot, name);
        });
}

void internal_m2n_material_instance_revert_all(entt::entity id, uint32_t slot)
{
    change_instance(
        id,
        slot,
        [](material_instance& instance)
        {
            instance.revert_all();
        },
        [&](model_component& comp)
        {
            comp.revert_material_instance(slot);
        });
}

auto internal_m2n_material_instance_has_overrides(entt::entity id, uint32_t slot) -> bool
{
    const auto* instance = find_instance(id, slot);
    return instance && instance->has_overrides();
}

auto internal_m2n_material_instance_shares_draw(entt::entity id, uint32_t slot) -> bool
{
    const auto* instance = find_instance(id, slot);
    return instance && instance->shares_draw();
}

auto internal_m2n_material_instance_has_parameter(entt::entity id, uint32_t slot, const std::string& name) -> bool
{
    const auto* instance = find_instance(id, slot);
    return instance && instance->get_graph_parameter(name).has_value();
}

auto internal_m2n_material_instance_get_parameter(entt::entity id, uint32_t slot, const std::string& name) -> managed_vector4
{
    const auto* instance = find_instance(id, slot);
    const auto value = instance ? instance->get_graph_parameter(name) : hpp::optional<math::vec4>{};
    return make_managed_vector4(value.value_or(math::vec4(0.0f)));
}

auto internal_m2n_material_instance_set_parameter(entt::entity id, uint32_t slot, const std::string& name, const managed_vector4& value)
    -> bool
{
    bool is_set = false;
    change_instance(
        id,
        slot,
        [&](material_instance& instance)
        {
            is_set = instance.set_graph_parameter(name, make_vector4(value));
        },
        [&](model_component& comp)
        {
            is_set = comp.set_material_instance_parameter(slot, name, make_vector4(value));
        });
    return is_set;
}

auto internal_m2n_material_instance_has_texture_node(entt::entity id, uint32_t slot, const std::string& name) -> bool
{
    const auto* instance = find_instance(id, slot);
    const auto shared = instance ? instance->get_shared_material().get() : nullptr;
    const auto* graph = shared ? shared->safe_cast<graph_material>() : nullptr;
    return graph && graph->find_texture(name) != nullptr;
}

auto internal_m2n_material_instance_get_texture_by_name(entt::entity id, uint32_t slot, const std::string& name) -> hpp::uuid
{
    const auto* instance = find_instance(id, slot);
    return instance ? instance->get_graph_texture(name).uid() : hpp::uuid{};
}

auto internal_m2n_material_instance_set_texture_by_name(entt::entity id, uint32_t slot, const std::string& name, const hpp::uuid& uid)
    -> bool
{
    const auto texture = make_texture_value(uid).cast<texture_handle>();
    bool is_set = false;
    change_instance(
        id,
        slot,
        [&](material_instance& instance)
        {
            is_set = instance.set_graph_texture(name, texture);
        },
        [&](model_component& comp)
        {
            is_set = comp.set_material_instance_texture(slot, name, texture);
        });
    return is_set;
}

auto internal_m2n_material_instance_is_name_overridden(entt::entity id, uint32_t slot, const std::string& name) -> bool
{
    const auto* instance = find_instance(id, slot);
    return instance && (instance->is_graph_parameter_overridden(name) || instance->is_graph_texture_overridden(name));
}

void internal_m2n_material_instance_revert_name(entt::entity id, uint32_t slot, const std::string& name)
{
    change_instance(
        id,
        slot,
        [&](material_instance& instance)
        {
            instance.revert_graph_parameter(name);
            instance.revert_graph_texture(name);
        },
        [&](model_component& comp)
        {
            comp.revert_material_instance_graph_value(slot, name);
        });
}

auto internal_m2n_material_instance_create(const hpp::uuid& material_uid) -> uint32_t
{
    auto& ctx = engine::context();
    const auto shared = ctx.get_cached<asset_manager>().get_asset<material>(material_uid);
    return ctx.get_cached<rendering_system>().create_material_instance(shared);
}

void internal_m2n_material_instance_release(uint32_t instance_id)
{
    engine::context().get_cached<rendering_system>().release_material_instance(instance_id);
}

//------------------------------

} // namespace

void register_model_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.ModelComponent");
    reg.add_internal_call("internal_m2n_model_get_enabled", script_internal_call(internal_m2n_model_get_enabled));
    reg.add_internal_call("internal_m2n_model_set_enabled", script_internal_call(internal_m2n_model_set_enabled));
    reg.add_internal_call("internal_m2n_model_get_casts_shadow",
                          script_internal_call(internal_m2n_model_get_casts_shadow));
    reg.add_internal_call("internal_m2n_model_set_casts_shadow",
                          script_internal_call(internal_m2n_model_set_casts_shadow));
    reg.add_internal_call("internal_m2n_model_get_receive_decals",
                          script_internal_call(internal_m2n_model_get_receive_decals));
    reg.add_internal_call("internal_m2n_model_set_receive_decals",
                          script_internal_call(internal_m2n_model_set_receive_decals));
    reg.add_internal_call("internal_m2n_model_get_post_process_mask",
                          script_internal_call(internal_m2n_model_get_post_process_mask));
    reg.add_internal_call("internal_m2n_model_get_sort_priority", script_internal_call(internal_m2n_model_get_sort_priority));
    reg.add_internal_call("internal_m2n_model_set_sort_priority", script_internal_call(internal_m2n_model_set_sort_priority));
    reg.add_internal_call("internal_m2n_model_set_post_process_mask",
                          script_internal_call(internal_m2n_model_set_post_process_mask));
    reg.add_internal_call("internal_m2n_model_get_static", script_internal_call(internal_m2n_model_get_static));
    reg.add_internal_call("internal_m2n_model_set_static", script_internal_call(internal_m2n_model_set_static));
    reg.add_internal_call("internal_m2n_model_get_mesh", script_internal_call(internal_m2n_model_get_mesh));
    reg.add_internal_call("internal_m2n_model_set_mesh", script_internal_call(internal_m2n_model_set_mesh));
    reg.add_internal_call("internal_m2n_model_get_lod_selection_bias",
                          script_internal_call(internal_m2n_model_get_lod_selection_bias));
    reg.add_internal_call("internal_m2n_model_set_lod_selection_bias",
                          script_internal_call(internal_m2n_model_set_lod_selection_bias));
    reg.add_internal_call("internal_m2n_model_get_shared_material",
                            script_internal_call(internal_m2n_model_get_shared_material));
    reg.add_internal_call("internal_m2n_model_get_shared_material_count",
                            script_internal_call(internal_m2n_model_get_shared_material_count));
    reg.add_internal_call("internal_m2n_model_set_shared_material",
                            script_internal_call(internal_m2n_model_set_shared_material));

    auto instance_reg = dotnet::internal_call_registry("Unravel.Core.MaterialInstance");
    instance_reg.add_internal_call("internal_m2n_material_instance_set_color",
                                   script_internal_call(internal_m2n_material_instance_set_color));
    instance_reg.add_internal_call("internal_m2n_material_instance_get_color",
                                   script_internal_call(internal_m2n_material_instance_get_color));
    instance_reg.add_internal_call("internal_m2n_material_instance_set_float",
                                   script_internal_call(internal_m2n_material_instance_set_float));
    instance_reg.add_internal_call("internal_m2n_material_instance_get_float",
                                   script_internal_call(internal_m2n_material_instance_get_float));
    instance_reg.add_internal_call("internal_m2n_material_instance_set_vector",
                                   script_internal_call(internal_m2n_material_instance_set_vector));
    instance_reg.add_internal_call("internal_m2n_material_instance_get_vector",
                                   script_internal_call(internal_m2n_material_instance_get_vector));
    instance_reg.add_internal_call("internal_m2n_material_instance_set_texture",
                                   script_internal_call(internal_m2n_material_instance_set_texture));
    instance_reg.add_internal_call("internal_m2n_material_instance_get_texture",
                                   script_internal_call(internal_m2n_material_instance_get_texture));
    instance_reg.add_internal_call("internal_m2n_material_instance_set_mode",
                                   script_internal_call(internal_m2n_material_instance_set_mode));
    instance_reg.add_internal_call("internal_m2n_material_instance_get_mode",
                                   script_internal_call(internal_m2n_material_instance_get_mode));
    instance_reg.add_internal_call("internal_m2n_material_instance_is_overridden",
                                   script_internal_call(internal_m2n_material_instance_is_overridden));
    instance_reg.add_internal_call("internal_m2n_material_instance_revert",
                                   script_internal_call(internal_m2n_material_instance_revert));
    instance_reg.add_internal_call("internal_m2n_material_instance_revert_all",
                                   script_internal_call(internal_m2n_material_instance_revert_all));
    instance_reg.add_internal_call("internal_m2n_material_instance_has_overrides",
                                   script_internal_call(internal_m2n_material_instance_has_overrides));
    instance_reg.add_internal_call("internal_m2n_material_instance_create",
                                   script_internal_call(internal_m2n_material_instance_create));
    instance_reg.add_internal_call("internal_m2n_material_instance_release",
                                   script_internal_call(internal_m2n_material_instance_release));
    instance_reg.add_internal_call("internal_m2n_material_instance_shares_draw",
                                   script_internal_call(internal_m2n_material_instance_shares_draw));
    instance_reg.add_internal_call("internal_m2n_material_instance_has_parameter",
                                   script_internal_call(internal_m2n_material_instance_has_parameter));
    instance_reg.add_internal_call("internal_m2n_material_instance_get_parameter",
                                   script_internal_call(internal_m2n_material_instance_get_parameter));
    instance_reg.add_internal_call("internal_m2n_material_instance_set_parameter",
                                   script_internal_call(internal_m2n_material_instance_set_parameter));
    instance_reg.add_internal_call("internal_m2n_material_instance_has_texture_node",
                                   script_internal_call(internal_m2n_material_instance_has_texture_node));
    instance_reg.add_internal_call("internal_m2n_material_instance_get_texture_by_name",
                                   script_internal_call(internal_m2n_material_instance_get_texture_by_name));
    instance_reg.add_internal_call("internal_m2n_material_instance_set_texture_by_name",
                                   script_internal_call(internal_m2n_material_instance_set_texture_by_name));
    instance_reg.add_internal_call("internal_m2n_material_instance_is_name_overridden",
                                   script_internal_call(internal_m2n_material_instance_is_name_overridden));
    instance_reg.add_internal_call("internal_m2n_material_instance_revert_name",
                                   script_internal_call(internal_m2n_material_instance_revert_name));
}

} // namespace unravel
