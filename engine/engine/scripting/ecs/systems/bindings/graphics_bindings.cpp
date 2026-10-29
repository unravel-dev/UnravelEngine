#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>

#include <cstdint>

namespace unravel
{
namespace
{

/// The material a draw draws with: material instance @p material_instance (an id of
/// rendering_system::create_material_instance, 0 for none) in place of its shared material, else @p material_uid.
void set_draw_material(instanced_draw_list::draw_settings& settings,
                       const hpp::uuid& material_uid,
                       int material_instance)
{
    auto& ctx = engine::context();
    if(material_instance != 0)
    {
        settings.render_material_instance =
            ctx.get_cached<rendering_system>().find_material_instance(uint32_t(material_instance));
    }
    if(settings.render_material_instance)
    {
        settings.render_material = settings.render_material_instance->get_shared_material();
        return;
    }
    if(!material_uid.is_nil())
    {
        settings.render_material = ctx.get_cached<asset_manager>().get_asset<material>(material_uid);
    }
}

void internal_m2n_graphics_draw_mesh_instanced(const hpp::uuid& mesh_uid,
                                               const hpp::uuid& material_uid,
                                               int material_instance,
                                               std::intptr_t transforms,
                                               int count,
                                               std::intptr_t previous,
                                               bool casts_shadow,
                                               int layer_mask)
{
    if(count <= 0 || transforms == 0)
    {
        return;
    }
    auto& ctx = engine::context();
    instanced_draw_list::draw_settings settings;
    settings.render_mesh = ctx.get_cached<asset_manager>().get_asset<mesh>(mesh_uid);
    set_draw_material(settings, material_uid, material_instance);
    settings.casts_shadow = casts_shadow;
    settings.layer_mask = layer_mask;
    ctx.get_cached<rendering_system>().get_instanced_draws().add_unaligned(settings,
                                                                          reinterpret_cast<const float*>(transforms),
                                                                          size_t(count),
                                                                          reinterpret_cast<const float*>(previous));
}

void internal_m2n_graphics_draw_baked_instanced(int animation_id,
                                                const hpp::uuid& material_uid,
                                                int material_instance,
                                                std::intptr_t transforms,
                                                std::intptr_t states,
                                                int count,
                                                std::intptr_t previous,
                                                std::intptr_t previous_states,
                                                bool casts_shadow,
                                                int layer_mask)
{
    if(count <= 0 || transforms == 0 || states == 0)
    {
        return;
    }
    auto& ctx = engine::context();
    auto& rendering = ctx.get_cached<rendering_system>();
    auto animation = rendering.get_baked_animations().find(uint32_t(animation_id));
    if(!animation)
    {
        return;
    }
    instanced_draw_list::draw_settings settings;
    set_draw_material(settings, material_uid, material_instance);
    settings.casts_shadow = casts_shadow;
    settings.layer_mask = layer_mask;
    rendering.get_instanced_draws().add_baked(settings,
                                              std::move(animation),
                                              reinterpret_cast<const float*>(transforms),
                                              reinterpret_cast<const baked_animation_state*>(states),
                                              size_t(count),
                                              reinterpret_cast<const float*>(previous),
                                              reinterpret_cast<const baked_animation_state*>(previous_states));
}

auto internal_m2n_baked_animation_create(const hpp::uuid& mesh_uid, float frames_per_second, int retarget) -> int
{
    auto& ctx = engine::context();
    const auto render_mesh = ctx.get_cached<asset_manager>().get_asset<mesh>(mesh_uid);
    const auto retarget_mode = retarget == int(baked_animation_retarget::rest_aligned)
                                   ? baked_animation_retarget::rest_aligned
                                   : baked_animation_retarget::copy;
    return int(ctx.get_cached<rendering_system>().get_baked_animations().create(render_mesh,
                                                                                frames_per_second,
                                                                                retarget_mode));
}

auto internal_m2n_baked_animation_add_clip(int id, const hpp::uuid& clip_uid) -> int
{
    auto& ctx = engine::context();
    const auto clip = ctx.get_cached<asset_manager>().get_asset<animation_clip>(clip_uid);
    return ctx.get_cached<rendering_system>().get_baked_animations().add_clip(uint32_t(id), clip);
}

auto internal_m2n_baked_animation_get_clip_length(int id, int clip) -> float
{
    const auto animation = engine::context().get_cached<rendering_system>().get_baked_animations().find(uint32_t(id));
    return animation ? animation->get_clip_length(clip) : 0.0f;
}

void internal_m2n_baked_animation_release(int id)
{
    engine::context().get_cached<rendering_system>().get_baked_animations().release(uint32_t(id));
}

auto internal_m2n_baked_animation_is_valid(int id) -> bool
{
    return engine::context().get_cached<rendering_system>().get_baked_animations().find(uint32_t(id)) != nullptr;
}

} // namespace

void register_graphics_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.Graphics");
    reg.add_internal_call("internal_m2n_graphics_draw_mesh_instanced",
                          script_internal_call(internal_m2n_graphics_draw_mesh_instanced));
    reg.add_internal_call("internal_m2n_graphics_draw_baked_instanced",
                          script_internal_call(internal_m2n_graphics_draw_baked_instanced));

    auto baked = dotnet::internal_call_registry("Unravel.Core.BakedAnimation");
    baked.add_internal_call("internal_m2n_baked_animation_create", script_internal_call(internal_m2n_baked_animation_create));
    baked.add_internal_call("internal_m2n_baked_animation_add_clip", script_internal_call(internal_m2n_baked_animation_add_clip));
    baked.add_internal_call("internal_m2n_baked_animation_get_clip_length",
                            script_internal_call(internal_m2n_baked_animation_get_clip_length));
    baked.add_internal_call("internal_m2n_baked_animation_release", script_internal_call(internal_m2n_baked_animation_release));
    baked.add_internal_call("internal_m2n_baked_animation_is_valid", script_internal_call(internal_m2n_baked_animation_is_valid));
}

} // namespace unravel
