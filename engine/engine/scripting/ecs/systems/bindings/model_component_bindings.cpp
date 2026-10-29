#include "script_glue_common.h"
#include "script_bindings.h"
#include "material_script_helpers.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
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

/// The material instance a script's MaterialInstance stands for: model @p id's of slot @p slot, or the material
/// instance of no renderer @p slot when @p id is none (is_standalone_instance). Null when it is gone.
auto find_instance(entt::entity id, uint32_t slot) -> const material_instance*
{
    if(is_standalone_instance(id))
    {
        // rendering_system keeps it alive until its script releases it.
        return find_standalone_instance(slot).get();
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
    if(auto comp = safe_get_component<model_component>(id))
    {
        change_model(*comp);
    }
}

/// The value slot @p slot of the model draws with for @p property: its material instance's override, else the shared
/// material's.
auto get_instance_value(entt::entity id, uint32_t slot, int32_t property) -> entt::meta_any
{
    const auto* instance = find_instance(id, slot);
    return instance ? instance->get_value(get_material_property_id(property)) : entt::meta_any{};
}

/// Overrides @p property of the slot's shared material for this model only.
void set_instance_value(entt::entity id, uint32_t slot, int32_t property, const entt::meta_any& value)
{
    const entt::id_type property_id = get_material_property_id(property);
    change_instance(
        id,
        slot,
        [&](material_instance& instance)
        {
            instance.set_override(property_id, value);
        },
        [&](model_component& comp)
        {
            comp.set_material_instance_value(slot, property_id, value);
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

void internal_m2n_material_instance_set_mode(entt::entity id, uint32_t slot, int32_t property, int32_t value)
{
    set_instance_value(id, slot, property, make_mode_value(property, value));
}

auto internal_m2n_material_instance_get_mode(entt::entity id, uint32_t slot, int32_t property) -> int32_t
{
    return read_mode_value(get_instance_value(id, slot, property));
}

auto internal_m2n_material_instance_is_overridden(entt::entity id, uint32_t slot, int32_t property) -> bool
{
    const auto* instance = find_instance(id, slot);
    return instance && instance->is_overridden(get_material_property_id(property));
}

void internal_m2n_material_instance_revert(entt::entity id, uint32_t slot, int32_t property)
{
    const entt::id_type property_id = get_material_property_id(property);
    change_instance(
        id,
        slot,
        [&](material_instance& instance)
        {
            instance.revert(property_id);
        },
        [&](model_component& comp)
        {
            comp.revert_material_instance_value(slot, property_id);
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
}

} // namespace unravel
