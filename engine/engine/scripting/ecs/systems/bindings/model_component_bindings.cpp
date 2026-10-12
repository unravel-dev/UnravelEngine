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
        return comp->get_model().get_material(index).uid();
    }

    return {};
}

auto internal_m2n_model_get_shared_material_count(entt::entity id) -> int
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_materials().size();
    }

    return {};
}

auto internal_m2n_model_get_material_instance(entt::entity id, uint32_t index)
    -> dotnetpp_backend::managed_interface::material_properties
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        auto instance = comp->get_model().get_material_instance(index);
        return get_material_properties(instance);
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

void internal_m2n_model_set_material_instance(entt::entity id,
                                              const dotnetpp_backend::managed_interface::material_properties& props,
                                              uint32_t index)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        if(props.valid)
        {
            set_material_properties(comp->get_or_emplace_material_instance(index), props);
        }
        else
        {
            comp->set_material_instance(nullptr, index);
        }
    }
}

auto internal_m2n_model_get_material_instance_count(entt::entity id) -> int
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_material_instances().size();
    }

    return {};
}

/// Material fields a script reads or writes one at a time. Matches the managed MaterialProperty
/// enum; the managed side checks that a field is used with the setter of its type.
enum class material_property : int32_t
{
    base_color,
    emissive_color,
    subsurface_color,
    emissive_intensity,
    roughness,
    metalness,
    bumpiness,
    alpha_cutoff,
    tiling,
    color_map,
    normal_map,
    roughness_map,
    metalness_map,
    emissive_map,
    ao_map,
    alpha_mode,
    cull_mode,
};

using managed_color = dotnetpp_backend::managed_interface::color;
using managed_vector2 = dotnetpp_backend::managed_interface::vector2;
using texture_handle = asset_handle<gfx::texture>;

/// The material a slot renders with (its instance, else the shared asset), when it is PBR.
auto find_slot_material(entt::entity id, uint32_t slot) -> std::shared_ptr<pbr_material>
{
    auto comp = safe_get_component<model_component>(id);
    if(!comp)
    {
        return {};
    }
    auto material = comp->get_model().get_material_instance(slot);
    if(!material || !material->is<pbr_material>())
    {
        return {};
    }
    return std::static_pointer_cast<pbr_material>(material);
}

/// The slot's own material instance, copied from the shared material on first use, when it is PBR.
auto edit_slot_material(entt::entity id, uint32_t slot) -> std::shared_ptr<pbr_material>
{
    auto comp = safe_get_component<model_component>(id);
    if(!comp)
    {
        return {};
    }
    auto material = comp->get_or_emplace_material_instance(slot);
    if(!material || !material->is<pbr_material>())
    {
        return {};
    }
    return std::static_pointer_cast<pbr_material>(material);
}

auto get_texture_slot(pbr_material& material, material_property property) -> const texture_handle*
{
    switch(property)
    {
        case material_property::color_map:
            return &material.get_color_map();
        case material_property::normal_map:
            return &material.get_normal_map();
        case material_property::roughness_map:
            return &material.get_roughness_map();
        case material_property::metalness_map:
            return &material.get_metalness_map();
        case material_property::emissive_map:
            return &material.get_emissive_map();
        case material_property::ao_map:
            return &material.get_ao_map();
        default:
            return nullptr;
    }
}

void internal_m2n_model_set_material_float(entt::entity id, int32_t property, float value, uint32_t slot)
{
    auto material = edit_slot_material(id, slot);
    if(!material)
    {
        return;
    }
    switch(static_cast<material_property>(property))
    {
        case material_property::emissive_intensity:
            material->set_emissive_intensity(value);
            break;
        case material_property::roughness:
            material->set_roughness(value);
            break;
        case material_property::metalness:
            material->set_metalness(value);
            break;
        case material_property::bumpiness:
            material->set_bumpiness(value);
            break;
        case material_property::alpha_cutoff:
            material->set_alpha_cutoff(value);
            break;
        default:
            break;
    }
}

