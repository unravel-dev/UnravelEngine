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

void internal_m2n_graphics_draw_mesh_instanced(const hpp::uuid& mesh_uid,
                                               const hpp::uuid& material_uid,
                                               std::intptr_t transforms,
                                               int count,
                                               std::intptr_t previous,
                                               bool casts_shadow,
                                               int layer_mask)
{
    if(!require_script_thread("Graphics.DrawMeshInstanced") || count <= 0 || transforms == 0)
    {
        return;
    }
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    instanced_draw_list::draw_settings settings;
    settings.render_mesh = am.get_asset<mesh>(mesh_uid);
    if(!material_uid.is_nil())
    {
        settings.render_material = am.get_asset<material>(material_uid);
    }
    settings.casts_shadow = casts_shadow;
    settings.layer_mask = layer_mask;
    ctx.get_cached<rendering_system>().get_instanced_draws().add_unaligned(settings,
                                                                          reinterpret_cast<const float*>(transforms),
                                                                          size_t(count),
                                                                          reinterpret_cast<const float*>(previous));
}

} // namespace

void register_graphics_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.Graphics");
    reg.add_internal_call("internal_m2n_graphics_draw_mesh_instanced",
                          dotnet_internal_call(internal_m2n_graphics_draw_mesh_instanced));
}

} // namespace unravel
