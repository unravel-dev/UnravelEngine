#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/rendering/ecs/components/decal_component.h>

namespace unravel
{
namespace
{

auto internal_m2n_decal_get_material(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        return comp->get_material().uid();
    }
    return {};
}

void internal_m2n_decal_set_material(entt::entity id, const hpp::uuid& uid)
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        auto& am = engine::context().get_cached<asset_manager>();
        comp->set_material(uid.is_nil() ? asset_handle<material>{} : am.get_asset<material>(uid));
    }
}

auto internal_m2n_decal_get_size(entt::entity id) -> math::vec2
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        return comp->size;
    }
    return {};
}

void internal_m2n_decal_set_size(entt::entity id, const math::vec2& size)
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        comp->size = math::max(size, math::vec2(0.001f));
    }
}

auto internal_m2n_decal_get_angle_fade(entt::entity id) -> math::vec2
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        return comp->angle_fade;
    }
    return {};
}

void internal_m2n_decal_set_angle_fade(entt::entity id, const math::vec2& angles)
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        comp->angle_fade = math::clamp(angles, math::vec2(0.0f), math::vec2(180.0f));
    }
}

/// The float field @p field of decal @p id: what the C# properties read and write by index (decal_float_field).
enum class decal_float_field : int32_t
{
    depth,
    opacity,
    fade_screen_size,
    max_distance,
    lifetime,
    fade_in,
    fade_out,
    age,
};

auto get_decal_float(decal_component& comp, decal_float_field field) -> float*
{
    switch(field)
    {
        case decal_float_field::depth:
            return &comp.depth;
        case decal_float_field::opacity:
            return &comp.opacity;
        case decal_float_field::fade_screen_size:
            return &comp.fade_screen_size;
        case decal_float_field::max_distance:
            return &comp.max_distance;
        case decal_float_field::lifetime:
            return &comp.lifetime;
        case decal_float_field::fade_in:
            return &comp.fade_in;
        case decal_float_field::fade_out:
            return &comp.fade_out;
        case decal_float_field::age:
            return &comp.age;
    }
    return nullptr;
}

auto internal_m2n_decal_get_float(entt::entity id, int32_t field) -> float
{
    auto comp = safe_get_component<decal_component>(id);
    const auto* value = comp ? get_decal_float(*comp, decal_float_field(field)) : nullptr;
    return value ? *value : 0.0f;
}

void internal_m2n_decal_set_float(entt::entity id, int32_t field, float value)
{
    auto comp = safe_get_component<decal_component>(id);
    if(auto* target = comp ? get_decal_float(*comp, decal_float_field(field)) : nullptr)
    {
        switch(decal_float_field(field))
        {
            case decal_float_field::depth:
                *target = std::max(value, 0.001f);
                break;
            case decal_float_field::opacity:
                *target = std::clamp(value, 0.0f, 1.0f);
                break;
            default:
                *target = std::max(value, 0.0f);
                break;
        }
    }
}

auto internal_m2n_decal_get_sort_order(entt::entity id) -> int32_t
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        return comp->sort_order;
    }
    return 0;
}

void internal_m2n_decal_set_sort_order(entt::entity id, int32_t order)
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        comp->sort_order = order;
    }
}

auto internal_m2n_decal_get_remove_after_fade(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        return comp->remove_after_fade;
    }
    return false;
}

void internal_m2n_decal_set_remove_after_fade(entt::entity id, bool remove)
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        comp->remove_after_fade = remove;
    }
}