auto internal_m2n_model_get_material_float(entt::entity id, int32_t property, uint32_t slot) -> float
{
    auto material = find_slot_material(id, slot);
    if(!material)
    {
        return 0.0f;
    }
    switch(static_cast<material_property>(property))
    {
        case material_property::emissive_intensity:
            return material->get_emissive_intensity();
        case material_property::roughness:
            return material->get_roughness();
        case material_property::metalness:
            return material->get_metalness();
        case material_property::bumpiness:
            return material->get_bumpiness();
        case material_property::alpha_cutoff:
            return material->get_alpha_cutoff();
        default:
            return 0.0f;
    }
}

void internal_m2n_model_set_material_color(entt::entity id, int32_t property, const managed_color& value, uint32_t slot)
{
    using converter = dotnet::managed_interface::converter;
    auto material = edit_slot_material(id, slot);
    if(!material)
    {
        return;
    }
    const auto color = converter::convert<managed_color, math::color>(value);
    switch(static_cast<material_property>(property))
    {
        case material_property::base_color:
            material->set_base_color(color);
            break;
        case material_property::emissive_color:
            material->set_emissive_color(color);
            break;
        case material_property::subsurface_color:
            material->set_subsurface_color(color);
            break;
        default:
            break;
    }
}

auto internal_m2n_model_get_material_color(entt::entity id, int32_t property, uint32_t slot) -> managed_color
{
    using converter = dotnet::managed_interface::converter;
    auto material = find_slot_material(id, slot);
    if(!material)
    {
        return {};
    }
    switch(static_cast<material_property>(property))
    {
        case material_property::base_color:
            return converter::convert<math::color, managed_color>(material->get_base_color());
        case material_property::emissive_color:
            return converter::convert<math::color, managed_color>(material->get_emissive_color());
        case material_property::subsurface_color:
            return converter::convert<math::color, managed_color>(material->get_subsurface_color());
        default:
            return {};
    }
}

void internal_m2n_model_set_material_vector(entt::entity id,
                                            int32_t property,
                                            const managed_vector2& value,
                                            uint32_t slot)
{
    using converter = dotnet::managed_interface::converter;
    auto material = edit_slot_material(id, slot);
    if(material && static_cast<material_property>(property) == material_property::tiling)
    {
        material->set_tiling(converter::convert<managed_vector2, math::vec2>(value));
    }
}

auto internal_m2n_model_get_material_vector(entt::entity id, int32_t property, uint32_t slot) -> managed_vector2
{
    using converter = dotnet::managed_interface::converter;
    auto material = find_slot_material(id, slot);
    if(material && static_cast<material_property>(property) == material_property::tiling)
    {
        return converter::convert<math::vec2, managed_vector2>(material->get_tiling());
    }
    return {};
}

void internal_m2n_model_set_material_texture(entt::entity id, int32_t property, const hpp::uuid& uid, uint32_t slot)
{
    auto material = edit_slot_material(id, slot);
    if(!material)
    {
        return;
    }
    texture_handle texture;
    if(!uid.is_nil())
    {
        auto& am = engine::context().get_cached<asset_manager>();
        texture = am.get_asset<gfx::texture>(uid);
    }
    switch(static_cast<material_property>(property))
    {
        case material_property::color_map:
            material->set_color_map(texture);
            break;
        case material_property::normal_map:
            material->set_normal_map(texture);
            break;
        case material_property::roughness_map:
            material->set_roughness_map(texture);
            break;
        case material_property::metalness_map:
            material->set_metalness_map(texture);
            break;
        case material_property::emissive_map:
            material->set_emissive_map(texture);
            break;
        case material_property::ao_map:
            material->set_ao_map(texture);
            break;
        default:
            break;
    }
}

auto internal_m2n_model_get_material_texture(entt::entity id, int32_t property, uint32_t slot) -> hpp::uuid
{
    auto material = find_slot_material(id, slot);
    if(!material)
    {
        return {};
    }
    const auto* texture = get_texture_slot(*material, static_cast<material_property>(property));
    return texture ? texture->uid() : hpp::uuid{};
}

void internal_m2n_model_set_material_mode(entt::entity id, int32_t property, int32_t value, uint32_t slot)
{
    auto material = edit_slot_material(id, slot);
    if(!material)
    {
        return;
    }
    switch(static_cast<material_property>(property))
    {
        case material_property::alpha_mode:
            material->set_alpha_mode(static_cast<alpha_mode>(value));
            break;
        case material_property::cull_mode:
            material->set_cull_type(static_cast<cull_type>(value));
            break;
        default:
            break;
    }
}

