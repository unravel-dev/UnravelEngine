#include "script_glue_common.h"
#include "script_bindings.h"
#include "material_script_helpers.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/model_component.h>
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

/// The value slot @p slot of the model draws with for @p property: its material instance's override, else the shared
/// material's.
auto get_instance_value(entt::entity id, uint32_t slot, int32_t property) -> entt::meta_any
{
    auto comp = safe_get_component<model_component>(id);
    return comp ? comp->get_material_instance(slot).get_value(get_material_property_id(property)) : entt::meta_any{};
}

/// Overrides @p property of the slot's shared material for this model only.
void set_instance_value(entt::entity id, uint32_t slot, int32_t property, const entt::meta_any& value)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        comp->set_material_instance_value(slot, get_material_property_id(property), value);
    }
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
    auto comp = safe_get_component<model_component>(id);
    return comp && comp->get_material_instance(slot).is_overridden(get_material_property_id(property));
}

void internal_m2n_material_instance_revert(entt::entity id, uint32_t slot, int32_t property)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        comp->revert_material_instance_value(slot, get_material_property_id(property));
    }
}

void internal_m2n_material_instance_revert_all(entt::entity id, uint32_t slot)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        comp->revert_material_instance(slot);
    }
}

auto internal_m2n_material_instance_has_overrides(entt::entity id, uint32_t slot) -> bool
{
    auto comp = safe_get_component<model_component>(id);
    return comp && comp->get_material_instance(slot).has_overrides();
}

auto internal_m2n_material_instance_shares_draw(entt::entity id, uint32_t slot) -> bool
{
    auto comp = safe_get_component<model_component>(id);
    return comp && comp->get_material_instance(slot).shares_draw();
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
    instance_reg.add_internal_call("internal_m2n_material_instance_shares_draw",
                                   script_internal_call(internal_m2n_material_instance_shares_draw));
}

} // namespace unravel
