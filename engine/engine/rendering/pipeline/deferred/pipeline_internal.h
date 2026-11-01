#pragma once

#include "pipeline.h"
#include "glm/ext/scalar_integer.hpp"
#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/fog_volume_component.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/gi/gi_pass_constants.h>
#include <engine/rendering/perez_luminance.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/ssil_component.h>
#include <engine/rendering/ecs/components/tonemapping_component.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/rendering/ecs/systems/skylight_system.h>
#include <engine/rendering/default_textures.h>
#include <engine/engine.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
#include <engine/rendering/pipeline/passes/pass_helpers.h>
#include <engine/rendering/render_scale.h>
#include <engine/rendering/renderer.h>
#include <engine/rendering/skinning_palette_buffer.h>
#include <engine/settings/settings.h>
#include <hpp/optional.hpp>

#include <graphics/index_buffer.h>
#include <graphics/device_caps.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>
#include <graphics/render_view.h>
#include <graphics/texture.h>
#include <graphics/vertex_buffer.h>

namespace unravel
{
namespace rendering
{
/// The helpers that more than one of the deferred pipeline's files use (pipeline.cpp, pipeline_*.cpp).
namespace deferred_detail
{
/// Border fade of the cloud shadow map in map space (fraction of the half extent).
constexpr float cloud_shadow_border_fade = 0.08f;
/// Slots of deferred::color_lighting_pcf_tent_.
constexpr size_t pcf_tent3_slot = 0;
constexpr size_t pcf_tent7_slot = 1;

/// Whether a light of @p intensity and @p linear_color adds anything to a pixel. One that does not still costs a
/// full G-buffer read per covered pixel, and its shadow maps a pass per caster.
inline auto is_adding_light(float intensity, const math::vec3& linear_color) -> bool
{
    return intensity > 0.0f && std::max({linear_color.x, linear_color.y, linear_color.z}) > 0.0f;
}

/// Whether the light pass and the shadow maps need @p l: it adds light, or the shadow coverage view shows its
/// shadow maps, which that view draws whatever the intensity.
inline auto is_light_needed(const light& l, const math::vec3& linear_color) -> bool
{
    const bool shows_coverage = l.casts_shadows && l.shadow_params.show_coverage;
    return shows_coverage || is_adding_light(l.intensity, linear_color);
}

/// The cloud_shadow.sh uniforms of @p shadow (u_cloudShadow, u_cloudShadow2) and whether the lights apply it.
struct cloud_shadow_uniforms
{
    math::vec4 placement{0.0f};
    math::vec4 layer{0.0f};
    bool is_applied = false;
};

inline auto make_cloud_shadow_uniforms(const atmospheric_pass_perez::cloud_shadow_result& shadow) -> cloud_shadow_uniforms
{
    cloud_shadow_uniforms uniforms;
    uniforms.is_applied = shadow.valid && shadow.apply_to_lights && shadow.map;
    uniforms.placement = math::vec4(shadow.origin.x, shadow.origin.y, 1.0f / std::max(shadow.extent, 1.0f), shadow.opacity);
    uniforms.layer = math::vec4(uniforms.is_applied ? 1.0f : 0.0f, shadow.base_world_y, cloud_shadow_border_fade, 0.0f);
    return uniforms;
}
// Returns whether the intermediate G/L/R buffers should be RGBA16F. Tonemapping
// implies HDR, and probe captures force it explicitly (they strip the post stack
// but still light in HDR for the cubemap).
inline auto wants_hdr_buffers(const pipeline::run_params& params) -> bool
{
    return static_cast<bool>(params.fill_hdr_params) || params.force_hdr_buffers;
}

inline auto get_default_format() -> bgfx::TextureFormat::Enum
{
    return bgfx::TextureFormat::RGBA8;
}

inline auto get_default_hdr_format() -> bgfx::TextureFormat::Enum
{
    return bgfx::TextureFormat::RGBA16F;
}

// The format of the targets that hold light: the emissive, the light buffer and bloom. RG11B10F is half the size of
// RGBA16F, and what tile-based GPUs compress where they store RGBA16F raw. It has no alpha and no negative values, and
// temporal AA, SSR, SSIL, GI and probe captures read or copy the light buffer as RGBA16F, so they keep that.
inline auto get_light_target_format(const pipeline::run_params& params) -> bgfx::TextureFormat::Enum
{
    if(!wants_hdr_buffers(params))
    {
        return get_default_format();
    }
    const bool can_use_compact_format = !params.limits.full_precision_light_targets &&
                                        params.run_type == pipeline::pipeline_run_type::camera &&
                                        !params.fill_taa_params && !params.fill_ssr_params && !params.fill_ssil_params &&
                                        !params.fill_gi_params &&
                                        gfx::get_device_caps().can_render_to(bgfx::TextureFormat::RG11B10F);
    return can_use_compact_format ? bgfx::TextureFormat::RG11B10F : get_default_hdr_format();
}

inline auto create_or_get_irradiance_texture(gfx::render_view& rview) -> const gfx::texture::ptr&
{
    // Match auto-exposure: RGBA32F + COMPUTE_WRITE uses glTexStorage2D on GL (immutable
    // storage). Initial data must go through update_texture_2d (glTexSubImage2D), not
    // the texture ctor _mem path. BGFX_TEXTURE_RT keeps the GL texture sampleable after
    // compute image writes (same pattern as Hi-Z and other compute targets).
    // Layout: one texel per SH coefficient, rgb = channels R,G,B (a unused), then the GPU's share of the
    // pre-exposure (pre_exposure_state::gpu_texel), 1 until the first run writes it.
    constexpr std::uint16_t width = pre_exposure_state::gpu_texel + 1;
    return rview.tex_acquire("IRRADIANCE_SH",
                             {.size = {width, 1},
                              .format = bgfx::TextureFormat::RGBA32F,
                              .flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_MIN_POINT |
                                       BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP},
                             [](const gfx::texture::ptr& tex)
                             {
                                 float initial_texels[width * 4] = {};
                                 std::fill_n(initial_texels + pre_exposure_state::gpu_texel * 4, 4, 1.0f);
                                 const bgfx::Memory* initial_pixels = bgfx::copy(initial_texels, sizeof(initial_texels));
                                 bgfx::updateTexture2D(tex->native_handle(), 0, 0, 0, 0, width, 1, initial_pixels);
                             });
}

inline auto reflection_screen_stack_enabled(const pipeline::run_params& params) -> bool
{
    return params.run_type == pipeline::pipeline_run_type::camera &&
           (params.pflags & deferred::pipeline_steps::reflection_probe) != 0u;
}
} // namespace deferred_detail

using namespace deferred_detail;
} // namespace rendering
} // namespace unravel