auto internal_m2n_model_get_material_mode(entt::entity id, int32_t property, uint32_t slot) -> int32_t
{
    auto material = find_slot_material(id, slot);
    if(!material)
    {
        return 0;
    }
    switch(static_cast<material_property>(property))
    {
        case material_property::alpha_mode:
            return static_cast<int32_t>(material->get_alpha_mode());
        case material_property::cull_mode:
            return static_cast<int32_t>(material->get_cull_type());
        default:
            return 0;
    }
}

//------------------------------

} // namespace

void register_model_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.ModelComponent");
    reg.add_internal_call("internal_m2n_model_get_enabled", dotnet_internal_call(internal_m2n_model_get_enabled));
    reg.add_internal_call("internal_m2n_model_set_enabled", dotnet_internal_call(internal_m2n_model_set_enabled));
    reg.add_internal_call("internal_m2n_model_get_casts_shadow",
                          dotnet_internal_call(internal_m2n_model_get_casts_shadow));
    reg.add_internal_call("internal_m2n_model_set_casts_shadow",
                          dotnet_internal_call(internal_m2n_model_set_casts_shadow));
    reg.add_internal_call("internal_m2n_model_get_static", dotnet_internal_call(internal_m2n_model_get_static));
    reg.add_internal_call("internal_m2n_model_set_static", dotnet_internal_call(internal_m2n_model_set_static));
    reg.add_internal_call("internal_m2n_model_get_mesh", dotnet_internal_call(internal_m2n_model_get_mesh));
    reg.add_internal_call("internal_m2n_model_set_mesh", dotnet_internal_call(internal_m2n_model_set_mesh));
    reg.add_internal_call("internal_m2n_model_get_lod_selection_bias",
                          dotnet_internal_call(internal_m2n_model_get_lod_selection_bias));
    reg.add_internal_call("internal_m2n_model_set_lod_selection_bias",
                          dotnet_internal_call(internal_m2n_model_set_lod_selection_bias));
    reg.add_internal_call("internal_m2n_model_get_shared_material",
                            dotnet_internal_call(internal_m2n_model_get_shared_material));
    reg.add_internal_call("internal_m2n_model_get_shared_material_count",
                            dotnet_internal_call(internal_m2n_model_get_shared_material_count));
    reg.add_internal_call("internal_m2n_model_set_shared_material",
                            dotnet_internal_call(internal_m2n_model_set_shared_material));
    reg.add_internal_call("internal_m2n_model_set_material_instance",
                            dotnet_internal_call(internal_m2n_model_set_material_instance));
    reg.add_internal_call("internal_m2n_model_get_material_instance",
                            dotnet_internal_call(internal_m2n_model_get_material_instance));
    reg.add_internal_call("internal_m2n_model_get_material_instance_count",
                            dotnet_internal_call(internal_m2n_model_get_material_instance_count));
    reg.add_internal_call("internal_m2n_model_set_material_float",
                          dotnet_internal_call(internal_m2n_model_set_material_float));
    reg.add_internal_call("internal_m2n_model_get_material_float",
                          dotnet_internal_call(internal_m2n_model_get_material_float));
    reg.add_internal_call("internal_m2n_model_set_material_color",
                          dotnet_internal_call(internal_m2n_model_set_material_color));
    reg.add_internal_call("internal_m2n_model_get_material_color",
                          dotnet_internal_call(internal_m2n_model_get_material_color));
    reg.add_internal_call("internal_m2n_model_set_material_vector",
                          dotnet_internal_call(internal_m2n_model_set_material_vector));
    reg.add_internal_call("internal_m2n_model_get_material_vector",
                          dotnet_internal_call(internal_m2n_model_get_material_vector));
    reg.add_internal_call("internal_m2n_model_set_material_texture",
                          dotnet_internal_call(internal_m2n_model_set_material_texture));
    reg.add_internal_call("internal_m2n_model_get_material_texture",
                          dotnet_internal_call(internal_m2n_model_get_material_texture));
    reg.add_internal_call("internal_m2n_model_set_material_mode",
                          dotnet_internal_call(internal_m2n_model_set_material_mode));
    reg.add_internal_call("internal_m2n_model_get_material_mode",
                          dotnet_internal_call(internal_m2n_model_get_material_mode));
}

} // namespace unravel