/// Decals.Spawn: a transient decal on the surface at @p point, facing into it, fading out over @p fade_out after
/// @p lifetime (0: it stays), removed then, under @p attach_to when valid (it follows that entity's moves).
auto internal_m2n_decals_spawn(const hpp::uuid& material_uid,
                               const math::vec3& point,
                               const math::vec3& normal,
                               const math::vec2& size,
                               float rotation,
                               float lifetime,
                               float fade_out,
                               entt::entity attach_to) -> entt::entity
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    // In the scene of the entity it follows: parents and children share a registry.
    const auto parent = get_entity_from_id(attach_to);
    auto* parent_scene = parent ? scene::get_scene(parent) : nullptr;
    auto& scn = parent_scene != nullptr ? *parent_scene : ctx.get_cached<ecs>().get_scene();
    const auto chosen = material_uid.is_nil() ? asset_handle<material>{} : am.get_asset<material>(material_uid);
    auto decal = defaults::create_decal_entity(ctx, scn, "Decal", chosen);
    auto& comp = decal.get<decal_component>();
    comp.resize(size);
    comp.lifetime = std::max(lifetime, 0.0f);
    comp.fade_out = std::max(fade_out, 0.0f);
    comp.remove_after_fade = lifetime > 0.0f;
    comp.is_transient = true;
    defaults::place_decal_on_surface(decal, point, normal, rotation);
    if(parent)
    {
        decal.get<transform_component>().set_parent(parent, true);
    }
    return decal.entity();
}

auto internal_m2n_decal_get_receiver_layers(entt::entity id) -> int32_t
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        return comp->receiver_layers.mask;
    }
    return 0;
}

void internal_m2n_decal_set_receiver_layers(entt::entity id, int32_t mask)
{
    if(auto comp = safe_get_component<decal_component>(id))
    {
        comp->receiver_layers.mask = mask;
    }
}

auto internal_m2n_decals_get_max_transient() -> int32_t
{
    return int32_t(decal_component::max_transient_decals);
}

void internal_m2n_decals_set_max_transient(int32_t count)
{
    decal_component::max_transient_decals = uint32_t(std::max(count, 0));
}

} // namespace

void register_decal_component_script_bindings()
{
    auto reg = dotnet::internal_call_registry("Unravel.Core.DecalComponent");
    reg.add_internal_call("internal_m2n_decal_get_material", script_internal_call(internal_m2n_decal_get_material));
    reg.add_internal_call("internal_m2n_decal_set_material", script_internal_call(internal_m2n_decal_set_material));
    reg.add_internal_call("internal_m2n_decal_get_size", script_internal_call(internal_m2n_decal_get_size));
    reg.add_internal_call("internal_m2n_decal_set_size", script_internal_call(internal_m2n_decal_set_size));
    reg.add_internal_call("internal_m2n_decal_get_angle_fade", script_internal_call(internal_m2n_decal_get_angle_fade));
    reg.add_internal_call("internal_m2n_decal_set_angle_fade", script_internal_call(internal_m2n_decal_set_angle_fade));
    reg.add_internal_call("internal_m2n_decal_get_float", script_internal_call(internal_m2n_decal_get_float));
    reg.add_internal_call("internal_m2n_decal_set_float", script_internal_call(internal_m2n_decal_set_float));
    reg.add_internal_call("internal_m2n_decal_get_sort_order", script_internal_call(internal_m2n_decal_get_sort_order));
    reg.add_internal_call("internal_m2n_decal_set_sort_order", script_internal_call(internal_m2n_decal_set_sort_order));
    reg.add_internal_call("internal_m2n_decal_get_remove_after_fade", script_internal_call(internal_m2n_decal_get_remove_after_fade));
    reg.add_internal_call("internal_m2n_decal_set_remove_after_fade", script_internal_call(internal_m2n_decal_set_remove_after_fade));
    reg.add_internal_call("internal_m2n_decal_get_receiver_layers", script_internal_call(internal_m2n_decal_get_receiver_layers));
    reg.add_internal_call("internal_m2n_decal_set_receiver_layers", script_internal_call(internal_m2n_decal_set_receiver_layers));

    auto decals = dotnet::internal_call_registry("Unravel.Core.Decals");
    decals.add_internal_call("internal_m2n_decals_spawn", script_internal_call(internal_m2n_decals_spawn));
    decals.add_internal_call("internal_m2n_decals_get_max_transient", script_internal_call(internal_m2n_decals_get_max_transient));
    decals.add_internal_call("internal_m2n_decals_set_max_transient", script_internal_call(internal_m2n_decals_set_max_transient));
}

} // namespace unravel
