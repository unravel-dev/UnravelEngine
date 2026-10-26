#include "pipeline.h"
#include "glm/ext/scalar_integer.hpp"
#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
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
#include <engine/rendering/default_textures.h>
#include <engine/engine.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
#include <engine/rendering/render_scale.h>
#include <engine/rendering/renderer.h>
#include <engine/rendering/skinning_palette_buffer.h>
#include <engine/settings/settings.h>
#include <hpp/optional.hpp>

#include <graphics/index_buffer.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <algorithm>
#include <cmath>
#include <graphics/render_view.h>
#include <graphics/texture.h>
#include <graphics/vertex_buffer.h>

namespace unravel
{
namespace
{
namespace ANONYMOUS
{
/// Border fade of the cloud shadow map in map space (fraction of the half extent).
constexpr float cloud_shadow_border_fade = 0.08f;
/// First sampler stage of the shadow maps' depth attachments in the light programs (s_shadowDepth0).
constexpr uint8_t light_shadow_compare_stage = 12;
/// The stages fs_pbr_lighting.sh declares the GPU pre-exposure at (PRE_EXPOSURE_STAGE): the direct light
/// programs leave s_tex5 / s_tex6 unbound, the indirect ones keep 13 free.
constexpr uint8_t light_pre_exposure_stage = 6;
constexpr uint8_t indirect_pre_exposure_stage = 13;

/// PCF filter radius in shadow map texels at PCF offset 1, at least one texel.
constexpr float pcf_radius_texels_per_offset = 2.0f;
constexpr float pcf_min_radius_texels = 1.0f;
/// Point lights pack four tetrahedron faces into one map and filter each at this fraction of the radius.
constexpr float pcf_point_face_radius_scale = 0.25f;
/// A tent of N texels stands in for a filter disk of radius (N - 1) / 2: radii below these pick the 3x3 and
/// the 5x5 tent, larger ones the 7x7.
constexpr float pcf_tent3_max_radius_texels = 1.5f;
constexpr float pcf_tent5_max_radius_texels = 2.5f;
/// Slots of deferred::color_lighting_pcf_tent_.
constexpr size_t pcf_tent3_slot = 0;
constexpr size_t pcf_tent7_slot = 1;

/// The color_lighting_pcf_tent_ slot of a PCF light, or no slot for the 5x5 tent.
auto get_pcf_tent_slot(const light& l) -> hpp::optional<size_t>
{
    const float offset = std::max(l.shadow_params.pcf.x_offset, l.shadow_params.pcf.y_offset);
    const float face_scale = l.type == light_type::point ? pcf_point_face_radius_scale : 1.0f;
    const float radius_texels = std::max(pcf_radius_texels_per_offset * offset * face_scale, pcf_min_radius_texels);
    if(radius_texels < pcf_tent3_max_radius_texels)
    {
        return pcf_tent3_slot;
    }
    if(radius_texels >= pcf_tent5_max_radius_texels)
    {
        return pcf_tent7_slot;
    }
    return hpp::nullopt;
}

/// Whether a light of @p intensity and @p linear_color adds anything to a pixel. One that does not still costs a
/// full G-buffer read per covered pixel, and its shadow maps a pass per caster.
auto is_adding_light(float intensity, const math::vec3& linear_color) -> bool
{
    return intensity > 0.0f && std::max({linear_color.x, linear_color.y, linear_color.z}) > 0.0f;
}

/// Whether the light pass and the shadow maps need @p l: it adds light, or the shadow coverage view shows its
/// shadow maps, which that view draws whatever the intensity.
auto is_light_needed(const light& l, const math::vec3& linear_color) -> bool
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

auto make_cloud_shadow_uniforms(const atmospheric_pass_perez::cloud_shadow_result& shadow) -> cloud_shadow_uniforms
{
    cloud_shadow_uniforms uniforms;
    uniforms.is_applied = shadow.valid && shadow.apply_to_lights && shadow.map;
    uniforms.placement = math::vec4(shadow.origin.x, shadow.origin.y, 1.0f / std::max(shadow.extent, 1.0f), shadow.opacity);
    uniforms.layer = math::vec4(uniforms.is_applied ? 1.0f : 0.0f, shadow.base_world_y, cloud_shadow_border_fade, 0.0f);
    return uniforms;
}
/// Material textures filter anisotropically, up to the device maximum (BGFX_RESET_MAXANISOTROPY): trilinear alone
/// picks the mip from the longer footprint axis and smears surfaces seen at grazing angles, such as distant floors.
constexpr std::uint32_t material_anisotropic_sampler_flags = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;

/// The texture's own sampler state (wrap, border...) with anisotropic filtering, unless it asks for point sampling.
auto get_material_sampler_flags(const gfx::texture::ptr& texture) -> std::uint32_t
{
    if(!texture)
    {
        return material_anisotropic_sampler_flags;
    }
    const auto own_flags = static_cast<std::uint32_t>(texture->flags & BGFX_SAMPLER_BITS_MASK);
    const bool is_point = (own_flags & (BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT)) != 0u;
    return is_point ? own_flags : own_flags | material_anisotropic_sampler_flags;
}
/// Material texture mip bias while TAA jitters the view: the jitter integrates the slightly sharper mip's
/// extra detail instead of aliasing it.
constexpr float material_taa_mip_bias = -0.3f;

/// The G-buffer's u_camera_clip_planes: near, far, and the material texture mip bias (TAA jitter active or not).
auto make_geometry_clip_planes(const camera& view_camera) -> math::vec4
{
    const bool is_jittered = view_camera.get_aa_data().y > 1.0f;
    return {view_camera.get_near_clip(), view_camera.get_far_clip(), is_jittered ? material_taa_mip_bias : 0.0f, 0.0f};
}
/// Period of the contact-shadow dither's temporal offset (frames); TAA integrates it.
constexpr int contact_shadow_dither_frames = 16;
/// Most particle lights one view draws (the brightest of every emitter's, emitter_render_desc::light_count).
constexpr size_t max_particle_lights_per_view = 64;
/// RBUFFER's clear, packed RGBA8 (bgfx converts it for float targets): black with alpha 1 - no
/// traced reflection yet, and the whole pixel left to the probe layer.
constexpr uint32_t reflection_traced_clear_rgba = 0x000000ff;
/// GI experiment flag (surface_cache_system::get_experiment_flags): the global SDF clipmap stays where it was when
/// the bit was set (no camera re-snaps), to isolate re-snap transients in A/Bs.
constexpr uint32_t gi_experiment_freeze_clipmap_origin = 1u << 27u;
/// The global distance field's level scale for the GI: each level doubles the previous one's extent.
constexpr float gi_clipmap_level_scale = 2.0f;
/// The scene detail's range; 1 / detail scales the smallest object the distance field keeps.
constexpr float gi_clipmap_min_detail = 0.01f;
constexpr float gi_clipmap_max_detail = 100.0f;

/// The global distance field the GI traces (its hits read the surface cache) in the GI's layout (gi_pass_constants.h),
/// with the view's rebuild budget, level blend and the smallest object its scene detail keeps, partially updated
/// unless @p experiments holds gi_pass::experiment_no_sdf_partial_updates.
auto make_gi_clipmap_settings(const gi_settings& gi, bool compose_on_gpu, uint64_t experiments)
    -> global_sdf_clipmap::settings
{
    const auto& field = gi.distance_field;
    global_sdf_clipmap::settings settings;
    settings.resolution = uint32_t(gi::GI_GLOBAL_SDF_RESOLUTION);
    settings.base_extent = gi::GI_GLOBAL_SDF_EXTENT;
    settings.level_scale = gi_clipmap_level_scale;
    settings.compose_on_gpu = compose_on_gpu;
    settings.max_levels_per_update = std::clamp(field.levels_per_update, 1u, global_sdf_clipmap::level_count);
    settings.blend_voxels = std::max(field.level_blend_band, 0.0f);
    settings.object_radius_scale =
        1.0f / std::clamp(gi.scene.detail, gi_clipmap_min_detail, gi_clipmap_max_detail);
    settings.partial_updates = (experiments & gi_pass::experiment_no_sdf_partial_updates) == 0u;
    return settings;
}
} // namespace ANONYMOUS
} // namespace unravel

namespace rendering
{

namespace
{


auto get_default_format() -> bgfx::TextureFormat::Enum
{
    return bgfx::TextureFormat::RGBA8;
}

auto get_default_hdr_format() -> bgfx::TextureFormat::Enum
{
    return bgfx::TextureFormat::RGBA16F;
}

auto get_default_depth_format() -> bgfx::TextureFormat::Enum
{
    return bgfx::TextureFormat::D32F;
}

// Returns whether the intermediate G/L/R buffers should be RGBA16F. Tonemapping
// implies HDR, and probe captures force it explicitly (they strip the post stack
// but still light in HDR for the cubemap).
auto wants_hdr_buffers(const pipeline::run_params& params) -> bool
{
    return static_cast<bool>(params.fill_hdr_params) || params.force_hdr_buffers;
}

// Cubemap face captures strip post-processing and write the linear HDR lighting
// result directly to the cubemap face. Clearing fill_hdr_params also used to drop
// the G/L buffers to RGBA8 (LDR-clamped IBL); force_hdr_buffers keeps them float.
void strip_post_effects_for_reflection_probe_capture(pipeline::run_params& params)
{
    params.fill_assao_params = {};
    params.fill_auto_exposure_params = {};
    params.fill_bloom_params = {};
    params.fill_taa_params = {};
    params.apply_taa_params = {};
    params.fill_ssr_params = {};
    params.fill_ssil_params = {};
    params.fill_hdr_params = {};
    params.fill_fog_params = {};
    params.force_hdr_buffers = true;
}

void clear_reflection_probe_face(const gfx::frame_buffer::ptr& fbo)
{
    if(!fbo)
    {
        return;
    }

    gfx::render_pass pass("Reflection Probe/Clear Face");
    pass.bind(fbo.get());
    pass.set_view_proj(nullptr, nullptr);
    pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);
}

// run_pipeline_impl takes const camera& for reads; jitter only touches projection jitter state.
void apply_pipeline_taa_jitter_to_camera(const camera& view_camera,
                                         const usize32_t& viewport_size,
                                         const pipeline::run_params& params)
{
    camera& cam = const_cast<camera&>(view_camera);
    if(params.apply_taa_params)
    {
        params.apply_taa_params(cam, viewport_size);
    }
    else
    {
        cam.set_aa_data(viewport_size, 0u, 1u);
    }
    // With the frame's jitter final, record this frame's matrices; last frame's recording
    // becomes the camera's get_prev_* set (frame-stamped - a second run of the same camera
    // in one frame is a no-op). This is the ONE place previous matrices are maintained.
    cam.record_current_matrices();
}

auto create_or_resize_d_buffer(gfx::render_view& rview,
                               const usize32_t& render_size,
                               const pipeline::run_params& params) -> const gfx::texture::ptr&
{
    return rview.tex_acquire("DEPTH",
                             {.size = render_size, .format = bgfx::TextureFormat::D32F, .flags = BGFX_TEXTURE_RT});
}

auto create_or_resize_hiz_buffer(gfx::render_view& rview, const usize32_t& render_size) -> const gfx::texture::ptr&
{
    // R32F for precision, a full mip chain, compute-written and point-sampled with clamped UVs.
    return rview.tex_acquire("HIZBUFFER",
                             {.size = render_size,
                              .format = bgfx::TextureFormat::R32F,
                              .flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_MIN_POINT |
                                       BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP |
                                       BGFX_SAMPLER_V_CLAMP,
                              .has_mips = true});
}

auto create_or_resize_g_buffer(gfx::render_view& rview,
                               const usize32_t& render_size,
                               const pipeline::run_params& params) -> const gfx::frame_buffer::ptr&
{
    const auto& depth = create_or_resize_d_buffer(rview, render_size, params);
    const auto format = wants_hdr_buffers(params) ? get_default_hdr_format() : get_default_format();
    // sRGB: the base colour is stored encoded and every reader gets it decoded by the texture unit (EncodeGBuffer).
    const auto& tex0 = rview.tex_acquire(
        "GBUFFER_0",
        {.size = render_size, .format = get_default_format(), .flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_SRGB});
    const auto& tex1 = rview.tex_acquire("GBUFFER_1", {.size = render_size, .format = format, .flags = BGFX_TEXTURE_RT});
    const auto& tex2 = rview.tex_acquire("GBUFFER_2", {.size = render_size, .format = format, .flags = BGFX_TEXTURE_RT});
    const auto& tex3 = rview.tex_acquire(
        "GBUFFER_3",
        {.size = render_size, .format = get_default_format(), .flags = BGFX_TEXTURE_RT});

    auto& fbo = rview.fbo_acquire("GBUFFER");
    if(gfx::needs_recreate(fbo, {tex0, tex1, tex2, tex3, depth}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex0, tex1, tex2, tex3, depth});
    }

    return fbo;
}

auto create_or_resize_l_buffer(gfx::render_view& rview,
                               const usize32_t& render_size,
                               const pipeline::run_params& params) -> const gfx::frame_buffer::ptr&
{
    const auto& depth = create_or_resize_d_buffer(rview, render_size, params);
    const auto format = wants_hdr_buffers(params) ? get_default_hdr_format() : get_default_format();
    const auto& tex = rview.tex_acquire("LBUFFER", {.size = render_size, .format = format, .flags = BGFX_TEXTURE_RT});

    auto& fbo_depth = rview.fbo_acquire("LBUFFER_DEPTH");
    if(gfx::needs_recreate(fbo_depth, {tex, depth}))
    {
        fbo_depth.reset();
        fbo_depth = std::make_shared<gfx::frame_buffer>();
        fbo_depth->populate({tex, depth});
    }

    auto& fbo = rview.fbo_acquire("LBUFFER");
    if(gfx::needs_recreate(fbo, {tex}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }

    return fbo;
}

/// A reflection target at the render size, rebuilt when its size, format or flags change.
auto create_or_resize_reflection_buffer(gfx::render_view& rview,
                                        const std::string& name,
                                        const usize32_t& render_size,
                                        const pipeline::run_params& params,
                                        uint64_t texture_flags) -> const gfx::frame_buffer::ptr&
{
    const auto format = wants_hdr_buffers(params) ? get_default_hdr_format() : get_default_format();
    const auto& tex = rview.tex_acquire(name, {.size = render_size, .format = format, .flags = texture_flags});

    auto& fbo = rview.fbo_acquire(name);
    if(gfx::needs_recreate(fbo, {tex}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }

    return fbo;
}

/// The reflection buffers. RBUFFER holds the traced layers (GI reflections, then SSR),
/// premultiplied in rgb, with the share they leave uncovered in alpha; PBUFFER holds the
/// untraced layer (reflection probes, sky, the GI rough tier). The indirect pass occludes the
/// two differently (ComposeIndirectSpecular in lighting.sh). The GI reflections write both
/// from compute.
void create_or_resize_reflection_buffers(gfx::render_view& rview,
                                         const usize32_t& render_size,
                                         const pipeline::run_params& params)
{
    create_or_resize_reflection_buffer(rview, "RBUFFER", render_size, params, BGFX_TEXTURE_RT | BGFX_TEXTURE_COMPUTE_WRITE);
    create_or_resize_reflection_buffer(rview, "PBUFFER", render_size, params, BGFX_TEXTURE_RT | BGFX_TEXTURE_COMPUTE_WRITE);
}
/// The size a run renders its scene at: a camera run's render resolution (its viewport's size unless set) at its
/// render scale (get_view_render_size); every other run renders at its viewport's size.
auto get_run_render_size(const usize32_t& viewport_size, const pipeline::run_params& params) -> usize32_t
{
    const bool is_camera_run = params.run_type == pipeline::pipeline_run_type::camera;
    return is_camera_run ? get_view_render_size(viewport_size, params.render_resolution, params.render_scale) : viewport_size;
}

/// The output's color at the viewport's size; acquire_scene_output attaches the scene's depth to it.
auto create_or_resize_o_buffer(gfx::render_view& rview, const usize32_t& viewport_size) -> const gfx::frame_buffer::ptr&
{
    const auto& tex = rview.tex_acquire("OBUFFER",
                                        {.size = viewport_size,
                                         .format = get_default_format(),
                                         .flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_RT});

    auto& fbo = rview.fbo_acquire("OBUFFER");
    if(gfx::needs_recreate(fbo, {tex}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }

    return fbo;
}

/// Where a camera run finishes its scene: the passes after the post-processing chain draw into it.
struct scene_output
{
    /// The finished scene's color.
    gfx::frame_buffer::ptr color;
    /// The same color with the scene's depth attached.
    gfx::frame_buffer::ptr color_depth;
    /// A scaled run's output with a depth of its own at the output's size (OBUFFER_DEPTH), which the upscale
    /// fills with the scene's depth; null unless the run asks for it (run_params::needs_output_depth).
    gfx::frame_buffer::ptr output_depth;
};

/// The depth a scaled run gives its output, at the output's size, for the overlays drawn over it.
auto acquire_output_depth(gfx::render_view& rview, const gfx::frame_buffer::ptr& output, const usize32_t& viewport_size)
    -> const gfx::frame_buffer::ptr&
{
    const auto& depth = rview.tex_acquire("OUTPUT_DEPTH",
                                          {.size = viewport_size, .format = bgfx::TextureFormat::D32F, .flags = BGFX_TEXTURE_RT});
    auto& output_depth = rview.fbo_acquire("OBUFFER_DEPTH");
    if(gfx::needs_recreate(output_depth, {output->get_texture(), depth}))
    {
        output_depth.reset();
        output_depth = std::make_shared<gfx::frame_buffer>();
        output_depth->populate({output->get_texture(), depth});
    }
    return output_depth;
}

/**
 * @brief The scene output of a camera run rendering at `render_size` into `output`.
 *
 * At the output's size the run finishes in the output itself. A scaled run finishes in SCALED_OBUFFER, at the
 * render size like the rest of its targets, which the pipeline resamples into the output last; its targets are
 * released while the run is not scaled.
 * @param needs_output_depth A scaled run also gives the output a depth at its size (run_params::needs_output_depth).
 */
auto acquire_scene_output(gfx::render_view& rview,
                          const gfx::frame_buffer::ptr& output,
                          const usize32_t& viewport_size,
                          const usize32_t& render_size,
                          bool needs_output_depth) -> scene_output
{
    const auto& depth = rview.tex_get("DEPTH");
    if(render_size == viewport_size)
    {
        rview.fbo_remove("SCALED_OBUFFER_DEPTH");
        rview.fbo_remove("SCALED_OBUFFER");
        rview.tex_remove("SCALED_OBUFFER");
        rview.tex_remove("OUTPUT_DEPTH");
        auto& output_depth = rview.fbo_acquire("OBUFFER_DEPTH");
        if(gfx::needs_recreate(output_depth, {output->get_texture(), depth}))
        {
            output_depth.reset();
            output_depth = std::make_shared<gfx::frame_buffer>();
            output_depth->populate({output->get_texture(), depth});
        }
        return {output, output_depth};
    }
    // Clamped: the upscale filters across the edge texels.
    const auto& tex = rview.tex_acquire(
        "SCALED_OBUFFER",
        {.size = render_size, .format = get_default_format(), .flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP});
    auto& color = rview.fbo_acquire("SCALED_OBUFFER");
    if(gfx::needs_recreate(color, {tex}))
    {
        color.reset();
        color = std::make_shared<gfx::frame_buffer>();
        color->populate({tex});
    }
    auto& color_depth = rview.fbo_acquire("SCALED_OBUFFER_DEPTH");
    if(gfx::needs_recreate(color_depth, {tex, depth}))
    {
        color_depth.reset();
        color_depth = std::make_shared<gfx::frame_buffer>();
        color_depth->populate({tex, depth});
    }
    if(needs_output_depth)
    {
        return {color, color_depth, acquire_output_depth(rview, output, viewport_size)};
    }
    // The scene's DEPTH is at the render size: the output has no depth.
    rview.fbo_remove("OBUFFER_DEPTH");
    rview.tex_remove("OUTPUT_DEPTH");
    return {color, color_depth, {}};
}

// Velocity (motion vector) target, at the render size of the G-buffer it pairs with. RGBA16F: RG = total uv-delta
// (uv_curr - uv_prev), BA = the OBJECT-ONLY component (total minus the camera-induced
// part), both computed inside the velocity pass with one consistent matrix set.
//
// TWO framebuffers over the SAME color texture, and the split is load-bearing:
//  - "VELOCITY_FBO_CAMERA" is color-only, for the fullscreen camera sub-pass, which
//    SAMPLES the G-buffer depth. Rendering that sub-pass with DEPTH attached made the
//    same texture SRV and DSV of one draw - D3D11 silently unbinds the SRV and every
//    depth sample returns 0 (the NEAR PLANE), inflating the written camera velocity by
//    the near-plane parallax while keeping its direction plausible. That corruption
//    masqueraded as a cross-pass "previous view-projection mismatch" for three debugging
//    rounds (see tasks/velocity_buffer_plan.md).
//  - "VELOCITY_FBO" carries the shared DEPTH attachment for the movers sub-pass, which
//    depth-tests EQUAL and never samples depth - no conflict there.
auto create_or_resize_v_buffer(gfx::render_view& rview, const usize32_t& render_size)
    -> const gfx::frame_buffer::ptr&
{
    const auto& depth = rview.tex_find("DEPTH");

    const auto& tex = rview.tex_acquire("VELOCITY",
                                        {.size = render_size,
                                         .format = bgfx::TextureFormat::RGBA16F,
                                         .flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP});

    auto& camera_fbo = rview.fbo_acquire("VELOCITY_FBO_CAMERA");
    if(gfx::needs_recreate(camera_fbo, {tex}))
    {
        camera_fbo.reset();
        camera_fbo = std::make_shared<gfx::frame_buffer>();
        camera_fbo->populate({tex});
    }

    auto& fbo = rview.fbo_acquire("VELOCITY_FBO");
    if(gfx::needs_recreate(fbo, {tex, depth}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex, depth});
    }

    return fbo;
}

auto create_or_get_irradiance_texture(gfx::render_view& rview) -> const gfx::texture::ptr&
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

/// Copies into @p light_casters the casters whose world bounds reach into a local light's
/// bounding sphere (world space). A caster wholly outside it is beyond the light's reach:
/// every ray from the light to a lit receiver stays inside that volume, so such a caster has
/// nothing to shadow. Both lists are pipeline-owned scratch whose clear() keeps the storage -
/// once warmed up this allocates nothing, and it reads nothing but the contiguous list.
void cull_shadow_casters(const shadow::shadow_map_models_t& static_casters,
                         const shadow::shadow_map_models_t& dynamic_casters,
                         const shadow::shadow_map_models_t& particle_casters,
                         const math::bsphere& light_sphere,
                         shadow::shadow_map_models_t& light_casters)
{
    APP_SCOPE_PERF("Rendering/Shadow Caster Cull Per Light");
    light_casters.clear();
    const float radius_squared = light_sphere.radius * light_sphere.radius;
    const auto keep_casters_in_range = [&](const shadow::shadow_map_models_t& casters)
    {
        for(const auto& caster : casters)
        {
            const math::vec3 to_closest =
                caster.world_bounds.closest_point(light_sphere.position) - light_sphere.position;
            if(math::dot(to_closest, to_closest) <= radius_squared)
            {
                light_casters.emplace_back(caster);
            }
        }
    };
    keep_casters_in_range(static_casters);
    keep_casters_in_range(dynamic_casters);
    keep_casters_in_range(particle_casters);
}

auto reflection_screen_stack_enabled(const pipeline::run_params& params) -> bool
{
    return params.run_type == pipeline::pipeline_run_type::camera &&
           (params.pflags & deferred::pipeline_steps::reflection_probe) != 0u;
}
} // namespace

auto deferred::get_light_program(const light& l) const -> const color_lighting&
{
    const auto& program = color_lighting_[uint8_t(l.type)][uint8_t(l.shadow_params.depth)][uint8_t(l.shadow_params.type)];
    if(l.shadow_params.type != sm_impl::pcf)
    {
        return program;
    }
    const auto tent_slot = ANONYMOUS::get_pcf_tent_slot(l);
    if(!tent_slot)
    {
        return program;
    }
    const auto& tent_program = color_lighting_pcf_tent_[uint8_t(l.type)][uint8_t(l.shadow_params.depth)][*tent_slot];
    return tent_program.program ? tent_program : program;
}

auto deferred::get_light_program_no_shadows(const light& l) const -> const color_lighting&
{
    return color_lighting_no_shadow_[uint8_t(l.type)];
}

void deferred::submit_pbr_material(geom_program& program, const pbr_material& mat)
{
    const auto& color_map = mat.get_color_map();
    const auto& normal_map = mat.get_normal_map();
    const auto& roughness_map = mat.get_roughness_map();
    const auto& metalness_map = mat.get_metalness_map();
    const auto& ao_map = mat.get_ao_map();
    const auto& emissive_map = mat.get_emissive_map();

    const auto& albedo = color_map ? color_map : mat.default_color_map();
    const auto& normal = normal_map ? normal_map : mat.default_normal_map();
    const auto& roughness = roughness_map ? roughness_map : mat.default_color_map();
    const auto& metalness = metalness_map ? metalness_map : mat.default_color_map();
    const auto& ao = ao_map ? ao_map : mat.default_color_map();
    const auto& emissive = emissive_map ? emissive_map : mat.default_color_map();

    // Resolve and pin every texture up front. asset_handle::get() returns a
    // shared_ptr<texture>; holding our own copies for the duration of the
    // submit keeps the texture objects alive even if the asset watcher
    // thread invalidates/reloads these handles mid-frame (which happens
    // intermittently right after a scene is opened). Without this, a texture
    // resolved here could be released between the individual set_texture
    // calls, leaving a dangling pointer for native_handle().
    const auto albedo_tex = albedo.get();
    const auto normal_tex = normal.get();
    const auto roughness_tex = roughness.get();
    const auto metalness_tex = metalness.get();
    const auto ao_tex = ao.get();
    const auto emissive_tex = emissive.get();

    // The values an instanced draw reads per instance (pbr_material::write_instance_constants), here as uniforms.
    std::array<math::vec4, material::MAX_INSTANCE_CONSTANTS> constants{};
    mat.write_instance_constants(constants.data());
    const auto& tiling = mat.get_tiling();
    const auto& dither_threshold = mat.get_dither_threshold();
    const auto surface_data2 = mat.get_surface_data2();

    gfx::set_texture(program.s_tex_color, 0, albedo_tex, ANONYMOUS::get_material_sampler_flags(albedo_tex));
    gfx::set_texture(program.s_tex_normal, 1, normal_tex, ANONYMOUS::get_material_sampler_flags(normal_tex));
    gfx::set_texture(program.s_tex_roughness, 2, roughness_tex, ANONYMOUS::get_material_sampler_flags(roughness_tex));
    gfx::set_texture(program.s_tex_metalness, 3, metalness_tex, ANONYMOUS::get_material_sampler_flags(metalness_tex));
    gfx::set_texture(program.s_tex_ao, 4, ao_tex, ANONYMOUS::get_material_sampler_flags(ao_tex));
    gfx::set_texture(program.s_tex_emissive, 5, emissive_tex, ANONYMOUS::get_material_sampler_flags(emissive_tex));

    gfx::set_uniform(program.u_base_color, constants[0]);
    gfx::set_uniform(program.u_subsurface_color, constants[1]);
    gfx::set_uniform(program.u_emissive_color, constants[2]);
    gfx::set_uniform(program.u_surface_data, constants[3]);
    gfx::set_uniform(program.u_tiling, tiling);
    gfx::set_uniform(program.u_dither_threshold, dither_threshold);
    gfx::set_uniform(program.u_surface_data2, surface_data2);

    auto state = mat.get_render_states(true, true, true);

    bgfx::setState(state);
}

void deferred::build_reflections(scene& scn, const camera& camera, delta_t dt, const feature_limits& limits)
{
    APP_SCOPE_PERF("Rendering/Reflection Generation Pass");

    scn.registry->view<transform_component, reflection_probe_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& reflection_probe_comp, auto&& active)
        {
            if(reflection_probe_comp.already_generated())
            {
                return;
            }

            // reflection_probe_comp.set_generation_frame(gfx::get_render_frame());

            const auto& world_transform = transform_comp.get_transform_global();

            const auto& bounds = reflection_probe_comp.get_bounds();
            if(!camera.test_obb(bounds, world_transform))
            {
                return;
            }

            const auto& probe = reflection_probe_comp.get_probe();

            auto handle = scn.create_handle(e);
            {
                gfx::render_pass::push_scope("Build Reflections");

                if(reflection_probe_comp.is_bake_cycle_unstarted())
                {
                    for(std::uint32_t face = 0; face < 6; ++face)
                    {
                        clear_reflection_probe_face(reflection_probe_comp.get_cubemap_fbo(face));
                    }
                }

                bool any_face_dirty = false;
                // iterate trough each cube face
                for(std::uint32_t face = 0; face < 6; ++face)
                {
                    if(reflection_probe_comp.already_generated(face))
                    {
                        continue;
                    }

                    reflection_probe_comp.set_generation_frame(face, gfx::get_render_frame());

                    auto camera = camera::get_face_camera(face, world_transform);
                    camera.set_far_clip(probe.get_face_extents(face, world_transform));
                    auto& rview = reflection_probe_comp.get_render_view(face);
                    const auto& cubemap_fbo = reflection_probe_comp.get_cubemap_fbo(face);

                    camera.set_viewport_size(usize32_t(cubemap_fbo->get_size()));

                    bool not_environment = probe.method != reflect_method::environment;

                    pipeline_flags pflags = 0;
                    visibility_flags vflags = visibility_query::is_static;

                    if(not_environment)
                    {
                        pflags |= pipeline_steps::geometry_pass;
                    }

                    if(reflection_probe_comp.get_capture_sky())
                    {
                        pflags |= pipeline_steps::atmospheric;
                    }

                    if(not_environment && reflection_probe_comp.get_capture_shadows())
                    {
                        pflags |= pipeline_steps::shadow_pass;
                        vflags |= visibility_query::is_shadow_caster;
                    }

                    auto params = create_run_params(handle, &scn, &camera);
                    params.run_type = pipeline_run_type::reflection_probe_capture;
                    params.vflags = vflags;
                    params.pflags = pflags;
                    // The camera's limits: its shadow maps are shared with the captures, so both render
                    // every light the same way.
                    apply_feature_limits(params, limits);
                    strip_post_effects_for_reflection_probe_capture(params);

                    //if(!reflection_probe_comp.get_capture_sky())
                    {
                        clear_reflection_probe_face(cubemap_fbo);
                    }

                    run_pipeline_impl(cubemap_fbo, scn, camera, rview, dt, params);
                    any_face_dirty = true;
                }

                if(any_face_dirty && reflection_probe_comp.is_bake_complete())
                {
                    auto env_cube = reflection_probe_comp.get_cubemap();
                    auto env_cube_prefiltered = reflection_probe_comp.get_cubemap_prefiltered();
                    prefilter_pass::run_params prefilter_params;

                    prefilter_params.apply_prefilter = reflection_probe_comp.get_apply_prefilter();

                    for(std::uint32_t face = 0; face < 6; ++face)
                    {
                        const auto& cubemap_fbo = reflection_probe_comp.get_cubemap_fbo(face);
                        prefilter_params.input_faces[face] = cubemap_fbo->get_texture();
                    }

                    prefilter_params.output_cube = env_cube;
                    prefilter_params.output_cube_prefiltered = env_cube_prefiltered;

                    prefilter_pass_.run(reflection_probe_comp.get_render_view(0), prefilter_params);
                    reflection_probe_comp.mark_capture_ready();
                }

                gfx::render_pass::pop_scope();
            }
        });
}

void deferred::refresh_shadow_casters(scene& scn, const camera& camera, delta_t dt, layer_mask render_mask)
{
    const auto* revision = scn.registry->ctx().find<shadow_caster_revision>();
    const uint64_t current_revision = revision != nullptr ? revision->value : 0ULL;
    const bool lists_retired =
        current_revision != shadow_casters_revision_ || render_mask.mask != shadow_casters_mask_.mask;

    if(lists_retired)
    {
        APP_SCOPE_PERF("Rendering/Shadow Casters Rebuild");
        // The ONE walk over every model in the scene. Everything the split depends on -
        // membership, static-ness, a static caster's bounds - is behind the revision, so in a
        // steady scene this does not run again.
        shadow_static_casters_.clear();
        shadow_dynamic_casters_.clear();
        const auto query = visibility_flags{visibility_query::is_shadow_caster};
        // No LOD reference camera: the shadow submit resolves LOD itself, per caster it draws.
        gather_visible_models(scn, nullptr, query, render_mask, dt,
            [&](entt::handle entity, const lod_data& /*lod_data*/)
            {
                const auto& model_comp = entity.get<model_component>();
                auto& casters = model_comp.is_static() ? shadow_static_casters_ : shadow_dynamic_casters_;
                casters.emplace_back(shadow::shadow_visibility_data{entity, model_comp.get_world_bounds()});
            }, nullptr);
        shadow_casters_revision_ = current_revision;
        shadow_casters_mask_ = render_mask;
        shadow_casters_frame_ = gfx::get_render_frame();
        return;
    }

    const uint64_t frame = gfx::get_render_frame();
    if(shadow_casters_frame_ == frame)
    {
        return;
    }
    shadow_casters_frame_ = frame;

    APP_SCOPE_PERF("Rendering/Shadow Casters Refresh Movers");
    // Movers only: their bounds changed because they moved, which is not a membership change.
    // Static casters keep the bounds captured at the rebuild - a change there bumped the
    // revision and took the branch above.
    for(auto& caster : shadow_dynamic_casters_)
    {
        caster.world_bounds = caster.entity.get<model_component>().get_world_bounds();
    }
}

namespace
{
/// How much of @p camera's view a light reaching @p reach covers: its angular radius, up to 1 from inside it. The
/// shadow atlas keeps the resolution of the lights that cover more the longest.
auto get_shadow_importance(const camera& camera, const math::bsphere& reach) -> float
{
    const float distance = math::distance(camera.get_position(), reach.position);
    return reach.radius / math::max(distance, math::max(reach.radius, 1e-4f));
}
} // namespace

void deferred::reserve_shadow_atlas_tiles(scene& scn, const camera& camera, const feature_limits& limits)
{
    shadow_atlas_requests_.clear();
    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& light_comp, auto&& active)
        {
            const auto light = limit_light(light_comp.get_light(), limits);
            if(!shadow::shadowmap_generator::uses_shadow_atlas(light) || !light_comp.is_enabled() ||
               !ANONYMOUS::is_light_needed(light, light_comp.get_linear_color()))
            {
                return;
            }
            auto world_transform = transform_comp.get_transform_global();
            world_transform.reset_scale();
            const auto light_sphere = light_comp.get_world_bounds_sphere(world_transform);
            if(!camera.get_frustum().test_sphere(light_sphere))
            {
                return;
            }
            const float importance = get_shadow_importance(camera, light_sphere);
            shadow_atlas_requests_.push_back(light_comp.get_shadowmap_generator().make_atlas_request(light, importance));
        });
    // Every view reserves, with or without lights: a new frame first takes back the tiles no view asked for.
    shadow::shadow_atlas::get().reserve(shadow_atlas_requests_);
}

void deferred::build_shadows(scene& scn,
                             const camera& camera,
                             delta_t dt,
                             const feature_limits& limits,
                             layer_mask render_mask)
{
    APP_SCOPE_PERF("Rendering/Shadow Generation Pass");

    // Before any light renders: reserving can grow the atlas or step lights down, which moves tiles.
    reserve_shadow_atlas_tiles(scn, camera, limits);

    bool queried = false;

    scn.registry->view<transform_component, light_component>().each(
        [&](auto e, auto&& transform_comp, auto&& light_comp)
        {
            const auto light = limit_light(light_comp.get_light(), limits);

            bool is_directional = light.type == light_type::directional;
            bool has_render_mask = render_mask.mask != layer_reserved::everything_layer;
            bool camera_dependant = is_directional || has_render_mask;
            bool is_active = scn.registry->all_of<active_component>(e);

            // A point / spot light's shadow maps are the same for every view of the frame, so
            // the first view that resolves them wins and the rest skip the light (see
            // shadowmap_generator::already_resolved). A directional light re-fits its cascades
            // to the view being rendered, and a render mask makes even a local light's caster
            // set view specific, so neither is ever skipped.
            auto& generator = light_comp.get_shadowmap_generator();
            if(!camera_dependant && generator.already_resolved())
            {
                return;
            }

            APP_SCOPE_PERF("Rendering/Shadow Generation Pass Per Light");

            auto world_transform = transform_comp.get_transform_global();
            world_transform.reset_scale();

            generator.update(camera, light, world_transform, is_active);

            // Camera independent: no view of this frame wants shadow maps from this light. A disabled
            // light, or one that adds no light, keeps its maps allocated so switching it back on is cheap.
            if(!is_active || !light_comp.is_enabled() || !light.casts_shadows ||
               !ANONYMOUS::is_light_needed(light, light_comp.get_linear_color()))
            {
                generator.mark_resolved();
                return;
            }

            // Camera DEPENDENT, so not a resolution: a later view of this frame (the camera
            // after a probe face capture) may still see this light and have to generate.
            const auto light_sphere = light_comp.get_world_bounds_sphere(world_transform);
            if(!camera.get_frustum().test_sphere(light_sphere))
            {
                return;
            }

            if(!queried)
            {
                refresh_shadow_casters(scn, camera, dt, render_mask);
                queried = true;
            }

            // A directional light reaches every caster (its generator culls them against the
            // view); a local light only gets the casters that reach into its range.
            const shadow::shadow_map_models_t* light_casters = nullptr;
            if(is_directional)
            {
                // One list for the generator: the movers appended to the static ones. Only
                // the tail is rewritten per light, so the static part is not copied again.
                shadow_light_casters_.assign(shadow_static_casters_.begin(), shadow_static_casters_.end());
                shadow_light_casters_.insert(shadow_light_casters_.end(),
                                             shadow_dynamic_casters_.begin(),
                                             shadow_dynamic_casters_.end());
                shadow_light_casters_.insert(shadow_light_casters_.end(),
                                             mesh_particle_casters_.begin(),
                                             mesh_particle_casters_.end());
                light_casters = &shadow_light_casters_;
            }
            else
            {
                cull_shadow_casters(shadow_static_casters_,
                                    shadow_dynamic_casters_,
                                    mesh_particle_casters_,
                                    light_sphere,
                                    shadow_light_casters_);
                light_casters = &shadow_light_casters_;
            }

            // Nothing in reach: skip, unless the maps still show casters that have since left.
            if(light_casters->empty() && !generator.needs_clear())
            {
                generator.mark_resolved();
                return;
            }

            APP_SCOPE_PERF("Rendering/Shadow Generation Pass Per Light After Cull");

            generator.generate_shadowmaps(*light_casters, camera, &stats_);
        });
}

void deferred::gather_mesh_particles(scene& scn, const camera& camera, bool is_camera_run, layer_mask render_mask)
{
    mesh_particle_emitters_.clear();
    mesh_particle_casters_.clear();
    if(!is_camera_run)
    {
        return;
    }
    APP_SCOPE_PERF("Rendering/Gather Mesh Particles");
    const math::vec3 eye = camera.get_position();
    const ps_soa::particle_quality& quality = ps_soa::get_quality();
    scn.registry->view<transform_component, particle_emitter_component, active_component>().each(
        [&](auto entity, auto&& transform_comp, auto&& emitter, auto&& active)
        {
            if(!emitter.is_enabled() || !emitter.is_mesh_render() || !is_on_rendered_layer(*scn.registry, entity, render_mask))
            {
                return;
            }
            const math::bbox& bounds = emitter.get_mesh_bounds();
            const math::bbox emitter_bounds = emitter.get_world_bounds();
            // The same gates as the billboards: beyond the draw distance or smaller on screen than the project
            // allows, the emitter is not drawn (and renderer-based culling freezes it).
            const float max_draw_distance = emitter.get_max_draw_distance();
            if(max_draw_distance > 0.0f &&
               math::distance(eye, math::clamp(eye, emitter_bounds.min, emitter_bounds.max)) > max_draw_distance)
            {
                return;
            }
            if(quality.min_screen_size > 0.0f && get_particle_screen_size(camera, emitter_bounds, eye) < quality.min_screen_size)
            {
                return;
            }
            const bool has_particles = bounds.is_populated() && !emitter.get_mesh_transforms().empty();
            const bool is_visible = has_particles && camera.test_aabb(bounds);
            if(is_visible || camera.test_aabb(emitter_bounds))
            {
                emitter.set_last_render_frame(uint64_t(gfx::get_render_frame()));
            }
            if(is_visible)
            {
                mesh_particle_emitters_.push_back(&emitter);
            }
            if(has_particles && emitter.get_mesh_cast_shadows())
            {
                mesh_particle_casters_.push_back(
                    shadow::shadow_visibility_data{entt::handle(*scn.registry, entity), bounds, &emitter});
            }
        });
}

auto deferred::run_pipeline(scene& scn,
                            const camera& camera,
                            gfx::render_view& rview,
                            delta_t dt,
                            const run_params& params,
                            layer_mask render_mask) -> gfx::frame_buffer::ptr
{
    const auto& obuffer = create_or_resize_o_buffer(rview, camera.get_viewport_size());

    run_pipeline_impl(obuffer, scn, camera, rview, dt, params, render_mask);

    return obuffer;
}

void deferred::run_pipeline(const gfx::frame_buffer::ptr& output,
                            scene& scn,
                            const camera& camera,
                            gfx::render_view& rview,
                            delta_t dt,
                            const run_params& params,
                            layer_mask render_mask)
{
    // A scaled view upscales straight into the caller's target: going through OBUFFER would cost one more pass at
    // the output's size, on the devices the scale is for.
    const auto& viewport_size = camera.get_viewport_size();
    if(get_run_render_size(viewport_size, params) != viewport_size)
    {
        run_pipeline_impl(output, scn, camera, rview, dt, params, render_mask);
        return;
    }
    auto obuffer = run_pipeline(scn, camera, rview, dt, params, render_mask);

    blit_to_output(rview, obuffer, output);
}

void deferred::set_debug_pass(int pass)
{
    debug_pass_ = pass;
    is_particle_overdraw_view_ = pass == debug_pass_particle_overdraw;
}

void deferred::set_debug_view_scale(float scale)
{
    debug_view_scale_ = scale > 0.0f ? scale : 1.0f;
}

auto deferred::get_debug_tonemapping(const run_params& rparams) -> tonemapping_method
{
    if(!rparams.fill_hdr_params)
    {
        return tonemapping_method::none;
    }
    tonemapping_pass::run_params tonemapping;
    rparams.fill_hdr_params(tonemapping);
    return tonemapping.config.method;
}

auto deferred::get_pre_exposure(gfx::render_view& rview) const -> pre_exposure_state
{
    const auto* state = rview.data().try_get<pre_exposure_state>(pre_exposure_state::view_key);
    return state ? *state : pre_exposure_state{};
}

void deferred::run_pipeline_impl(const gfx::frame_buffer::ptr& output,
                                 scene& scn,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 delta_t dt,
                                 const run_params& params,
                                 layer_mask render_mask)
{
    APP_SCOPE_PERF("Rendering/Run Pipeline");

    const pipeline_flags stages = params.pflags;
    const bool is_camera_run = params.run_type == pipeline_run_type::camera;
    const bool is_probe_capture = params.run_type == pipeline_run_type::reflection_probe_capture;

    if(is_camera_run)
    {
        stats_ = {};
    }

    gfx::frame_buffer::ptr target = nullptr;

    const bool build_shadowmaps = (stages & pipeline_steps::shadow_pass) != 0u;
    const bool build_reflection_probes = (stages & pipeline_steps::reflection_probe) != 0u;

    if(build_reflection_probes)
    {
        build_reflections(scn, camera, dt, params.limits);
    }

    is_camera_run_ = is_camera_run;
    render_mask_ = render_mask;
    gather_mesh_particles(scn, camera, is_camera_run, render_mask);

    if(build_shadowmaps)
    {
        build_shadows(scn, camera, dt, params.limits, render_mask);
    }

    // Before any pass writes or reads scene lighting (the nested probe captures above rendered
    // with their own unit pre-exposure). Stored in the render view; the passes read it back.
    const pre_exposure_state pre_exposure = update_pre_exposure(rview, params, is_camera_run);

    // The GI scene: instance residency and this view's global distance field (gating rationale at the definition).
    run_gi_scene_passes(scn, camera, rview, params);

    // The scene renders at the render size; the camera's viewport keeps the output's size, which the screen
    // helpers, the UI and the picking work in.
    const auto& viewport_size = camera.get_viewport_size();
    const usize32_t render_size = get_run_render_size(viewport_size, params);
    create_or_resize_d_buffer(rview, render_size, params);
    create_or_resize_g_buffer(rview, render_size, params);
    create_or_resize_l_buffer(rview, render_size, params);
    create_or_resize_reflection_buffers(rview, render_size, params);

    apply_pipeline_taa_jitter_to_camera(camera, render_size, params);

    // Velocity buffer production: camera runs that have a reader (is_velocity_wanted). It is two
    // passes at the render size, so a run without temporal effects skips it. The velocity_pass step
    // bit stays the opt-out lever: probe captures build pflags from 0 and never set it, and a
    // custom caller can clear it. Consumers receive the texture EXPLICITLY through their run
    // params (this pipeline is the only place that fetches "VELOCITY" from the render view); a
    // valid texture IS their enable - null falls back to their matrix reprojection. Also excludes
    // movers from static-mesh batching below so their G-buffer raster matches the velocity pass
    // for the EQUAL depth test.
    velocity_run_active_ =
        is_camera_run && (stages & pipeline_steps::velocity_pass) != 0u && is_velocity_wanted(params);
    if(velocity_run_active_)
    {
        model_component::request_velocity_recording(gfx::get_render_frame());
    }

    const auto visibility_set = collect_visible_models(scn, camera, params, render_mask, dt);

    run_g_buffer_pass(visibility_set, camera, rview, dt);

    run_velocity_pass(visibility_set, camera, rview, render_size);

    run_screen_ao_pass(camera, rview, dt, params);

    // SSR composites into RBUFFER from the traced clear. The GI reflections write every pixel of it
    // when they run, and without a traced layer the indirect pass reads the clear value from a
    // constant texture (get_traced_reflection_layer), so only SSR needs the clear.
    const bool gi_owns_reflections = is_camera_run && gi_reflections_own_view(params);
    gi_reflections_written_ = false;
    traced_reflections_cleared_ = false;
    if(build_reflection_probes && is_ssr_wanted(params))
    {
        clear_traced_reflections(rview);
    }
    run_reflection_probe_pass(scn, camera, rview, build_reflection_probes, gi_owns_reflections, dt);

    const bool hiz_active = run_hiz_pass(camera, rview, params, render_size, dt);

    // SSR samples last frame's PREV_SCENE_HDR snapshot (post-TAA, scene-referred linear).
    // It must NOT sample the final OBUFFER: that image is tonemapped, sRGB-encoded and has
    // UI composited on it -- display-referred values injected into linear lighting, which
    // auto exposure then meters and re-amplifies (runaway brightening in dark scenes).
    run_ssr_pass(camera, rview, params);

    // Cloud shadow map before any lighting: the directional light and the irradiance bake
    // read it.
    run_cloud_shadow_pass(scn, camera, rview);

    // The sky's irradiance SH, before GI: the radiance cache, the screen probes and the card radiosity read this
    // frame's sky on their misses (a view's first frame included), and the indirect pass composites it.
    const irradiance_pass_result irradiance = run_irradiance_pass(scn, rview);

    // Direct lighting starts the current frame LBUFFER after SSR has consumed its history source.
    target = run_direct_lighting_pass(scn, camera, rview, build_shadowmaps, params.limits, dt);

    // GI: the surface cache, the screen probe gather and the reflections. Runs after direct lighting, so this
    // frame's light buffer is populated, and before the indirect pass, which composites the results.
    bool gi_active = false;
    if(is_camera_run)
    {
        gi_active = run_gi_pass(camera, rview, params);
    }
    if(build_reflection_probes && gi_owns_reflections && !gi_reflections_written_)
    {
        clear_traced_reflections(rview);
    }

    // SSIL pass
    run_ssil_pass(camera, rview, params, gi_active);

    // Indirect lighting after SSIL so it can use the result.
    target = run_indirect_lighting_pass(camera, rview, irradiance, build_reflection_probes, dt);

    if(stages & pipeline_steps::atmospheric)
    {
        target = run_atmospherics_pass(target, scn, camera, rview, dt);
    }

    // Over the sky and the clouds; the particles drawn next fog themselves with the same fog.
    const fog_pass::shader_params fog = run_fog_pass(scn, camera, rview, target, params, build_shadowmaps);

    gfx::texture::ptr particle_reactive;
    if(stages & pipeline_steps::particles_pass)
    {
        const gfx::texture::ptr hiz_depth = hiz_active ? rview.tex_find("HIZBUFFER") : gfx::texture::ptr{};
        const bool wants_reactive = static_cast<bool>(params.fill_taa_params);
        particle_reactive =
            run_particle_pass(scn, camera, rview, target, pre_exposure, hiz_depth, build_shadowmaps, wants_reactive, dt, fog, render_mask);
    }

    if(is_camera_run)
    {
        run_gi_visualize_scene_color(camera, rview, pre_exposure);
        release_gi_visualize_card_generation();
    }

    if(is_probe_capture)
    {
        blit_to_output(rview, target, output);
        finish_run_batches();
        return;
    }

    const auto scene = acquire_scene_output(rview, output, viewport_size, render_size, params.needs_output_depth);

    target = run_taa_pass(camera, rview, target, scene.color, params, particle_reactive);

    // Scene-referred history for next frame's SSR trace and GI far-field. Taken after TAA
    // (temporally stable) and before bloom/tonemap/UI (still linear HDR, no display encode,
    // no interface pixels). Only kept while a consumer exists.
    const bool wants_scene_history =
        is_camera_run &&
        ((reflection_screen_stack_enabled(params) && params.fill_ssr_params) || params.fill_gi_params);
    snapshot_prev_scene_color(rview, target, camera, wants_scene_history);

    run_auto_exposure_pass(rview, camera, target, params, dt);

    // The scene the exposure pass metered: local exposure measures it, not the bloom composite.
    const auto scene_before_bloom = target;
    target = run_bloom_pass(rview, target, params);

    const bool is_upscaled = scene.color != output;
    target = run_tonemapping_pass(rview, target, scene_before_bloom, scene.color, params, is_upscaled);

    run_fxaa_pass(rview, target, scene.color, params, is_upscaled);

    if(is_camera_run)
    {
        run_ui_pass(scn, camera, rview, scene.color_depth);
        run_debug_passes(camera, rview, scene.color, params);
        // Whatever shaders printed this frame (nothing to draw unless one did).
        shader_print_.draw(scene.color);
        // The temporal-stability instrument measures the finished image (the lit frame or the active
        // debug view, before the editor draws its overlays) and must read LAST frame's depth, so it
        // runs before the depth snapshot below.
        run_temporal_probe_pass(camera, rview, scene.color);
    }

    if(is_upscaled)
    {
        upscale_pass::run_params upscale;
        upscale.input = scene.color;
        upscale.output = output;
        upscale.output_depth = scene.output_depth;
        upscale.input_depth = rview.tex_find("DEPTH");
        run_upscale_pass(std::move(upscale), params);
    }

    // After all passes that sample PREV_DEPTH (must follow Hi-Z / SSIL path).
    //
    // The GI is a second, independent consumer: its temporal accumulation validates
    // reprojected history against this depth and treats a missing one as "no history", so the
    // snapshot cannot be gated on the Hi-Z stack alone.
    // TAA is a third consumer: its disocclusion test compares the reprojected
    // expected depth against this snapshot (a null snapshot degrades the test to a
    // same-frame approximation on frame 0 only).
    const bool taa_active = static_cast<bool>(params.fill_taa_params);
    // GTAO is a fourth consumer: its temporal accumulation reprojects against this depth and
    // treats a missing one as "no history" (raw per-frame noise).
    const bool gtao_active = static_cast<bool>(rview.tex_find("GTAO"));
    snapshot_prev_depth(rview, render_size, hiz_active || gi_active || taa_active || gtao_active);

    finish_run_batches();
}

void deferred::finish_run_batches()
{
    batch_collector_.clear();
    skinned_batch_collector_.clear();
    // The palettes this run acquired are packed now, while their models certainly hold them: the editor may change or
    // delete entities between this run and the frame's upload.
    engine::context().get_cached<renderer>().get_skinning_palettes().pack_pending();
}

auto deferred::collect_visible_models(scene& scn,
                                      const camera& camera,
                                      const run_params& rparams,
                                      layer_mask render_mask,
                                      delta_t dt) -> visibility_set_models_t
{
    visibility_set_models_t visibility_set;
    if((rparams.pflags & pipeline_steps::geometry_pass) == 0u)
    {
        return visibility_set;
    }
    gather_visible_models(scn,
                          &camera,
                          rparams.vflags,
                          render_mask,
                          dt,
                          [&](entt::handle entity, const lod_data& lod_data)
                          {
                              visibility_set.emplace_back(visibility_data{entity, lod_data});
                          });
    return visibility_set;
}

void deferred::blit_to_output(gfx::render_view& rview,
                              const gfx::frame_buffer::ptr& input,
                              const gfx::frame_buffer::ptr& output)
{
    blit_pass::run_params pass_params;
    pass_params.input = input;
    pass_params.output = output;
    blit_pass_.run(rview, pass_params);
}

void deferred::run_gi_visualize_scene_color(const camera& camera,
                                            gfx::render_view& rview,
                                            const pre_exposure_state& pre_exposure)
{
    if(!gi_visualize_settings_.is_any_in_scene_color())
    {
        return;
    }
    gi_visualize_pass::world_params world;
    const auto& lbuffer = rview.fbo_find("LBUFFER");
    const auto& gbuffer = rview.fbo_find("GBUFFER");
    world.scene_color = lbuffer ? lbuffer->get_texture(0) : nullptr;
    world.scene_depth = gbuffer ? gbuffer->get_texture(4) : nullptr;
    world.rview = &rview;
    world.cam = &camera;
    world.surface_cache = &gi_surface_cache_pass_;
    world.gi_scene = &engine::context().get_cached<surface_cache_system>();
    world.radiance_cache = gi_gather_pass_.get_radiance_cache();
    world.pre_exposure = pre_exposure;
    world.settings = gi_visualize_settings_;
    gi_visualize_pass_.draw_world(world);
}

void deferred::release_gi_visualize_card_generation()
{
    if(gi_visualize_settings_.is_card_generation())
    {
        return;
    }
    gi_visualize_pass_.release_card_generation(engine::context().get_cached<surface_cache_system>());
}

void deferred::run_debug_passes(const camera& camera,
                                gfx::render_view& rview,
                                const gfx::frame_buffer::ptr& output,
                                const run_params& rparams)
{
    debug_view_labels_.clear();

    if(debug_pass_ == debug_pass_velocity)
    {
        run_velocity_debug_pass(camera, rview, output);
    }
    else if(debug_pass_ == debug_pass_exposure)
    {
        // An overlay over the finished image, so it runs after the UI pass like the others
        // but keeps what is underneath.
        run_exposure_debug_pass(rview, output, rparams);
    }
    else if(debug_pass_ == debug_pass_particle_overdraw)
    {
        run_particle_overdraw_debug_pass(rview, output);
    }
    else if(debug_pass_ == debug_pass_ao_bent_normals)
    {
        run_debug_visualization_pass(camera, rview, output, rparams);
    }
    else if(debug_pass_ >= debug_pass_gi_scene && debug_pass_ <= debug_pass_gi_performance_overview)
    {
        run_gi_visualize_pass(camera, rview, output, rparams);
    }
    else if(debug_pass_ >= 0 && debug_pass_ < debug_pass_gbuffer_modes)
    {
        run_debug_visualization_pass(camera, rview, output, rparams);
    }

    if(gi_visualize_settings_.is_any_overlay())
    {
        run_gi_visualize_overlays(camera, rview, output, rparams);
    }
}

void deferred::run_temporal_probe_pass(const camera& camera,
                                       gfx::render_view& rview,
                                       const gfx::frame_buffer::ptr& output)
{
    temporal_probe_pass::run_params probe_params;
    probe_params.color = output->get_texture(0);
    probe_params.velocity = rview.tex_find("VELOCITY");
    const auto& probe_gbuffer = rview.fbo_get("GBUFFER");
    probe_params.depth = probe_gbuffer ? probe_gbuffer->get_texture(4) : nullptr;
    probe_params.prev_depth = rview.tex_find("PREV_DEPTH");
    probe_params.cam = &camera;
    temporal_probe_pass_.run(probe_params);
}

void deferred::snapshot_prev_scene_color(gfx::render_view& rview,
                                         const gfx::frame_buffer::ptr& source,
                                         const camera& camera,
                                         bool has_consumer)
{
    if(!has_consumer)
    {
        rview.tex_remove("PREV_SCENE_HDR");
        return;
    }
    // This frame's G-buffer depth rides along in alpha (scene_history_pass), so the readers
    // can tell a reprojection that still shows its surface from a disoccluded one.
    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto depth = gbuffer ? gbuffer->get_texture(4) : nullptr;
    scene_history_pass_.run(rview, source, depth, camera);
}

void deferred::snapshot_prev_depth(gfx::render_view& rview, const usize32_t& viewport_size, bool has_consumer)
{
    if(!has_consumer)
    {
        rview.tex_remove("PREV_DEPTH");
        return;
    }
    auto depth_src = rview.fbo_get("GBUFFER")->get_texture(4);
    const auto& prev_depth = rview.tex_acquire(
        "PREV_DEPTH",
        {.size = viewport_size, .format = bgfx::TextureFormat::D32F, .flags = BGFX_TEXTURE_BLIT_DST});
    gfx::render_pass blit_pass("History/Prev Depth Blit Pass");
    bgfx::blit(blit_pass.id,
               bgfx::TextureRegion{.handle = prev_depth->native_handle()},
               bgfx::TextureRegion{.handle = depth_src->native_handle()});
}

void deferred::run_g_buffer_pass(const visibility_set_models_t& visibility_set,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 delta_t dt)
{
    APP_SCOPE_PERF("Rendering/G-Buffer Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& viewport_size = camera.get_viewport_size();

    const auto& gbuffer = rview.fbo_get("GBUFFER");

    gfx::render_pass pass("G-Buffer/Pass");
    pass.clear();
    pass.set_view_proj(view, proj);
    pass.bind(gbuffer.get());

    // Clear batch collector for this frame
    batch_collector_.clear();
    skinned_batch_collector_.clear();
    skinned_batching_run_ = is_skinned_batching_active();
    auto& skinning_palettes = engine::context().get_cached<renderer>().get_skinning_palettes();

    const auto& view_frustum = camera.get_frustum();

    for(const auto& element : visibility_set)
    {
        const auto& entity = element.entity;
        const auto& lod_data = element.lod_data;
        const auto& transform_comp = entity.get<transform_component>();
        auto& model_comp = entity.get<model_component>();

        const auto& model = model_comp.get_model();
        if(!model.is_valid())
        {
            continue;
        }

        const auto& world_transform = transform_comp.get_transform_global();
        const auto clip_planes = ANONYMOUS::make_geometry_clip_planes(camera);

        const auto current_time = lod_data.current_time;
        const auto current_lod_index = lod_data.current_lod_index;
        const auto target_lod_index = lod_data.target_lod_index;

        // Optimized single-component LOD transition parameters
        // Positive: current LOD fading out (1.0 -> 0.0)
        // Negative: target LOD fading in (0.0 -> -1.0)
        const float transition_progress = lod_data.transition_time > 0.0f 
            ? current_time / lod_data.transition_time 
            : 1.0f;
        
        const auto params = math::vec3{1.0f - transition_progress, 0.0f, 0.0f};      // Current LOD: positive, fading out
        const auto params_inv = math::vec3{-transition_progress, 0.0f, 0.0f};        // Target LOD: negative, fading in

        const auto& submesh_transforms = model_comp.get_submesh_transforms();
        const auto& bone_transforms = model_comp.get_bone_transforms();
        const auto& skinning_matrices = model_comp.get_skinning_transforms();

        auto camera_pos = camera.get_position();


        model::submit_callbacks callbacks;
        callbacks.setup_begin = [&](const model::submit_callbacks::params& submit_params)
        {
            if(submit_params.skinned)
            {
                stats_.drawn_skinned_models++;
            }
            else
            {
                stats_.drawn_models++;
            }
            geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;
            prog.program->begin();
            gfx::set_uniform(prog.u_camera_wpos, camera_pos);
            gfx::set_uniform(prog.u_camera_clip_planes, clip_planes);
        };
        callbacks.setup_params_per_instance = [&](const model::submit_callbacks::params& submit_params)
        {
            geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;

            gfx::set_uniform(prog.u_lod_params, params);
        };
        callbacks.setup_params_per_submesh =
            [&](const model::submit_callbacks::params& submit_params, const material& mat)
        {
            if(submit_params.skinned)
            {
                stats_.drawn_skinned_submeshes++;
            }
            else
            {
                stats_.drawn_static_submeshes++;
            }
            geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;

            bool submitted = mat.submit(prog.program.get());
            if(!submitted)
            {
                if(mat.is<pbr_material>())
                {
                    const auto& pbr = static_cast<const pbr_material&>(mat);
                    submit_pbr_material(prog, pbr);
                }
            }

            bgfx::submit(pass.id,
                         prog.program->native_handle(),
                         0,
                         submit_params.preserve_state ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL);
        };
        callbacks.setup_end = [&](const model::submit_callbacks::params& submit_params)
        {
            geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;

            prog.program->end();
        };

        model_comp.set_last_render_frame(gfx::get_render_frame());

        const auto extras = model_comp.get_submit_extras(false);

        // Check if this model can be batched (static mesh, no skinning). Movers stay
        // batched: their instances carry the previous-frame transform alongside the
        // current one, and the velocity pass re-rasterizes them with the IDENTICAL
        // instanced matrix math (vs_velocity_instanced mirrors vs_deferred_geom_instanced
        // bit for bit), so the EQUAL depth test holds without excluding them here.
        const bool is_skinned = !skinning_matrices.empty();
        // Skinned batching takes the skinned submeshes; the model's other submeshes then batch like a static model's.
        const bool batch_skinned = is_skinned && skinned_batching_run_;
        const bool can_batch = batch_collector::is_static_mesh_batching_enabled() && (!is_skinned || batch_skinned);

        if(batch_skinned)
        {
            // Movers carry their previous palettes for the velocity pass, which draws these batches again.
            const auto* prev_skinning = velocity_run_active_ && model_comp.has_motion()
                                            ? &model_comp.get_prev_skinning_transforms()
                                            : nullptr;
            const auto rows = skinning_palettes.acquire(entity, skinning_matrices, prev_skinning);
            bool is_drawn = model.submit_for_skinned_batching(skinned_batch_collector_,
                                                              world_transform,
                                                              submesh_transforms,
                                                              rows.current,
                                                              rows.previous,
                                                              current_lod_index,
                                                              params.x,
                                                              &view_frustum,
                                                              &camera,
                                                              extras);
            if(math::epsilonNotEqual(current_time, 0.0f, math::epsilon<float>()))
            {
                is_drawn = model.submit_for_skinned_batching(skinned_batch_collector_,
                                                             world_transform,
                                                             submesh_transforms,
                                                             rows.current,
                                                             rows.previous,
                                                             target_lod_index,
                                                             params_inv.x,
                                                             &view_frustum,
                                                             &camera,
                                                             extras) ||
                           is_drawn;
            }
            // Counted like the shadow path: a model whose every submesh was culled draws nothing.
            stats_.drawn_skinned_models += is_drawn ? 1 : 0;
        }

        if (can_batch)
        {
            // Movers attach their previous-frame transforms so the velocity pass's
            // instanced submit can pick their instances out of the shared batches.
            auto batch_extras = extras;
            if(velocity_run_active_ && model_comp.has_motion())
            {
                batch_extras.prev_world_transform = &model_comp.get_prev_world_transform();
                batch_extras.prev_submesh_transforms = &model_comp.get_prev_submesh_transforms();
            }
            // Collect this model for batching with appropriate transforms
            model.submit_for_batching(batch_collector_, world_transform, submesh_transforms, current_lod_index, params.x, &view_frustum, &camera, batch_extras);
            // A skinned model counted as a skinned one above.
            const uint32_t counted_models = is_skinned ? 0u : 1u;
            stats_.drawn_models += counted_models;
            // Handle LOD transitions for batched models
            if(math::epsilonNotEqual(current_time, 0.0f, math::epsilon<float>()))
            {
                model.submit_for_batching(batch_collector_, world_transform, submesh_transforms, target_lod_index, params_inv.x, &view_frustum, &camera, batch_extras);
                stats_.drawn_models += counted_models;
            }
        }
        else
        {
            // Render individually (skinned meshes, complex transforms, etc.). With skinned batching on, only the
            // model's other submeshes are left: no palettes = no skinned submeshes.
            const auto& individual_skinning = batch_skinned ? model::get_empty_skinning() : skinning_matrices;
            model.submit(world_transform,
                         submesh_transforms,
                         bone_transforms,
                         individual_skinning,
                         current_lod_index,
                         callbacks,
                         &view_frustum,
                         &camera,
                         extras);
            if(math::epsilonNotEqual(current_time, 0.0f, math::epsilon<float>()))
            {
                callbacks.setup_params_per_instance = [&](const model::submit_callbacks::params& submit_params)
                {
                    geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;

                    gfx::set_uniform(prog.u_lod_params, params_inv);
                };

                model.submit(world_transform,
                             submesh_transforms,
                             bone_transforms,
                             individual_skinning,
                             target_lod_index,
                             callbacks,
                             &view_frustum,
                             &camera,
                             extras);
            }
        }
    }

    // Mesh particles: an instance per particle and submesh, drawn with the batches whatever the batching setting.
    has_mesh_particle_instances_ = false;
    for(const auto* emitter : mesh_particle_emitters_)
    {
        emitter->collect_mesh_instances(batch_collector_, velocity_run_active_);
        has_mesh_particle_instances_ = true;
    }

    // Submit all collected batches
    if (batch_collector::is_static_mesh_batching_enabled() || has_mesh_particle_instances_)
    {
        submit_batched_geometry(pass, camera);
    }
    if(skinned_batching_run_)
    {
        submit_batched_skinned_geometry(pass, camera);
    }
    bgfx::discard();
}

namespace
{
/// The instance data of @p instance in a batch drawn with @p batch_material: a layout that carries per-instance
/// material values takes them from the instance's material, else the batch's.
template<typename InstanceData>
auto make_instance_data(const batch_instance& instance, const material& batch_material) -> InstanceData
{
    if constexpr(std::is_constructible_v<InstanceData, const batch_instance&, const material&>)
    {
        return InstanceData(instance, batch_material);
    }
    else
    {
        return InstanceData(instance);
    }
}

/**
 * Shared core of the instanced batch submits (G-buffer geometry and velocity movers).
 * Iterates the prepared batches, selects instances via @p should_include, packs the
 * selected ones DIRECTLY into a transient bgfx instance buffer as @p InstanceData
 * (which supplies packed_size() and a batch_instance conversion - two passes, count
 * then pack, so the all-instances case stays zero-copy and the filtered case allocates
 * exactly what it draws), binds the submesh buffers, and hands the draw to
 * @p submit_batch - program, uniform, state and stats differences live in the callers.
 * The instance count is clamped to what bgfx actually allocated, so a short transient
 * pool can never be overrun.
 */
template<typename InstanceData, typename BatchList, typename Filter, typename SubmitFn>
void submit_prepared_batches_instanced(const BatchList& prepared_batches,
                                       Filter&& should_include,
                                       SubmitFn&& submit_batch)
{
    for(const auto* batch : prepared_batches)
    {
        if(!batch->is_valid() || batch->instances.empty())
        {
            continue;
        }

        const auto mesh_ptr = batch->key.mesh_ptr;
        const auto material_ptr = batch->key.material_ptr;
        if(!mesh_ptr || !material_ptr)
        {
            continue;
        }

        const auto submesh = mesh_ptr->get_submesh(batch->key.submesh_index, batch->key.lod_index);
        if(!submesh)
        {
            continue;
        }

        uint32_t instance_count = 0;
        for(const auto& instance : batch->instances)
        {
            if(should_include(instance))
            {
                ++instance_count;
            }
        }
        if(instance_count == 0)
        {
            continue;
        }

        bgfx::InstanceDataBuffer instance_buffer;
        bgfx::allocInstanceDataBuffer(&instance_buffer,
                                      instance_count,
                                      static_cast<uint16_t>(InstanceData::packed_size()));
        if(!instance_buffer.data)
        {
            continue;
        }
        instance_count = std::min(instance_count, instance_buffer.num);

        auto* buffer_data = reinterpret_cast<InstanceData*>(instance_buffer.data);
        uint32_t packed = 0;
        for(const auto& instance : batch->instances)
        {
            if(packed >= instance_count)
            {
                break;
            }
            if(should_include(instance))
            {
                buffer_data[packed++] = make_instance_data<InstanceData>(instance, *material_ptr);
            }
        }

        mesh_ptr->bind_render_buffers_for_submesh(submesh, batch->key.lod_index);
        bgfx::setInstanceDataBuffer(&instance_buffer);

        submit_batch(*batch, *material_ptr, instance_count);
    }
}
} // namespace

template<typename PlainLayout, typename MaterialLayout, typename OnDraw>
void deferred::submit_prepared_geometry(batch_collector& collector,
                                        gfx::render_pass& pass,
                                        const camera& camera,
                                        geom_program& plain_program,
                                        geom_program& material_program,
                                        OnDraw&& on_draw)
{
    submit_context context;
    context.view_id = pass.id;
    context.camera_position = camera.get_position();
    // Opaque geometry needs no distance sorting.
    context.enable_distance_sorting = false;
    context.enable_profiling = true;
    collector.prepare_batches(context);
    const auto& prepared_batches = collector.get_prepared_batches();
    if(prepared_batches.empty())
    {
        return;
    }
    const auto camera_pos = camera.get_position();
    const auto clip_planes = ANONYMOUS::make_geometry_clip_planes(camera);
    // A batch whose instances carry per-instance material values (material instances sharing the draw) uses the
    // layout and program that read them; the others keep the matrix-only layout.
    plain_batches_.clear();
    material_value_batches_.clear();
    for(const auto* batch : prepared_batches)
    {
        (batch->has_material_overrides ? material_value_batches_ : plain_batches_).push_back(batch);
    }
    const auto draw_batches = [&](geom_program& program, const std::vector<const batch_group*>& batches, auto layout)
    {
        using instance_data = decltype(layout);
        if(batches.empty())
        {
            return;
        }
        program.program->begin();
        gfx::set_uniform(program.u_camera_wpos, camera_pos);
        gfx::set_uniform(program.u_camera_clip_planes, clip_planes);
        submit_prepared_batches_instanced<instance_data>(
            batches,
            [](const batch_instance&) { return true; },
            [&](const auto& /*batch*/, const material& mat, uint32_t instance_count)
            {
                const bool material_submitted = mat.submit(program.program.get());
                if(!material_submitted && mat.is<pbr_material>())
                {
                    submit_pbr_material(program, static_cast<const pbr_material&>(mat));
                }
                on_draw(program, instance_count);
                bgfx::submit(pass.id, program.program->native_handle(), 0, BGFX_DISCARD_ALL);
            });
        program.program->end();
    };
    draw_batches(plain_program, plain_batches_, PlainLayout{});
    draw_batches(material_program, material_value_batches_, MaterialLayout{});
    stats_.add_batch_stats(collector.get_stats());
}

void deferred::submit_batched_geometry(gfx::render_pass& pass, const camera& camera)
{
    APP_SCOPE_PERF("Rendering/Submit Batched Geometry");
    submit_prepared_geometry<instance_vertex_data, instance_material_vertex_data>(
        batch_collector_,
        pass,
        camera,
        geom_program_instanced_,
        geom_program_instanced_material_,
        [&](geom_program& program, uint32_t instance_count)
        {
            stats_.drawn_static_submeshes += instance_count;
            // No LOD transition: batched instances draw their LOD fully.
            const auto lod_params = math::vec3{0.0f, -1.0f, 1.0f};
            gfx::set_uniform(program.u_lod_params, lod_params);
        });

    // NOT cleared here: the velocity pass reuses the prepared batches (mover instances
    // carry their previous transforms) in the same frame. run_pipeline_impl clears the
    // collector at the end of the run, which also invalidates the transform pointers.
}

auto deferred::is_skinned_batching_active() const -> bool
{
    const auto is_usable = [](const auto& program) { return program.program && program.program->is_valid(); };
    return batch_collector::is_skinned_mesh_batching_enabled() && is_usable(geom_program_skinned_instanced_) &&
           is_usable(geom_program_skinned_instanced_material_) && is_usable(velocity_program_skinned_instanced_);
}

void deferred::submit_batched_skinned_geometry(gfx::render_pass& pass, const camera& camera)
{
    APP_SCOPE_PERF("Rendering/Submit Batched Skinned Geometry");
    auto& skinning_palettes = engine::context().get_cached<renderer>().get_skinning_palettes();
    submit_prepared_geometry<skinned_instance_vertex_data, skinned_instance_material_vertex_data>(
        skinned_batch_collector_,
        pass,
        camera,
        geom_program_skinned_instanced_,
        geom_program_skinned_instanced_material_,
        [&](geom_program& /*program*/, uint32_t instance_count)
        {
            stats_.drawn_skinned_submeshes += instance_count;
            skinning_palettes.bind();
        });
    // NOT cleared here: the velocity pass draws these batches again (submit_batched_skinned_velocity).
}

auto deferred::is_velocity_wanted(const run_params& params) const -> bool
{
    const bool has_temporal_effect = params.fill_taa_params || params.fill_gtao_params || params.fill_ssr_params ||
                                     params.fill_ssil_params || params.fill_gi_params;
    return has_temporal_effect || debug_pass_ == debug_pass_velocity || temporal_probe_pass_.is_busy();
}

void deferred::run_velocity_pass(const visibility_set_models_t& visibility_set,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 const usize32_t& render_size)
{
    if(!velocity_run_active_)
    {
        // Sole owner of the buffer's lifetime (the PREV_DEPTH convention): drop it as soon as
        // no consumer wants it instead of waiting for the idle collector.
        rview.fbo_remove("VELOCITY_FBO");
        rview.fbo_remove("VELOCITY_FBO_CAMERA");
        rview.tex_remove("VELOCITY");
        return;
    }
    if(!velocity_camera_program_.program || !velocity_camera_program_.program->is_valid())
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Velocity Pass");

    const auto& fbo = create_or_resize_v_buffer(rview, render_size);
    const auto& gbuffer = rview.fbo_get("GBUFFER");

    const auto& view = camera.get_view();
    // Jittered, deliberately: the movers sub-pass must rasterize exactly like the G-buffer for
    // the EQUAL depth test, and the fullscreen reconstruction mirrors the TAA formulation
    // (jittered current unprojection + unjittered previous reprojection, camera.h:267-275).
    const auto& proj = camera.get_projection();
    const auto prev_vp = camera.get_prev_view_projection_unjittered();

    // 1) Camera-derived velocity for every pixel, reconstructed from depth. Makes the buffer
    //    complete so consumers never branch on "does this pixel have object velocity".
    //    MUST render into the color-only FBO: this pass SAMPLES the depth texture, and with
    //    DEPTH attached the SRV/DSV conflict silently zeroes every depth read on D3D11
    //    (near-plane reconstruction -> inflated camera velocity; see create_or_resize_v_buffer).
    {
        const auto& camera_fbo = rview.fbo_get("VELOCITY_FBO_CAMERA");
        gfx::render_pass pass("Velocity/Camera Pass");
        pass.bind(camera_fbo.get());
        // Every pixel is drawn below: a clear instead of a load spares a tile-based GPU reading the old contents.
        pass.clear(BGFX_CLEAR_COLOR, 0, 1.0f, 0);
        pass.set_view_proj(view, proj);

        if(velocity_camera_program_.program->begin())
        {
            gfx::set_texture(velocity_camera_program_.s_depth,
                             0,
                             gbuffer->get_texture(4),
                             BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
                                 BGFX_SAMPLER_MAG_POINT);
            gfx::set_uniform(velocity_camera_program_.u_prev_view_proj, prev_vp.get_matrix());

            const auto topology = gfx::clip_quad(1.0f);
            bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_ALWAYS);
            bgfx::submit(pass.id, velocity_camera_program_.program->native_handle());
            bgfx::setState(BGFX_STATE_DEFAULT);
            velocity_camera_program_.program->end();
        }
    }

    // 2) Movers over the top with true per-object motion. Depth EQUAL against the shared
    //    G-buffer depth also re-creates alpha-cutout coverage for free: texels the geometry
    //    FS discarded hold another surface's depth and fail the test here.
    if(velocity_program_.program && velocity_program_.program->is_valid() && velocity_program_skinned_.program &&
       velocity_program_skinned_.program->is_valid())
    {
        gfx::render_pass pass("Velocity/Geometry Pass");
        pass.bind(fbo.get());
        pass.set_view_proj(view, proj);

        const auto& view_frustum = camera.get_frustum();

        for(const auto& element : visibility_set)
        {
            const auto& entity = element.entity;
            auto& model_comp = entity.get<model_component>();
            if(!model_comp.has_motion())
            {
                continue;
            }
            const auto& model = model_comp.get_model();
            if(!model.is_valid())
            {
                continue;
            }
            // Mover stamp for the GI reflection temporal's gate, BEFORE the batched skip so
            // batched movers (drawn by submit_batched_velocity below) count too: the buffer
            // holds object velocity this frame, so reflected-content stillness readings are
            // trustworthy only under the gate's cap for the next temporal window.
            velocity_movers_frame_ = gfx::get_render_frame();
            // Batchable movers were collected into the shared batches with their previous
            // transforms and are drawn by submit_batched_velocity below - mirroring the
            // G-buffer's can_batch split exactly so nothing draws twice.
            const bool is_skinned = !model_comp.get_skinning_transforms().empty();
            const bool batch_skinned = is_skinned && skinned_batching_run_;
            const bool batched =
                batch_collector::is_static_mesh_batching_enabled() && (!is_skinned || batch_skinned);
            if(batched)
            {
                continue;
            }

            const auto& transform_comp = entity.get<transform_component>();
            const auto& world_transform = transform_comp.get_transform_global();
            const auto& submesh_transforms = model_comp.get_submesh_transforms();
            const auto& bone_transforms = model_comp.get_bone_transforms();
            // Batched skinned submeshes are submit_batched_skinned_velocity's; the model's other submeshes stay here.
            const auto& skinning_matrices =
                batch_skinned ? model::get_empty_skinning() : model_comp.get_skinning_transforms();

            auto extras = model_comp.get_submit_extras(false);
            extras.prev_world_transform = &model_comp.get_prev_world_transform();
            extras.prev_submesh_transforms = &model_comp.get_prev_submesh_transforms();
            extras.prev_skinning_transforms = &model_comp.get_prev_skinning_transforms();

            model::submit_callbacks callbacks;
            callbacks.setup_begin = [&](const model::submit_callbacks::params& submit_params)
            {
                velocity_geom_program& prog =
                    submit_params.skinned ? velocity_program_skinned_ : velocity_program_;
                prog.program->begin();
                gfx::set_uniform(prog.u_prev_view_proj, prev_vp.get_matrix());
            };
            callbacks.setup_params_per_submesh =
                [&](const model::submit_callbacks::params& submit_params, const material& mat)
            {
                velocity_geom_program& prog =
                    submit_params.skinned ? velocity_program_skinned_ : velocity_program_;
                // Match the material's cull so two-sided surfaces keep velocity coverage, but
                // own the depth/write bits: EQUAL test, no depth write, color only.
                const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | cull_state);
                bgfx::submit(pass.id,
                             prog.program->native_handle(),
                             0,
                             submit_params.preserve_state ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL);
            };
            callbacks.setup_end = [&](const model::submit_callbacks::params& submit_params)
            {
                velocity_geom_program& prog =
                    submit_params.skinned ? velocity_program_skinned_ : velocity_program_;
                prog.program->end();
            };

            // Only the settled LOD, no crossfade second submit: dithered-out texels of the
            // fading LOD fail the EQUAL test and keep their camera-derived velocity, which is
            // a one-transition-long approximation not worth a second draw.
            model.submit(world_transform,
                         submesh_transforms,
                         bone_transforms,
                         skinning_matrices,
                         element.lod_data.current_lod_index,
                         callbacks,
                         &view_frustum,
                         &camera,
                         extras);
        }

        // Batched movers: instanced over the SAME prepared batches the G-buffer drew,
        // with the doubled per-instance stream (current + previous world matrix). Mesh particles are movers too.
        if(has_mesh_particle_instances_)
        {
            velocity_movers_frame_ = gfx::get_render_frame();
        }
        if(batch_collector::is_static_mesh_batching_enabled() || has_mesh_particle_instances_)
        {
            submit_batched_velocity(pass, prev_vp);
        }
        submit_batched_skinned_velocity(pass, prev_vp);
        bgfx::discard();
    }
}

void deferred::submit_batched_velocity(gfx::render_pass& pass, const math::transform& prev_vp)
{
    if(!velocity_program_instanced_.program || !velocity_program_instanced_.program->is_valid())
    {
        return;
    }

    // The batches (and their prepare_batches sorting) are the ones submit_batched_geometry
    // built this frame; the collector is deliberately not cleared until the end of the run.
    const auto& prepared_batches = batch_collector_.get_prepared_batches();
    if(prepared_batches.empty())
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Velocity Batched Geometry");

    velocity_program_instanced_.program->begin();
    gfx::set_uniform(velocity_program_instanced_.u_prev_view_proj, prev_vp.get_matrix());

    submit_prepared_batches_instanced<velocity_instance_vertex_data>(
        prepared_batches,
        // Only mover instances carry a previous transform; batches without any skip free.
        [](const batch_instance& instance) { return instance.prev_world_transform_ptr != nullptr; },
        [&](const auto& /*batch*/, const material& mat, uint32_t /*instance_count*/)
        {
            // Match the material's cull (two-sided coverage), own the depth/write bits:
            // EQUAL test against the G-buffer depth, no depth write, color only.
            const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | cull_state);
            bgfx::submit(pass.id, velocity_program_instanced_.program->native_handle(), 0, BGFX_DISCARD_ALL);
        });

    velocity_program_instanced_.program->end();
}

void deferred::submit_batched_skinned_velocity(gfx::render_pass& pass, const math::transform& prev_vp)
{
    auto& program = velocity_program_skinned_instanced_;
    if(!program.program || !program.program->is_valid())
    {
        return;
    }
    // The batches submit_batched_skinned_geometry drew this run; their instances carry the previous palette rows.
    const auto& prepared_batches = skinned_batch_collector_.get_prepared_batches();
    if(prepared_batches.empty())
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Velocity Batched Skinned Geometry");

    auto& skinning_palettes = engine::context().get_cached<renderer>().get_skinning_palettes();
    program.program->begin();
    gfx::set_uniform(program.u_prev_view_proj, prev_vp.get_matrix());
    submit_prepared_batches_instanced<skinned_instance_vertex_data>(
        prepared_batches,
        // Every skinned model is a mover (model_component::has_motion).
        [](const batch_instance&) { return true; },
        [&](const auto& /*batch*/, const material& mat, uint32_t /*instance_count*/)
        {
            // Match the material's cull (two-sided coverage), own the depth/write bits: EQUAL test against the
            // G-buffer depth, no depth write, color only.
            const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | cull_state);
            skinning_palettes.bind();
            bgfx::submit(pass.id, program.program->native_handle(), 0, BGFX_DISCARD_ALL);
        });
    program.program->end();
}

void deferred::run_velocity_debug_pass(const camera& camera,
                                       gfx::render_view& rview,
                                       const gfx::frame_buffer::ptr& output)
{
    if(!output)
    {
        return;
    }
    auto velocity_tex = rview.tex_find("VELOCITY");
    if(!velocity_tex || !velocity_debug_program_.program || !velocity_debug_program_.program->is_valid())
    {
        return;
    }

    gfx::render_pass pass("Debug/Velocity Pass");
    pass.bind(output.get());
    pass.set_view_proj(camera.get_view(), camera.get_projection());

    const auto output_size = output->get_size();

    velocity_debug_program_.program->begin();

    // x = pixels of motion mapped to full brightness in the visualization.
    const float debug_params[4] = {8.0f, 0.0f, 0.0f, 0.0f};
    gfx::set_uniform(velocity_debug_program_.u_params, debug_params);
    gfx::set_texture(velocity_debug_program_.s_velocity, 0, velocity_tex);

    irect32_t rect(0, 0, irect32_t::value_type(output_size.width), irect32_t::value_type(output_size.height));
    bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, velocity_debug_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    velocity_debug_program_.program->end();

    bgfx::discard();
}

void deferred::run_particle_overdraw_debug_pass(gfx::render_view& rview, const gfx::frame_buffer::ptr& output)
{
    const auto& overdraw = rview.tex_find("PARTICLE_OVERDRAW");
    if(!output || !overdraw || !particle_overdraw_debug_program_.program ||
       !particle_overdraw_debug_program_.program->is_valid())
    {
        return;
    }
    gfx::render_pass pass("Debug/Particle Overdraw Pass");
    pass.bind(output.get());
    const auto output_size = output->get_size();
    particle_overdraw_debug_program_.program->begin();
    gfx::set_texture(particle_overdraw_debug_program_.s_overdraw, 0, overdraw);
    bgfx::setScissor(0, 0, uint16_t(output_size.width), uint16_t(output_size.height));
    bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(pass.id, particle_overdraw_debug_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    particle_overdraw_debug_program_.program->end();
    bgfx::discard();
}

void deferred::run_exposure_debug_pass(gfx::render_view& rview,
                                       const gfx::frame_buffer::ptr& output,
                                       const run_params& rparams)
{
    if(!output || !exposure_debug_program_.program || !exposure_debug_program_.program->is_valid())
    {
        return;
    }
    // Auto exposure did not run for this camera (disabled, or an LDR / probe run): there is
    // nothing to draw and the lit image stays exactly as the tonemapper left it.
    auto exposure_tex = auto_exposure_pass_.get_exposure_texture(rview);
    auto history_tex = auto_exposure_pass_.get_history_texture(rview);
    auto histogram_tex = auto_exposure_pass_.get_histogram_texture(rview);
    if(!exposure_tex || !history_tex || !histogram_tex)
    {
        return;
    }
    // The live settings, for the markers the average shader decided against.
    auto_exposure_pass::settings config;
    if(rparams.fill_auto_exposure_params)
    {
        auto_exposure_pass::run_params exposure_params;
        rparams.fill_auto_exposure_params(exposure_params);
        config = exposure_params.config;
    }

    // Panel geometry in uv, bottom left: wide enough that the 256-frame trace gets about one
    // column per frame at 1080p, tall enough to separate the two bands.
    constexpr float panel_margin = 0.02f;
    constexpr float panel_width = 0.42f;
    constexpr float panel_height = 0.30f;

    gfx::render_pass pass("Debug/Exposure Pass");
    pass.bind(output.get());

    exposure_debug_program_.program->begin();
    constexpr uint64_t point_clamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    gfx::set_texture(exposure_debug_program_.s_exposure, 0, exposure_tex, point_clamp);
    gfx::set_texture(exposure_debug_program_.s_exposure_history, 1, history_tex, point_clamp);
    gfx::set_texture(exposure_debug_program_.s_exposure_histogram, 2, histogram_tex, point_clamp);
    const float rect[4] = {panel_margin, 1.0f - panel_margin - panel_height, panel_width, panel_height};
    gfx::set_uniform(exposure_debug_program_.u_exposure_debug_rect, rect);
    const float range[4] = {auto_exposure_pass::min_log_lum,
                            auto_exposure_pass::max_log_lum,
                            float(auto_exposure_pass::history_length),
                            float(auto_exposure_pass_.get_history_index(rview))};
    gfx::set_uniform(exposure_debug_program_.u_exposure_debug_range, range);
    const float settings_data[4] = {config.compensation,
                                    config.min_ev,
                                    config.max_ev,
                                    float(auto_exposure_pass::histogram_bins)};
    gfx::set_uniform(exposure_debug_program_.u_exposure_debug_settings, settings_data);
    // Blended, never opaque: the panel is an overlay on the finished frame, which this pass
    // cannot sample (it is the render target).
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(pass.id, exposure_debug_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    exposure_debug_program_.program->end();

    bgfx::discard();
}

void deferred::run_assao_pass(const camera& camera,
                              gfx::render_view& rview,
                              delta_t dt,
                              const run_params& rparams)
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_assao_params)
    {
        assao_pass_.release_resources(rview);
        return;
    }
    APP_SCOPE_PERF("Rendering/ASSAO Pass");

    const auto& gbuffer = rview.fbo_get("GBUFFER");

    auto normal = gbuffer->get_texture(1);
    auto depth = gbuffer->get_texture(4);

    assao_pass::run_params params;
    params.depth = depth.get();
    params.normal = normal.get();

    rparams.fill_assao_params(params);

    assao_pass_.run(camera, rview, params);
}

namespace
{
/// The camera moved further than auto_exposure_pass::camera_cut_distance, or turned wider than
/// auto_exposure_pass::camera_cut_degrees about its right, up or forward axis, since last frame:
/// a cut, not a move: the exposure snaps to its target and the GI histories start over.
auto is_camera_cut(const camera& cam) -> bool
{
    // The camera recorded last frame's matrices before this frame's passes ran.
    const math::transform previous = math::inverse(cam.get_prev_view());
    const math::transform current = math::inverse(cam.get_view());
    const float min_axis_cosine = std::cos(math::radians(auto_exposure_pass::camera_cut_degrees));
    const bool is_large_turn = math::dot(current.x_unit_axis(), previous.x_unit_axis()) < min_axis_cosine ||
                               math::dot(current.y_unit_axis(), previous.y_unit_axis()) < min_axis_cosine ||
                               math::dot(current.z_unit_axis(), previous.z_unit_axis()) < min_axis_cosine;
    return is_large_turn ||
           math::distance(current.get_position(), previous.get_position()) > auto_exposure_pass::camera_cut_distance;
}

/// The local exposure the tonemapper and the bloom apply: the lookups the exposure pass built for
/// this view this frame and the settings' shape. Inactive when the pass built nothing, which is
/// exactly when the settings are neutral.
auto make_local_exposure_params(const auto_exposure_pass& exposure_pass,
                                gfx::render_view& rview,
                                const pipeline::run_params& rparams) -> local_exposure_params
{
    local_exposure_params params;
    const auto local_view = exposure_pass.get_local_exposure_view(rview);
    if(!local_view.grid || !local_view.blurred || !rparams.fill_auto_exposure_params)
    {
        return params;
    }
    auto_exposure_pass::run_params exposure_params;
    rparams.fill_auto_exposure_params(exposure_params);
    const auto& config = exposure_params.config;
    params.grid = local_view.grid;
    params.blurred = local_view.blurred;
    params.tiles_x = local_view.tiles_x;
    params.tiles_y = local_view.tiles_y;
    params.grid_uv_scale = local_view.grid_uv_scale;
    params.blurred_uv_scale = local_view.blurred_uv_scale;
    params.slices = float(auto_exposure_pass::local_exposure_slices);
    params.min_log_lum = auto_exposure_pass::min_log_lum;
    params.log_lum_range = auto_exposure_pass::max_log_lum - auto_exposure_pass::min_log_lum;
    params.highlight_contrast = config.local_highlight_contrast;
    params.shadow_contrast = config.local_shadow_contrast;
    params.detail_strength = config.local_detail_strength;
    params.blurred_blend = config.local_blurred_blend;
    params.middle_grey_bias = config.local_middle_grey_bias;
    return params;
}

/// The directional light's luminous intensity in engine units (intensity x the luminance of
/// its linear colour): the one knob the sun-relative Perez sky follows (perez_luminance.h).
/// The strongest active directional light wins; 0 when the scene has none, which keeps the
/// fixed conversion.
auto find_sun_luminous_intensity(scene& scn) -> float
{
    float strongest = 0.0f;
    scn.registry->view<light_component, active_component>().each(
        [&](auto e, auto&& light_comp_ref, auto&& active)
        {
            const auto& light = light_comp_ref.get_light();
            if(light.type != light_type::directional)
            {
                return;
            }
            const auto color = light.color.to_linear();
            const float luminance = 0.2126f * color.value.r + 0.7152f * color.value.g + 0.0722f * color.value.b;
            strongest = math::max(strongest, light.intensity * luminance);
        });
    return strongest;
}
} // namespace

auto deferred::run_irradiance_pass(scene& scn, gfx::render_view& rview) -> deferred::irradiance_pass_result
{
    APP_SCOPE_PERF("Rendering/Irradiance Pass");

    irradiance_pass_result result;

    if(get_render_feature_support().is_available(render_feature::irradiance_sh) &&
       irradiance_compute_program_.program && irradiance_compute_program_.program->is_valid())
    {
        const auto& irradiance_tex = create_or_get_irradiance_texture(rview);

        struct skylight_params
        {
            float intensity = 0.0f;
            float sun_weight = 1.0f;
            float exposition = perez_luminance_to_engine;
            float sky_brightness = 1.0f;
            math::vec3 color = {1.0f, 1.0f, 1.0f};
            math::vec3 tint = {1.0f, 1.0f, 1.0f};
            math::vec3 light_dir;
            irradiance_perez_params perez;
            bool use_perez = false;
            bool is_skybox = false;
            bool use_sky = true;
            bool directional = true;
            asset_handle<gfx::texture> cubemap;
        };
        skylight_params dominant;
        const float sun_intensity = find_sun_luminous_intensity(scn);

        scn.registry->view<transform_component, skylight_component, active_component>().each(
            [&](auto e, auto&& transform_comp_ref, auto&& skylight_comp_ref, auto&& active)
            {
                const auto& skylight = skylight_comp_ref;
                float irradiance_intensity = skylight.get_irradiance_intensity();
                if(irradiance_intensity <= 0.0f)
                    return;

                const auto& world_transform = transform_comp_ref.get_transform_global();
                math::vec3 light_dir = world_transform.z_unit_axis();
                math::vec3 irradiance_color = {1.0f, 1.0f, 1.0f};
                bool use_perez = false;
                irradiance_perez_params candidate_perez;
                bool is_skybox = (skylight.get_mode() == skylight_component::sky_mode::skybox);
                // Two independent axes:
                //  - wants_sky: does the sky/environment color contribute, or is the ambient a flat tint?
                //  - directional: does the ambient vary with the surface normal (full SH), or is it flat (L0)?
                const bool wants_sky = skylight.get_irradiance_use_sky();
                const bool directional =
                    (skylight.get_irradiance_quality() == skylight_component::irradiance_quality::directional);
                float sun_weight = 1.0f;

                if(!is_skybox)
                {
                    // The shared day/night ramp (perez_luminance.h): 0 at the horizon, 1 from
                    // ~20 degrees up - the ramp the sun-relative exposition fades with too.
                    sun_weight = compute_perez_sun_weight(-light_dir.y);
                }
                float exposition = perez_luminance_to_engine;

                if(!wants_sky)
                {
                    // Sky ignored: flat artist ambient straight from the tint color. Kept independent
                    // of sun elevation (sun_weight=1) and at unit exposition so the tint reads literally.
                    sun_weight = 1.0f;
                    exposition = 1.0f;
                }
                else if(!is_skybox)
                {
                    // Sky contributes: one full Perez projection either way. `directional` only
                    // decides whether the SH bake keeps all bands (mode 1) or truncates to the
                    // L0 average (mode 5) -- the flat ambient is the SAME integral by
                    // construction. This replaced an empirical CPU-side collapse
                    // (mix(sky, sun, sun_weight * 0.25), a hand-calibrated match) with math.
                    // Computed into a candidate-local struct: writing into dominant.perez here
                    // let any later-iterated skylight stomp the true dominant's Perez params.
                    use_perez = true;
                    compute_irradiance_perez_params(light_dir, skylight.get_turbidity(), sun_intensity, candidate_perez);
                    irradiance_color = glm::mix(candidate_perez.sky_luminance_rgb, candidate_perez.sun_luminance_rgb, sun_weight);
                    // The shared Perez -> engine conversion (perez_luminance.h); the sky dome
                    // pass uses this same value, so ambient and dome cannot drift apart.
                    exposition = candidate_perez.exposition;
                }
                // skybox + wants_sky: irradiance_color stays white; the cubemap supplies the color in-shader.

                float sky_brightness = skylight.get_sky_brightness();
                exposition *= sky_brightness;

                // Tint is a picker (sRGB) color; the Perez luminances it scales are linear.
                const auto tint = skylight.get_irradiance_tint().to_linear();
                math::vec3 tint_vec = {tint.value.r, tint.value.g, tint.value.b};
                irradiance_color.x *= tint_vec.x;
                irradiance_color.y *= tint_vec.y;
                irradiance_color.z *= tint_vec.z;

                if(irradiance_intensity > dominant.intensity)
                {
                    dominant.intensity = irradiance_intensity;
                    dominant.color = irradiance_color;
                    dominant.tint = tint_vec;
                    dominant.light_dir = light_dir;
                    dominant.use_perez = use_perez;
                    dominant.perez = candidate_perez;
                    dominant.is_skybox = is_skybox;
                    dominant.use_sky = wants_sky;
                    dominant.directional = directional;
                    dominant.sun_weight = sun_weight;
                    dominant.exposition = exposition;
                    dominant.sky_brightness = sky_brightness;
                    dominant.cubemap = (is_skybox && wants_sky) ? skylight.get_cubemap() : asset_handle<gfx::texture>{};
                }
            });

        gfx::render_pass irr_pass("Irradiance/Compute Pass");
        irradiance_compute_program_.program->begin();
        bgfx::setImage(0, irradiance_tex->native_handle(), 0, bgfx::Access::Write);

        int mode = 0;
        float ambient_vec[4];
        if(dominant.use_perez)
        {
            ambient_vec[0] = dominant.tint.x;
            ambient_vec[1] = dominant.tint.y;
            ambient_vec[2] = dominant.tint.z;
            ambient_vec[3] = dominant.intensity;
        }
        else
        {
            ambient_vec[0] = dominant.color.x;
            ambient_vec[1] = dominant.color.y;
            ambient_vec[2] = dominant.color.z;
            ambient_vec[3] = dominant.intensity;
        }
        auto cubemap_tex = dominant.cubemap.get();
        const bool use_cubemap = dominant.is_skybox && dominant.use_sky && cubemap_tex && cubemap_tex->info.cubeMap;

        // Perez sky modes use physical luminance (exposition-scaled); cubemaps are typically
        // pre-baked in display range. The parity constant (perez_luminance.h) keeps the two
        // source types comparable at the same user-facing intensity slider - on the FIXED
        // conversion only: the sun-relative exposition already lands the slider's 1.0 on the
        // calibrated sky (the atmosphere's sky-to-sun ratio x the sun), a parity factor would double it.
        // The flat tint-only ambient is already in display range, so it gets no boost -- and
        // that includes the skybox-without-cubemap fallback (use_sky set but the texture
        // missing or still loading), which also renders the flat mode: gating on use_sky
        // boosted that fallback 2x and made the ambient pop when the cubemap finished loading.
        if(use_cubemap)
            ambient_vec[3] *= dominant.sky_brightness;
        else if(dominant.use_perez && !dominant.perez.sun_relative)
            ambient_vec[3] *= sky_ambient_cubemap_parity;

        gfx::set_uniform(irradiance_compute_program_.u_irradiance_tint_intensity, ambient_vec);

        // exposition: scale ambient to display range (matches atmospheric sky, ~0.1 at noon).
        float exp_val = dominant.exposition;
        float exp_vec[4] = {exp_val, 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(irradiance_compute_program_.u_exposition, exp_vec);

        // Only the cubemap modes read s_env, but the program declares it and D3D11 flags an empty
        // sampler slot on every dispatch, so the other modes bind a black cube.
        gfx::texture::ptr env_texture = default_textures::get().black_cube_texture();
        if(dominant.intensity > 0.0f && dominant.use_perez)
        {
            // mode 1 = full directional SH, mode 5 = flat (the SAME Perez integration
            // truncated to L0), mirroring the cubemap pair below.
            mode = dominant.directional ? 1 : 5;
            gfx::set_uniform(irradiance_compute_program_.u_sun_direction, dominant.perez.sun_direction);
            gfx::set_uniform(irradiance_compute_program_.u_sky_luminance_xyz, dominant.perez.sky_luminance_xyz);
            gfx::set_uniform(irradiance_compute_program_.u_perez_coeff, &dominant.perez.perez_coeff[0][0], 5);
        }
        else if(use_cubemap)
        {
            // mode 2 = full directional SH, mode 3 = flat (cubemap averaged into L0 only).
            mode = dominant.directional ? 2 : 3;
            env_texture = cubemap_tex;
        }
        else if(!dominant.use_sky && dominant.directional)
        {
            // No sky contribution but directional requested: hemisphere gradient from the tint
            // (full tint up -> darkened tint down). Flat tint-only stays at mode 0.
            mode = 4;
        }
        gfx::set_texture(irradiance_compute_program_.s_env, 1, env_texture);

        // Cloud coverage coupling: the Perez sky is blended toward an overcast grey by the mean
        // cloud transmittance (lowest mip of the cloud shadow map).
        const bool couple_clouds = dominant.use_perez && cloud_shadow_.valid && cloud_shadow_.map;
        gfx::set_texture(irradiance_compute_program_.s_cloudShadow,
                         2,
                         couple_clouds ? cloud_shadow_.map : default_textures::get().white_texture());

        // x=mode, y=sun_weight (applied in shader for all modes), z=cloud coverage coupling
        float mode_vec[4] = {float(mode), dominant.sun_weight, couple_clouds ? 1.0f : 0.0f, 0.0f};
        gfx::set_uniform(irradiance_compute_program_.u_mode, mode_vec);

        gfx::dispatch(irr_pass.id, irradiance_compute_program_.program->native_handle(), 1, 1, 1);
        irradiance_compute_program_.program->end();

        result.irradiance_tex = irradiance_tex;
        result.global_color = dominant.color;
        result.global_intensity = dominant.intensity;
    }
    else
    {
        // Fallback when irradiance compute is unavailable: still create/bind texture (zeros)
        const auto& irradiance_tex = create_or_get_irradiance_texture(rview);
        result.irradiance_tex = irradiance_tex;
        scn.registry->view<transform_component, skylight_component, active_component>().each(
            [&](auto e, auto&& transform_comp_ref, auto&& skylight_comp_ref, auto&& active)
            {
                const auto& skylight = skylight_comp_ref;
                if(skylight.get_irradiance_quality() != skylight_component::irradiance_quality::flat)
                    return;
                float irradiance_intensity = skylight.get_irradiance_intensity();
                if(irradiance_intensity <= 0.0f)
                    return;
                const auto& world_transform = transform_comp_ref.get_transform_global();
                math::vec3 light_dir = world_transform.z_unit_axis();
                math::vec3 irradiance_color = {1.0f, 1.0f, 1.0f};
                // Only fold in the sky color when sky contribution is enabled; otherwise the
                // flat tint (applied below) is the whole ambient.
                if(skylight.get_irradiance_use_sky() && skylight.get_mode() != skylight_component::sky_mode::skybox)
                {
                    math::vec3 sky_luminance_rgb;
                    math::vec3 sun_luminance_rgb;
                    compute_perez_luminance(light_dir, sky_luminance_rgb, sun_luminance_rgb);
                    float sun_weight = compute_perez_sun_weight(-light_dir.y);
                    irradiance_color = glm::mix(sky_luminance_rgb, sun_luminance_rgb, sun_weight);
                    irradiance_intensity *= sun_weight;
                }
                const auto tint = skylight.get_irradiance_tint().to_linear();
                irradiance_color.x *= tint.value.r;
                irradiance_color.y *= tint.value.g;
                irradiance_color.z *= tint.value.b;
                if(irradiance_intensity > result.global_intensity)
                {
                    result.global_intensity = irradiance_intensity;
                    result.global_color = irradiance_color;
                }
            });
    }

    return result;
}

namespace
{
/**
 * The inverse of @p camera's camera-relative view-projection: the light passes rebuild each pixel's offset from the
 * camera through it (fs_pbr_lighting.sh u_invViewProjRelative), which keeps millimetres anywhere in the world. Inverted
 * in double, so the inversion adds no rounding of its own.
 */
auto get_inverse_view_projection_relative(const camera& camera) -> math::mat4
{
    return math::mat4(glm::inverse(glm::dmat4(camera.get_view_projection_relative().get_matrix())));
}
} // namespace

auto deferred::run_direct_lighting_pass(scene& scn,
                                        const camera& camera,
                                        gfx::render_view& rview,
                                        bool apply_shadows,
                                        const feature_limits& limits,
                                        delta_t dt) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/Direct Lighting Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& camera_pos = camera.get_position();

    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& lbuffer = rview.fbo_get("LBUFFER");

    const auto buffer_size = lbuffer->get_size();

    gfx::render_pass pass("Direct Lighting/Pass");
    pass.bind(lbuffer.get());
    pass.set_view_proj(view, proj);
    pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);

    light_submit_context context;
    context.view = pass.id;
    context.cam = &camera;
    context.gbuffer = gbuffer;
    context.buffer_size = buffer_size;
    context.pre_exposure = get_pre_exposure(rview);
    context.inv_view_proj_relative = get_inverse_view_projection_relative(camera);
    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto e, auto&& transform_comp_ref, auto&& light_comp_ref, auto&& active)
        {
            if(!light_comp_ref.is_enabled())
            {
                return;
            }
            const auto light = limit_light(light_comp_ref.get_light(), limits);
            auto world_transform = transform_comp_ref.get_transform_global();
            world_transform.reset_scale();
            const auto& generator = light_comp_ref.get_shadowmap_generator();
            const bool has_shadows = light.casts_shadows && apply_shadows && generator.has_shadow_map();
            submit_light(light,
                         world_transform,
                         light_comp_ref.get_linear_color(),
                         has_shadows ? &generator : nullptr,
                         context);
        });
    submit_particle_lights(scn, context);

    bgfx::discard();

    return lbuffer;
}

void deferred::submit_light(const light& light,
                            const math::transform& world_transform,
                            const math::vec3& linear_color,
                            const shadow::shadowmap_generator* shadows,
                            const light_submit_context& context)
{
    const camera& camera = *context.cam;
    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& camera_pos = camera.get_position();
    if(!ANONYMOUS::is_light_needed(light, linear_color))
    {
        return;
    }
    const auto& light_position = world_transform.get_position();
    const auto& light_direction = world_transform.z_unit_axis();
    const math::bsphere reach = light.compute_world_bounds_sphere(light_position, light_direction);
    if(!camera.get_frustum().test_sphere(reach))
    {
        return;
    }
    irect32_t rect(0,
                   0,
                   irect32_t::value_type(context.buffer_size.width),
                   irect32_t::value_type(context.buffer_size.height));
    if(light.type != light_type::directional &&
       math::compute_projected_sphere_rect(rect.left,
                                           rect.right,
                                           rect.top,
                                           rect.bottom,
                                           reach.position,
                                           reach.radius,
                                           camera_pos,
                                           view,
                                           proj) == 0)
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Direct Lighting Pass/Per Light");

    const bool has_shadows = shadows != nullptr;

    stats_.drawn_lights++;
    stats_.drawn_lights_casting_shadows += uint32_t(has_shadows);

    const auto& lprogram = has_shadows ? get_light_program(light) : get_light_program_no_shadows(light);

    lprogram.program->begin();

    const float contact_shadow_distance = light.contact_shadow.enabled
                                           ? light.contact_shadow.ray_length
                                           : 0.0f;
    // The dither advances only while TAA integrates it (aa_data.x is the temporal frame
    // index, 0 without TAA); wrapped so the noise offsets stay in float range.
    const float contact_frame = std::fmod(camera.get_aa_data().x, float(ANONYMOUS::contact_shadow_dither_frames));
    const float contact_shadow_uniform[4] = {light.contact_shadow.thickness,
                                             light.contact_shadow.max_distance,
                                             light.contact_shadow.opacity,
                                             contact_frame};

    if(light.type == light_type::directional)
    {
        float light_data[4] = {0.0f, 0.0f, 0.0f, contact_shadow_distance};

        gfx::set_uniform(lprogram.u_light_direction, light_direction);
        gfx::set_uniform(lprogram.u_light_data, light_data);
    }
    if(light.type == light_type::point)
    {
        float light_data[4] = {light.point_data.range, 0.0f, 0.0f, contact_shadow_distance};

        gfx::set_uniform(lprogram.u_light_position, light_position);
        gfx::set_uniform(lprogram.u_light_data, light_data);
    }

    if(light.type == light_type::spot)
    {
        float light_data[4] = {light.spot_data.get_range(),
                               math::cos(math::radians(light.spot_data.get_inner_angle() * 0.5f)),
                               math::cos(math::radians(light.spot_data.get_outer_angle() * 0.5f)),
                               contact_shadow_distance};

        gfx::set_uniform(lprogram.u_light_direction, light_direction);
        gfx::set_uniform(lprogram.u_light_position, light_position);
        gfx::set_uniform(lprogram.u_light_data, light_data);
    }

    if(light.type != light_type::directional)
    {
        const auto source_axis = light.compute_source_axis(world_transform.y_unit_axis());
        const float light_source[4] = {source_axis.x, source_axis.y, source_axis.z, light.source_radius};
        gfx::set_uniform(lprogram.u_light_source, light_source);
    }

    gfx::set_uniform(lprogram.u_contact_shadow, contact_shadow_uniform);

    // Written with the view's pre-exposure, like every pass that lights the HDR buffers.
    float light_color_intensity[4] = {linear_color.x,
                                      linear_color.y,
                                      linear_color.z,
                                      light.intensity * context.pre_exposure.value};

    gfx::set_uniform(lprogram.u_light_color_intensity, light_color_intensity);

    gfx::set_uniform(lprogram.u_camera_position, camera_pos);
    gfx::set_uniform(lprogram.u_inv_view_proj_relative, context.inv_view_proj_relative);

    size_t i = 0;
    for(; i < context.gbuffer->get_attachment_count(); ++i)
    {
        gfx::set_texture(lprogram.s_tex[i], i, context.gbuffer->get_texture(i));
    }
    // Skip s_tex5 (RBUFFER) and s_tex6 (BRDF LUT) - not used by per-light direct shaders.
    // Shadow maps start at slot 7, their depth attachments at slot 12 (after the cloud shadow).
    i = 7;

    if(has_shadows)
    {
        shadows->submit_uniforms(i, ANONYMOUS::light_shadow_compare_stage);
    }

    if(light.type == light_type::directional)
    {
        // Cloud shadow map at slot 11 (after the 4 cascades). A white fallback keeps the
        // sampler bound when there is no layer; the enable flag skips the read.
        const auto cloud_shadow = ANONYMOUS::make_cloud_shadow_uniforms(cloud_shadow_);
        gfx::set_uniform(lprogram.u_cloudShadow, cloud_shadow.placement);
        gfx::set_uniform(lprogram.u_cloudShadow2, cloud_shadow.layer);
        gfx::set_texture(lprogram.s_cloudShadow,
                         11,
                         cloud_shadow.is_applied ? cloud_shadow_.map : default_textures::get().white_texture());
    }
    bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ADD);
    context.pre_exposure.bind(ANONYMOUS::light_pre_exposure_stage);
    bgfx::submit(context.view, lprogram.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);

    lprogram.program->end();
}

void deferred::submit_particle_lights(scene& scn, const light_submit_context& context)
{
    particle_lights_.clear();
    // Probes and captures leave particle lights out, as they do the particles: a passing flash is not baked.
    if(!is_camera_run_ || context.cam == nullptr)
    {
        return;
    }
    // Only emitters the views draw (billboards are marked by last frame's particle pass) light the scene.
    scn.registry->view<particle_emitter_component, active_component>().each(
        [&](auto e, auto&& emitter, auto&& active)
        {
            if(emitter.is_enabled() && emitter.get_light_count() > 0 && ps_soa::is_valid(emitter.get_emitter_handle()) &&
               emitter.was_used_last_frame() && is_on_rendered_layer(*scn.registry, e, render_mask_))
            {
                ps_soa::collect_particle_lights(emitter.get_emitter_handle(), particle_lights_);
            }
        });
    // Lights that add nothing or reach nothing in view do not take a place in the budget.
    const camera& view_camera = *context.cam;
    particle_lights_.erase(std::remove_if(particle_lights_.begin(),
                                          particle_lights_.end(),
                                          [&](const ps_soa::particle_light& particle)
                                          {
                                              if(!ANONYMOUS::is_adding_light(particle.intensity, particle.color))
                                              {
                                                  return true;
                                              }
                                              const math::vec3 reach(particle.range);
                                              return !view_camera.test_aabb(
                                                  math::bbox(particle.position - reach, particle.position + reach));
                                          }),
                           particle_lights_.end());
    // The brightest within the view's budget.
    const size_t count = math::min(particle_lights_.size(), ANONYMOUS::max_particle_lights_per_view);
    std::partial_sort(particle_lights_.begin(),
                      particle_lights_.begin() + std::ptrdiff_t(count),
                      particle_lights_.end(),
                      [](const ps_soa::particle_light& a, const ps_soa::particle_light& b)
                      {
                          return a.intensity > b.intensity;
                      });
    light point;
    point.type = light_type::point;
    point.casts_shadows = false;
    point.source_radius = 0.0f;
    point.source_length = 0.0f;
    for(size_t index = 0; index < count; ++index)
    {
        const auto& particle = particle_lights_[index];
        point.intensity = particle.intensity;
        point.point_data.range = particle.range;
        math::transform place;
        place.set_position(particle.position);
        submit_light(point, place, particle.color, nullptr, context);
    }
}

auto deferred::run_indirect_lighting_pass(const camera& camera,
                                          gfx::render_view& rview,
                                          const irradiance_pass_result& irradiance_result,
                                          bool apply_reflection,
                                          delta_t dt) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/Indirect Lighting Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& camera_pos = camera.get_position();

    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& lbuffer = rview.fbo_get("LBUFFER");

    gfx::render_pass pass("Indirect Lighting/Pass");
    pass.bind(lbuffer.get());
    pass.set_view_proj(view, proj);

    const bool samples_probe_inline = apply_reflection && inline_probe_.cubemap;
    const auto& iprogram = samples_probe_inline ? indirect_lighting_probe_program_ : indirect_lighting_program_;
    iprogram.program->begin();

    float light_data[4] = {irradiance_result.global_color.x, irradiance_result.global_color.y, irradiance_result.global_color.z, irradiance_result.global_intensity};
    gfx::set_uniform(iprogram.u_light_data, light_data);
    gfx::set_uniform(iprogram.u_camera_position, camera_pos);
    gfx::set_uniform(iprogram.u_inv_view_proj_relative, get_inverse_view_projection_relative(camera));

    size_t i = 0;
    for(; i < gbuffer->get_attachment_count(); ++i)
    {
        gfx::set_texture(iprogram.s_tex[i], i, gbuffer->get_texture(i));
    }
    gfx::set_texture(iprogram.s_tex[i],
                     i,
                     apply_reflection ? get_traced_reflection_layer(rview) : default_textures::get().black_texture());
    i++;
    gfx::set_texture(iprogram.s_tex[i], i, ibl_brdf_lut_.get());
    i++;
    gfx::set_texture(iprogram.s_irradiance, 7, irradiance_result.irradiance_tex ? irradiance_result.irradiance_tex : default_textures::get().black_texture());
    
    // Surface cache GI and SSIL produce the SAME quantity in the same units -- a hemispherical
    // indirect diffuse estimate plus the weight with which it replaces the environment probe --
    // so they feed one consumer slot and only one of them is used. The cache wins when present:
    // it sees geometry off screen and behind the camera, which SSIL cannot at any sample count.
    auto indirect_diffuse_tex = rview.tex_find("GI_RESOLVE");
    if(!indirect_diffuse_tex)
    {
        indirect_diffuse_tex = rview.tex_find("SSIL");
    }
    // Transparent (alpha 0) fallback when both are disabled/absent so the shader's
    // mix(irradiance, ssil.rgb, ssil.a) collapses to the pure SH probe. The opaque-black
    // default (alpha 1) would instead force mix() to 0 and wipe out the ambient.
    gfx::set_texture(iprogram.s_ssil,
                     8,
                     indirect_diffuse_tex ? indirect_diffuse_tex : default_textures::get().transparent_texture());
    // Whether that slot carries a real estimate: with it the shader takes the resolve outright;
    // without it the environment SH answers (fs_pbr_lighting.sh, pbr_indirect). And whether the
    // estimate is SSIL's, whose rays resolved the screen-space visibility per pixel: it takes
    // no screen-space AO, where the GI resolve takes it below its probe lattice.
    const bool indirect_diffuse_is_ssil = indirect_diffuse_tex && !rview.tex_find("GI_RESOLVE");
    // The occlusion of both indirect terms: the screen-space AO, and the GTSO table for the
    // specular. The untraced reflection layer (PBUFFER) takes the full occlusion, the traced
    // layers in RBUFFER only the material AO's.
    const auto screen_ao = get_screen_ao_inputs(rview);
    const float indirect_params[4] = {indirect_diffuse_tex ? 1.0f : 0.0f,
                                      indirect_diffuse_is_ssil ? 1.0f : 0.0f,
                                      screen_ao.multi_bounce_albedo_cap,
                                      get_gi_resolve_scale(rview)};
    gfx::set_uniform(iprogram.u_indirect_params, indirect_params);
    gfx::set_texture(iprogram.s_screen_ao, 9, screen_ao.texture);
    gfx::set_uniform(iprogram.u_screen_ao, screen_ao.params.data());
    if(samples_probe_inline)
    {
        gfx::set_texture(iprogram.s_probe_cube, 10, inline_probe_.cubemap);
        gfx::set_uniform(iprogram.u_probe_sphere, inline_probe_.sphere);
        gfx::set_uniform(iprogram.u_probe_sample, inline_probe_.sample);
        gfx::set_uniform(iprogram.u_probe_layer_params, probe_layer_inputs{}.params.data());
    }
    else
    {
        const auto probe_layer = get_probe_layer_inputs(rview, apply_reflection ? get_probe_reflection_layer(rview) : nullptr);
        gfx::set_texture(iprogram.s_probe_layer,
                         10,
                         probe_layer.texture ? probe_layer.texture : default_textures::get().black_texture());
        gfx::set_uniform(iprogram.u_probe_layer_params, probe_layer.params.data());
    }
    gfx::set_texture(iprogram.s_specular_occlusion, 12, default_textures::get().specular_occlusion().texture.get());
    gfx::set_uniform(iprogram.u_pre_exposure, get_pre_exposure(rview).to_uniform().data());
    get_pre_exposure(rview).bind(ANONYMOUS::indirect_pre_exposure_stage);
    

    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ADD);
    bgfx::submit(pass.id, iprogram.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);

    iprogram.program->end();

    bgfx::discard();

    return lbuffer;
}

void deferred::clear_traced_reflections(gfx::render_view& rview)
{
    const auto& rbuffer = rview.fbo_find("RBUFFER");
    if(!rbuffer)
    {
        return;
    }
    // The traced layers composite into RBUFFER from (0, 0, 0, 1): no traced radiance yet, and
    // the whole pixel left to the probe layer.
    gfx::render_pass traced_clear_pass("Reflections/Traced Clear");
    traced_clear_pass.bind(rbuffer.get());
    traced_clear_pass.clear(BGFX_CLEAR_COLOR, ANONYMOUS::reflection_traced_clear_rgba, 0.0f, 0);
    traced_reflections_cleared_ = true;
}

auto deferred::get_traced_reflection_layer(gfx::render_view& rview) const -> gfx::texture::ptr
{
    const auto& rbuffer = rview.fbo_find("RBUFFER");
    if(rbuffer && (traced_reflections_cleared_ || gi_reflections_written_))
    {
        return rbuffer->get_texture(0);
    }
    // Opaque black is the traced clear: no traced radiance, the whole pixel left to the probe layer.
    return default_textures::get().black_texture();
}

auto deferred::get_probe_reflection_layer(gfx::render_view& rview) const -> gfx::texture::ptr
{
    const auto& pbuffer = rview.fbo_find("PBUFFER");
    if(pbuffer && (probe_layer_written_ || gi_reflections_written_))
    {
        return pbuffer->get_texture(0);
    }
    // Transparent black is the probe layer's clear: no probe covers the pixel.
    return default_textures::get().transparent_texture();
}

void deferred::run_reflection_probe_pass(scene& scn,
                                         const camera& camera,
                                         gfx::render_view& rview,
                                         bool apply_probes,
                                         bool is_layer_read_by_gi,
                                         delta_t dt)
{
    probe_layer_written_ = false;
    inline_probe_ = {};
    if(!apply_probes)
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Reflection Probe Pass");

    std::vector<entt::entity> sorted_probes;

    // Collect the active probes that hold a bake: an unbaked probe's cubemap is uninitialized
    // memory, and a single NaN texel from it spreads across the frame through bloom.
    scn.registry->view<transform_component, reflection_probe_component, active_component>().each(
        [&](auto e, auto&& transform_comp_ref, auto&& probe_comp_ref, auto&& active)
        {
            if(probe_comp_ref.has_capture())
            {
                sorted_probes.emplace_back(e);
            }
        });

    // Sort the probes based on the method and max range
    std::sort(std::begin(sorted_probes),
              std::end(sorted_probes),
              [&](const auto& lhs, const auto& rhs)
              {
                  const auto& lhs_comp = scn.registry->get<reflection_probe_component>(lhs);
                  const auto& lhs_probe = lhs_comp.get_probe();

                  const auto& rhs_comp = scn.registry->get<reflection_probe_component>(rhs);
                  const auto& rhs_probe = rhs_comp.get_probe();

                  // Environment probes should be last
                  if(lhs_probe.method != rhs_probe.method)
                  {
                      return lhs_probe.method < rhs_probe.method; // Environment method is "greater"
                  }

                  // If the reflection methods are the same, compare based on the maximum range
                  return lhs_probe.get_max_range() > rhs_probe.get_max_range(); // Smaller ranges first
              });

    // Without probes the layer stays empty; the indirect pass reads the same empty layer from a
    // constant texture, so the full-screen clear is skipped unless the GI reflections read it.
    if(sorted_probes.empty() && !is_layer_read_by_gi)
    {
        return;
    }

    // A single probe the layer would only pass on to the indirect pass is sampled there instead.
    if(sorted_probes.size() == 1 && !is_layer_read_by_gi && !is_probe_layer_debug_view())
    {
        inline_probe_ = make_inline_probe(scn, sorted_probes.front(), rview);
        if(inline_probe_.cubemap)
        {
            return;
        }
    }

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& camera_pos = camera.get_position();

    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& pbuffer = rview.fbo_get("PBUFFER");

    const auto buffer_size = pbuffer->get_size();

    gfx::render_pass pass("Reflections/Buffer Pass");
    pass.bind(pbuffer.get());
    pass.set_view_proj(view, proj);
    pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);
    probe_layer_written_ = true;

    // Render or process the sorted probes, unoccluded: the GI reflection trace reads this layer
    // as the open sky, and the indirect pass occludes it (ComposeIndirectSpecular).
    for(const auto& e : sorted_probes)
    {
        auto& transform_comp_ref = scn.registry->get<transform_component>(e);
        auto& probe_comp_ref = scn.registry->get<reflection_probe_component>(e);

        const auto& probe = probe_comp_ref.get_probe();
        const auto& world_transform = transform_comp_ref.get_transform_global();
        const auto& probe_position = world_transform.get_position();
        const auto& probe_scale = world_transform.get_scale();

        irect32_t rect(0, 0, irect32_t::value_type(buffer_size.width), irect32_t::value_type(buffer_size.height));
        if(probe_comp_ref.compute_projected_sphere_rect(rect, probe_position, probe_scale, camera_pos, view, proj) == 0)
        {
            continue;
        }

        const auto& cubemap = probe_comp_ref.get_cubemap_prefiltered();

        ref_probe_program* ref_probe_program = nullptr;
        float influence_radius = 0.0f;
        if(probe.type == probe_type::sphere && sphere_ref_probe_program_.program)
        {
            ref_probe_program = &sphere_ref_probe_program_;
            influence_radius =
                math::max(probe_scale.x, math::max(probe_scale.y, probe_scale.z)) * probe.sphere_data.range;
        }

        if(probe.type == probe_type::box && box_ref_probe_program_.program)
        {
            math::transform t = world_transform;
            t.scale(probe.box_data.extents);
            auto u_inv_world = math::inverse(t).get_matrix();
            float data2[4] = {probe.box_data.extents.x,
                              probe.box_data.extents.y,
                              probe.box_data.extents.z,
                              probe.box_data.transition_distance};

            ref_probe_program = &box_ref_probe_program_;

            gfx::set_uniform(box_ref_probe_program_.u_inv_world, u_inv_world);
            gfx::set_uniform(box_ref_probe_program_.u_data2, data2);

            influence_radius = math::length(t.get_scale() + probe.box_data.transition_distance);
        }

        if(ref_probe_program)
        {
            float mips = cubemap ? float(cubemap->info.numMips) : 1.0f;
            float data0[4] = {
                probe_position.x,
                probe_position.y,
                probe_position.z,
                influence_radius,
            };

            const bool is_global_fallback = probe.method == reflect_method::environment;
            const float source_validity = 1.0f;
            // PBUFFER holds pre-exposed reflections; the captured cubemaps are absolute radiance.
            float data1[4] = {mips, probe.intensity * get_pre_exposure(rview).value, is_global_fallback ? 1.0f : 0.0f, source_validity};
            float capture[4] = {probe_comp_ref.get_apply_prefilter() ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};

            gfx::set_uniform(ref_probe_program->u_data0, data0);
            gfx::set_uniform(ref_probe_program->u_data1, data1);
            get_pre_exposure(rview).bind();
            gfx::set_uniform(ref_probe_program->u_capture, capture);

            for(size_t i = 0; i < gbuffer->get_attachment_count(); ++i)
            {
                gfx::set_texture(ref_probe_program->s_tex[i], i, gbuffer->get_texture(i));
            }

            gfx::set_texture(ref_probe_program->s_tex_cube, 5, cubemap);

            bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
            auto topology = gfx::clip_quad(1.0f);
            // Over-blend, rgb premultiplied by the probe's weight and alpha the union of the
            // coverages (1 - (1 - a)(1 - b)): the indirect pass and the GI reflection sky
            // fallback read that alpha as how much of the pixel the probes answer, and fill the
            // rest with the environment. BGFX_STATE_BLEND_ALPHA on alpha squared it instead.
            bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                           BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
                                                          BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                                          BGFX_STATE_BLEND_ONE,
                                                          BGFX_STATE_BLEND_INV_SRC_ALPHA));

            ref_probe_program->program->begin();
            bgfx::submit(pass.id, ref_probe_program->program->native_handle());
            bgfx::setState(BGFX_STATE_DEFAULT);
            ref_probe_program->program->end();
        }
    }

    bgfx::discard();
}

auto deferred::make_inline_probe(scene& scn, entt::entity probe, gfx::render_view& rview) const -> inline_probe
{
    auto& probe_comp = scn.registry->get<reflection_probe_component>(probe);
    const auto& settings = probe_comp.get_probe();
    const auto& cubemap = probe_comp.get_cubemap_prefiltered();
    if(settings.type != probe_type::sphere || !cubemap || !indirect_lighting_probe_program_.program ||
       !indirect_lighting_probe_program_.program->is_valid())
    {
        return {};
    }
    const auto& world_transform = scn.registry->get<transform_component>(probe).get_transform_global();
    const auto& scale = world_transform.get_scale();
    // The same probe the layer pass would draw (fs_sphere_reflection_probe: u_data0, u_data1, u_capture).
    inline_probe result;
    result.cubemap = cubemap;
    result.sphere = math::vec4(world_transform.get_position(),
                               math::max(scale.x, math::max(scale.y, scale.z)) * settings.sphere_data.range);
    result.sample = math::vec4(float(cubemap->info.numMips),
                               settings.intensity * get_pre_exposure(rview).value,
                               probe_comp.get_apply_prefilter() ? 1.0f : 0.0f,
                               1.0f);
    return result;
}

auto deferred::is_probe_layer_debug_view() const -> bool
{
    return debug_pass_ == debug_pass_ao_bent_normals || (debug_pass_ >= 0 && debug_pass_ < debug_pass_gbuffer_modes);
}

namespace
{
/// Copies the skylight settings into the atmospheric pass parameters. Returns false when the
/// scene has no active skylight. Only the sun direction depends on the directional light.
auto gather_skylight_params(scene& scn,
                            atmospheric_pass_perez::run_params& params_perez,
                            atmospheric_pass_skybox::run_params& params_skybox,
                            skylight_component::sky_mode& mode) -> bool
{
    bool found_sun = false;
    scn.registry->view<transform_component, skylight_component, active_component>().each(
        [&](auto e, auto&& transform_comp_ref, auto&& light_comp_ref, auto&& active)
        {
            auto entity = scn.create_handle(e);

            if(found_sun)
            {
                APPLOG_WARNING("[{}] More than one entity with this component. Others are ignored.", "Skylight");
                return;
            }
            const auto& cubemap = light_comp_ref.get_cubemap();
            auto cubemap_texture = cubemap.get();
            if(cubemap_texture)
            {
                if(cubemap_texture->info.cubeMap)
                {
                    params_skybox.cubemap = cubemap;
                }
            }

            mode = light_comp_ref.get_mode();
            found_sun = true;

            params_perez.turbidity = light_comp_ref.get_turbidity();
            params_perez.sky_brightness = light_comp_ref.get_sky_brightness();
            params_perez.cloud_mode = static_cast<int>(light_comp_ref.get_cloud_mode());
            params_perez.cloud_coverage = light_comp_ref.get_cloud_coverage();
            params_perez.cloud_macro_variation = light_comp_ref.get_cloud_macro_variation();
            params_perez.cloud_base_altitude = light_comp_ref.get_cloud_base_altitude();
            params_perez.cloud_thickness = light_comp_ref.get_cloud_thickness();
            params_perez.cloud_size = light_comp_ref.get_cloud_size();
            params_perez.cloud_softness = light_comp_ref.get_cloud_softness();
            params_perez.cloud_detail_erode = light_comp_ref.get_cloud_detail_erode();
            params_perez.cloud_density = light_comp_ref.get_cloud_density();
            params_perez.cloud_shadow_strength = light_comp_ref.get_cloud_shadow_strength();
            params_perez.cloud_brightness = light_comp_ref.get_cloud_brightness();
            params_perez.cloud_world_space_altitude = light_comp_ref.get_cloud_world_space_altitude();
            params_perez.cloud_shadows = light_comp_ref.get_cloud_shadows();
            params_perez.cloud_shadow_opacity = light_comp_ref.get_cloud_shadow_opacity();
            params_perez.cloud_wind_offset = light_comp_ref.get_cloud_wind_offset();
            params_perez.cloud_time = light_comp_ref.get_cloud_time();
            params_perez.sun_intensity = find_sun_luminous_intensity(scn);

            if(auto light_comp = entity.template try_get<light_component>())
            {
                const auto& light = light_comp->get_light();

                if(light.type == light_type::directional)
                {
                    const auto& world_transform = transform_comp_ref.get_transform_global();
                    params_perez.light_direction = world_transform.z_unit_axis();
                }
            }
            params_skybox.sky_brightness = light_comp_ref.get_sky_brightness();
        });
    return found_sun;
}
} // namespace

void deferred::run_cloud_shadow_pass(scene& scn, const camera& camera, gfx::render_view& rview)
{
    cloud_shadow_ = {};
    // The map is an R8 target with mips the backend builds; without them the lights take no cloud shadow.
    if(!get_render_feature_support().is_available(render_feature::cloud_shadow))
    {
        return;
    }
    atmospheric_pass_perez::run_params params_perez;
    atmospheric_pass_skybox::run_params params_skybox;
    skylight_component::sky_mode mode{};
    if(!gather_skylight_params(scn, params_perez, params_skybox, mode))
    {
        return;
    }
    if(mode == skylight_component::sky_mode::skybox)
    {
        return;
    }
    APP_SCOPE_PERF("Rendering/Cloud Shadow Pass");
    cloud_shadow_ = atmospheric_pass_perez_.run_cloud_shadow_pass(camera, rview, params_perez);
}

auto deferred::run_atmospherics_pass(gfx::frame_buffer::ptr input,
                                     scene& scn,
                                     const camera& camera,
                                     gfx::render_view& rview,
                                     delta_t dt) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/Atmospheric Pass");

    atmospheric_pass_perez::run_params params_perez;
    atmospheric_pass_skybox::run_params params_skybox;
    skylight_component::sky_mode mode{};
    const bool found_sun = gather_skylight_params(scn, params_perez, params_skybox, mode);

    if(!found_sun)
    {
        return input;
    }
    const auto pre_exposure = get_pre_exposure(rview);
    params_perez.pre_exposure = pre_exposure;
    params_skybox.sky_brightness *= pre_exposure.value;
    params_skybox.pre_exposure = pre_exposure;
    const auto& viewport_size = camera.get_viewport_size();

    auto c = camera;
    c.set_projection_mode(projection_mode::perspective);

    auto lbuffer_depth = rview.fbo_get("LBUFFER_DEPTH");

    switch(mode)
    {
        case unravel::skylight_component::sky_mode::skybox:
            atmospheric_pass_skybox_.run(lbuffer_depth, c, rview, dt, params_skybox);
            break;
        default:
            // input is the color-only LBUFFER over the same texture: the cloud composite
            // renders into it because it samples the depth that LBUFFER_DEPTH attaches.
            atmospheric_pass_perez_.run(lbuffer_depth, input, c, rview, dt, params_perez);
            break;
    }

    return input;
}

namespace
{
/**
 * The sun the fog scatters: the strongest enabled directional light, as the lit particles pick it, with its shadow
 * cascades when @p use_shadow_maps (the run built them) and the light renders them as `limits` limit it. ESM maps
 * store exponential depth, which the fog's depth test cannot read: such a sun leaves the fog unshadowed.
 */
auto find_fog_sun(scene& scn, bool use_shadow_maps, const feature_limits& limits) -> fog_pass::sun_light
{
    fog_pass::sun_light sun;
    float strongest = -1.0f;
    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& light_comp, auto&& active)
        {
            const auto light = limit_light(light_comp.get_light(), limits);
            if(!light_comp.is_enabled() || light.type != light_type::directional || light.intensity <= strongest)
            {
                return;
            }
            strongest = light.intensity;
            // The light travels along its z axis; the fog wants the direction toward it.
            sun.direction = -transform_comp.get_transform_global().z_unit_axis();
            sun.illuminance = light_comp.get_linear_color() * (light.intensity * light.volumetric_intensity);
            const auto& generator = light_comp.get_shadowmap_generator();
            const bool has_shadow = use_shadow_maps && light.casts_shadows && light.shadow_params.type != sm_impl::esm &&
                                    bgfx::isValid(generator.get_rt_texture(0));
            sun.shadow = has_shadow ? &generator : nullptr;
            sun.shadow_cascades = has_shadow ? std::uint32_t(light.directional_shadow_params.num_splits) : 0u;
            sun.shadow_depth_offset = light.shadow_params.depth == sm_depth::linear ? 0.5f : 0.0f;
        });
    return sun;
}

/// The enabled fog volumes @p camera sees.
auto gather_fog_volumes(scene& scn, const camera& camera) -> std::vector<fog_pass::fog_volume>
{
    std::vector<fog_pass::fog_volume> volumes;
    scn.registry->view<transform_component, fog_volume_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& volume_comp, auto&& active)
        {
            if(!volume_comp.enabled)
            {
                return;
            }
            const auto& world = transform_comp.get_transform_global();
            const math::vec3& extents = volume_comp.settings.extents;
            if(!camera.test_obb(math::bbox(-extents, extents), world))
            {
                return;
            }
            volumes.push_back({world, volume_comp.settings});
        });
    return volumes;
}

/// The enabled point and spot lights that scatter in the fog (volumetric intensity above 0).
/// The map the fog may read for @p generator's light: its atlas tile, once this frame's casters are in it (a tile the
/// view did not resolve may hold another light's map since the atlas last moved).
auto find_fog_light_shadow(const shadow::shadowmap_generator& generator) -> const shadow::shadowmap_generator*
{
    return generator.get_atlas_tile() && generator.already_resolved() ? &generator : nullptr;
}

auto gather_fog_local_lights(scene& scn, bool use_shadow_maps) -> std::vector<fog_pass::local_light>
{
    std::vector<fog_pass::local_light> lights;
    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& light_comp, auto&& active)
        {
            const auto& light = light_comp.get_light();
            if(!light_comp.is_enabled() || light.type == light_type::directional || light.intensity <= 0.0f ||
               light.volumetric_intensity <= 0.0f)
            {
                return;
            }
            // Scale must not skew the light's axis.
            auto world = transform_comp.get_transform_global();
            world.reset_scale();
            fog_pass::local_light local;
            local.id = static_cast<std::uint32_t>(entt::to_integral(e));
            local.position = world.get_position();
            local.direction = world.z_unit_axis();
            local.is_spot = light.type == light_type::spot;
            local.range = local.is_spot ? light.spot_data.get_range() : light.point_data.range;
            // Half angles, as the direct lighting gets them.
            local.cos_inner = math::cos(math::radians(light.spot_data.get_inner_angle() * 0.5f));
            local.cos_outer = math::cos(math::radians(light.spot_data.get_outer_angle() * 0.5f));
            local.intensity = light_comp.get_linear_color() * (light.intensity * light.volumetric_intensity);
            if(use_shadow_maps)
            {
                local.shadow = find_fog_light_shadow(light_comp.get_shadowmap_generator());
            }
            lights.push_back(local);
        });
    return lights;
}
} // namespace

auto deferred::run_fog_pass(scene& scn,
                            const camera& camera,
                            gfx::render_view& rview,
                            const gfx::frame_buffer::ptr& target,
                            const run_params& rparams,
                            bool use_shadow_maps) -> fog_pass::shader_params
{
    // The fog integrates along rays from the camera's position, which an orthographic view does not have.
    if(!rparams.fill_fog_params || camera.get_projection_mode() != projection_mode::perspective)
    {
        fog_pass_.release_volumetric(rview);
        return {};
    }
    APP_SCOPE_PERF("Rendering/Fog Pass");
    fog_pass::run_params params;
    rparams.fill_fog_params(params);
    params.target = target;
    params.depth = rview.tex_find("DEPTH");
    params.environment = rview.tex_find("IRRADIANCE_SH");
    params.pre_exposure = get_pre_exposure(rview);
    params.sun = find_fog_sun(scn, use_shadow_maps, rparams.limits);
    // The same cloud shadow the directional light applies.
    const auto cloud_shadow = ANONYMOUS::make_cloud_shadow_uniforms(cloud_shadow_);
    params.clouds.map = cloud_shadow.is_applied ? cloud_shadow_.map : gfx::texture::ptr{};
    params.clouds.placement = cloud_shadow.placement;
    params.clouds.layer = cloud_shadow.layer;
    params.allows_volumetric = rparams.limits.volumetric_fog;
    params.volumes = gather_fog_volumes(scn, camera);
    if(params.config.volumetric && params.allows_volumetric)
    {
        params.local_lights = gather_fog_local_lights(scn, use_shadow_maps);
    }
    params.is_camera_cut = is_camera_cut(camera);
    return fog_pass_.run(camera, rview, params);
}

void deferred::run_ssr_pass(const camera& camera,
                            gfx::render_view& rview,
                            const run_params& rparams)
{
    if(!is_ssr_wanted(rparams))
    {
        ssr_pass_.release_resources(rview);
        return;
    }

    ssr_pass::run_params ssr_params;

    ssr_params.output = rview.fbo_get("RBUFFER");
    ssr_params.g_buffer = rview.fbo_get("GBUFFER");

    // Last frame's post-TAA linear scene color. Before the first snapshot exists the
    // fallback is BLACK, not LBUFFER: on frame 0 the LBUFFER was just created and has
    // not been written yet (its first clear happens in the direct lighting pass, which
    // runs AFTER SSR), so it would trace one frame of undefined GPU memory as radiance.
    auto prev_scene = rview.tex_find("PREV_SCENE_HDR");
    ssr_params.previous_frame = prev_scene ? prev_scene : default_textures::get().black_texture();
    // This frame's velocity buffer, handed to the pass explicitly (a valid texture IS the enable).
    ssr_params.velocity = rview.tex_find("VELOCITY");

    ssr_params.cam = &camera;

    if(rparams.fill_ssr_params)
    {
        rparams.fill_ssr_params(ssr_params);
    }

    // Content-lag signal for the temporal's release ceiling (see run_params) - held one
    // accumulation window past the last mover draw. Placed after fill_ssr_params so the
    // settings window is final.
    const uint64_t ssr_frame_now = gfx::get_render_frame();
    ssr_params.velocity_movers_recent =
        velocity_movers_frame_ != ~0ull && ssr_frame_now >= velocity_movers_frame_ &&
        ssr_frame_now - velocity_movers_frame_ <=
            uint64_t(math::max(ssr_params.settings.fidelityfx.temporal.max_accum_frames, 1));

    ssr_params.hiz_buffer = rview.tex_get("HIZBUFFER");
    ssr_params.pre_exposure = get_pre_exposure(rview);

    // BUG Cone tracing is not working properly, so we disable it for now.
    ssr_params.settings.fidelityfx.enable_cone_tracing = false;

    ssr_pass_.run(rview, ssr_params);
}

void deferred::run_screen_ao_pass(const camera& camera, gfx::render_view& rview, delta_t dt, const run_params& rparams)
{
    if(gi_short_range_ao_owns_view(rparams))
    {
        gtao_pass_.release_resources(rview);
        rview.tex_remove("GTAO");
        assao_pass_.release_resources(rview);
        return;
    }
    // GTAO takes precedence: with both volumes enabled only GTAO runs.
    if(run_gtao_pass(camera, rview, rparams))
    {
        assao_pass_.release_resources(rview);
        return;
    }
    run_assao_pass(camera, rview, dt, rparams);
}

auto deferred::run_gtao_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams) -> bool
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_gtao_params)
    {
        gtao_pass_.release_resources(rview);
        rview.tex_remove("GTAO");
        return false;
    }
    gtao_pass::run_params gtao_params;
    gtao_params.g_buffer = rview.fbo_get("GBUFFER");
    // This frame's velocity buffer and last frame's depth; a null texture disables the
    // respective temporal path inside the pass.
    gtao_params.velocity = rview.tex_find("VELOCITY");
    gtao_params.prev_depth = rview.tex_find("PREV_DEPTH");
    gtao_params.cam = &camera;
    rparams.fill_gtao_params(gtao_params);
    auto result = gtao_pass_.run(rview, gtao_params);
    if(!result)
    {
        rview.tex_remove("GTAO");
        return false;
    }
    rview.tex_acquire("GTAO") = result;
    rview.data().get_or_emplace<gtao_pass::settings>("GTAO_SETTINGS") = gtao_params.config;
    return true;
}

auto deferred::get_probe_layer_inputs(gfx::render_view& rview, const gfx::texture::ptr& pbuffer) const
    -> probe_layer_inputs
{
    probe_layer_inputs inputs;
    inputs.texture = pbuffer;
    if(!gi_reflections_written_ || !pbuffer)
    {
        return inputs;
    }
    // The GI reflections own the view: the gather's rough specular is the whole untraced layer (no reflection
    // probes or sky specular are composited under it), read straight from its history.
    const auto rough_specular = rview.tex_find("GI_ROUGH_SPECULAR");
    inputs.texture = rough_specular ? rough_specular : default_textures::get().black_texture();
    inputs.params = {1.0f, get_gi_resolve_scale(rview), rough_specular ? 1.0f : 0.0f, 0.0f};
    return inputs;
}

auto deferred::get_screen_ao_inputs(gfx::render_view& rview) const -> screen_ao_inputs
{
    screen_ao_inputs inputs;
    inputs.texture = default_textures::get().white_texture();
    inputs.params = {1.0f, 0.0f, 1.0f, 0.0f};
    // The GI's short-range AO when the gather produced it this frame: the diffuse takes the
    // visibility through the multi-bounce fit at the post-process intensity the gather
    // published with it (no bent normal: the ambient axis stays the normal), the untraced
    // specular the bent cone.
    const auto& gi_ao = rview.tex_find(gi_gather_pass::screen_ao_texture);
    using screen_ao_state = gi_gather_pass::screen_ao_state;
    const auto* gi_ao_state = rview.data().try_get<screen_ao_state>(screen_ao_state::view_key);
    if(gi_ao && gi_ao_state && gi_ao_state->frame == uint32_t(gfx::get_render_frame()))
    {
        inputs.texture = gi_ao;
        inputs.params = {gi_ao_state->intensity, 0.0f, 1.0f, 1.0f};
        inputs.multi_bounce_albedo_cap = gi::GI_SHORT_RANGE_AO_MAX_MULTIBOUNCE_ALBEDO;
        return inputs;
    }
    const auto& gtao_tex = rview.tex_find("GTAO");
    const auto* gtao_settings = rview.data().try_get<gtao_pass::settings>("GTAO_SETTINGS");
    if(gtao_tex && gtao_settings)
    {
        inputs.texture = gtao_tex;
        inputs.params = {gtao_settings->intensity,
                         gtao_settings->bent_normal_strength,
                         gtao_settings->multi_bounce ? 1.0f : 0.0f,
                         1.0f};
    }
    else if(auto assao_tex = assao_pass_.get_ao_texture(rview))
    {
        inputs.texture = assao_tex;
    }
    return inputs;
}

void deferred::run_ssil_pass(const camera& camera,
                             gfx::render_view& rview,
                             const run_params& rparams,
                             bool gi_active)
{
    // Under GI the indirect pass reads GI_RESOLVE, never SSIL: nothing would consume the result.
    if(gi_active || !reflection_screen_stack_enabled(rparams) || !rparams.fill_ssil_params)
    {
        ssil_pass_.release_resources(rview);
        rview.tex_remove("SSIL");
        rview.tex_remove("PREV_SSIL");
        return;
    }

    ssil_pass::run_params ssil_params;
    ssil_params.g_buffer = rview.fbo_get("GBUFFER");
    ssil_params.direct_lighting = rview.fbo_get("LBUFFER")->get_texture(0);
    ssil_params.prev_depth = rview.tex_find("PREV_DEPTH");
    // This frame's velocity buffer, handed to the pass explicitly (a valid texture IS the enable).
    ssil_params.velocity = rview.tex_find("VELOCITY");
    ssil_params.prev_ssil = rview.tex_find("PREV_SSIL");
    // Last frame's environment SH (the pass that computes it runs later, in the indirect
    // lighting pass); used as the per-ray miss fallback so escaped rays integrate the
    // environment. Persists across frames in the render_view, so it is null only on frame 0.
    ssil_params.irradiance_sh = rview.tex_find("IRRADIANCE_SH");
    ssil_params.cam = &camera;

    rparams.fill_ssil_params(ssil_params);

    ssil_params.hiz_buffer = rview.tex_get("HIZBUFFER");
    ssil_params.pre_exposure = get_pre_exposure(rview);

    auto result = ssil_pass_.run(rview, ssil_params);
    rview.tex_acquire("SSIL") = result;

    if(ssil_params.settings.enable_multi_bounce && result)
    {
        // 1:1 blit of the SSIL output into PREV_SSIL. The output is full-res when the
        // trace runs reduced-res (the joint-bilateral upsample pass already reconstructed
        // it edge-aware), so feeding it back is safe -- the old failure mode was a NAIVE
        // full-viewport upscale that bled bright indirect across depth boundaries. Sizing
        // PREV_SSIL to the result keeps the blit a 1:1 copy regardless of trace resolution.
        const auto& prev_ssil =
            rview.tex_acquire("PREV_SSIL",
                              {.size = result->get_size(),
                               .format = bgfx::TextureFormat::RGBA16F,
                               .flags = BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP});
        gfx::render_pass blit_pass("SSIL/Prev SSIL Blit Pass");
        bgfx::blit(blit_pass.id,
                   bgfx::TextureRegion{.handle = prev_ssil->native_handle()},
                   bgfx::TextureRegion{.handle = result->native_handle()});
    }
    else
    {
        rview.tex_remove("PREV_SSIL");
    }

}

auto deferred::run_taa_pass(const camera& camera,
                            gfx::render_view& rview,
                            const gfx::frame_buffer::ptr& input,
                            const gfx::frame_buffer::ptr& output,
                            const run_params& rparams,
                            const gfx::texture::ptr& particle_reactive) -> gfx::frame_buffer::ptr
{
    if(!rparams.fill_taa_params)
    {
        taa_pass_.release_resources(rview);
        return input;
    }
    const auto& gbuffer = rview.fbo_find("GBUFFER");
    if(!input || !gbuffer)
    {
        return input;
    }
    taa_pass::run_params p;
    p.input = input;
    p.output = nullptr;
    p.cam = &camera;
    p.g_buffer = gbuffer;
    // Still the PREVIOUS frame's depth here (the snapshot happens at end of frame);
    // used for disocclusion rejection. Null on the first frame.
    p.prev_depth = rview.tex_find("PREV_DEPTH");
    // THIS frame's velocity buffer (produced right after the G-buffer pass); null when the
    // velocity pass is off, which drops the resolve back to camera-only depth reprojection.
    p.velocity = rview.tex_find("VELOCITY");
    p.reactive = particle_reactive;
    rparams.fill_taa_params(p);
    p.pre_exposure = get_pre_exposure(rview);
    return taa_pass_.run(rview, p);
}

auto deferred::run_fxaa_pass(gfx::render_view& rview,
                             const gfx::frame_buffer::ptr& input,
                             const gfx::frame_buffer::ptr& output,
                             const run_params& rparams,
                             bool is_upscaled) -> gfx::frame_buffer::ptr
{
    if(!rparams.fill_fxaa_params || rparams.fill_taa_params)
    {
        fxaa_pass_.release_resources(rview);
        return input;
    }

    APP_SCOPE_PERF("Rendering/FXAA Pass");

    fxaa_pass::run_params params;
    params.input = input;
    params.output = output;

    rparams.fill_fxaa_params(params);

    if(rparams.fill_hdr_params && !is_upscaled)
    {
        tonemapping_pass::run_params hdr;
        rparams.fill_hdr_params(hdr);
        params.grain_intensity = hdr.config.grain_intensity;
        params.dithering = hdr.config.dithering;
    }

    return fxaa_pass_.run(rview, params);
}

void deferred::run_upscale_pass(upscale_pass::run_params params, const run_params& rparams)
{
    APP_SCOPE_PERF("Rendering/Upscale Pass");
    if(rparams.fill_hdr_params)
    {
        tonemapping_pass::run_params hdr;
        rparams.fill_hdr_params(hdr);
        params.grain_intensity = hdr.config.grain_intensity;
        params.dithering = hdr.config.dithering;
    }
    upscale_pass_.run(params);
}

auto deferred::update_pre_exposure(gfx::render_view& rview, const run_params& params, bool is_camera_run)
    -> pre_exposure_state
{
    auto& state = rview.data().get_or_emplace<pre_exposure_state>(pre_exposure_state::view_key);
    state.previous = state.value;

    float value = 1.0f;
    const bool has_hdr_exposure = is_camera_run && params.fill_hdr_params;
    const bool auto_exposure_active =
        has_hdr_exposure && reflection_screen_stack_enabled(params) && params.fill_auto_exposure_params != nullptr;
    if(has_hdr_exposure)
    {
        // The CPU's share: the manual exposure scale (the tonemapper's exposure).
        tonemapping_pass::run_params hdr;
        params.fill_hdr_params(hdr);
        value = hdr.config.exposure;
    }
    state.value = std::clamp(value, pre_exposure_state::min_value, pre_exposure_state::max_value);
    // The adaptive share stays on the GPU, in the view's environment SH: it follows the auto exposure for the
    // camera's passes and is 1 for every other run.
    const auto& environment_sh = create_or_get_irradiance_texture(rview);
    auto_exposure_pass_.update_gpu_pre_exposure(rview, environment_sh, state.value, auto_exposure_active);
    state.gpu_texture = environment_sh->native_handle();
    state.gpu_sampler = pre_exposure_sampler_->native_handle();
    return state;
}

void deferred::run_auto_exposure_pass(gfx::render_view& rview,
                                      const camera& camera,
                                      const gfx::frame_buffer::ptr& input,
                                      const run_params& rparams,
                                      delta_t dt)
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_auto_exposure_params)
    {
        auto_exposure_pass_.release_resources(rview);
        return;
    }
    auto_exposure_pass::run_params params;
    params.input = input;
    params.delta_time = dt.count();
    params.camera_cut = is_camera_cut(camera);
    params.pre_exposure = get_pre_exposure(rview);
    rparams.fill_auto_exposure_params(params);
    auto_exposure_pass_.run(rview, params);
}

auto deferred::run_bloom_pass(gfx::render_view& rview,
                              const gfx::frame_buffer::ptr& input,
                              const run_params& rparams) -> gfx::frame_buffer::ptr
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_bloom_params || !rparams.fill_hdr_params)
    {
        bloom_pass_.release_resources(rview);
        return input;
    }
    bloom_pass::run_params params;
    params.input = input;
    rparams.fill_bloom_params(params);
    params.pre_exposure = get_pre_exposure(rview);

    if(rparams.fill_auto_exposure_params)
    {
        params.exposure_texture = auto_exposure_pass_.get_exposure_texture(rview);
        params.local_exposure = make_local_exposure_params(auto_exposure_pass_, rview, rparams);
    }
    tonemapping_pass::run_params hdr;
    rparams.fill_hdr_params(hdr);
    params.manual_exposure = hdr.config.exposure;

    return bloom_pass_.run(rview, params);
}

auto deferred::run_tonemapping_pass(gfx::render_view& rview,
                                    const gfx::frame_buffer::ptr& input,
                                    const gfx::frame_buffer::ptr& scene_before_bloom,
                                    const gfx::frame_buffer::ptr& output,
                                    const run_params& rparams,
                                    bool is_upscaled) -> gfx::frame_buffer::ptr
{
    if(!rparams.fill_hdr_params)
    {
        tonemapping_pass_.release_resources(rview);
        return input;
    }
    APP_SCOPE_PERF("Rendering/Tonemapping Pass");

    tonemapping_pass::run_params params;
    params.input = input;
    if(scene_before_bloom && scene_before_bloom != input)
    {
        params.scene_without_bloom = scene_before_bloom->get_texture();
    }

    const bool fxaa_follows = static_cast<bool>(rparams.fill_fxaa_params) && !rparams.fill_taa_params;
    if(!fxaa_follows)
    {
        params.output = output;
    }
    params.defer_output_noise = fxaa_follows || is_upscaled;

    rparams.fill_hdr_params(params);
    params.pre_exposure = get_pre_exposure(rview);

    if(rparams.fill_auto_exposure_params)
    {
        params.exposure_texture = auto_exposure_pass_.get_exposure_texture(rview);
        params.local_exposure = make_local_exposure_params(auto_exposure_pass_, rview, rparams);
    }

    return tonemapping_pass_.run(rview, params);
}

void deferred::run_gi_scene_passes(scene& scn, const camera& camera, gfx::render_view& rview, const run_params& params)
{
    // The GI scene is world state shared by every camera, refreshed once per camera-driven frame; reflection probe
    // captures skip it. Gated on a gi_component asking for GI: the update rebuilds the instance list of every model and
    // composes clipmap levels, a cost a camera without GI must not pay.
    gi_settings gi;
    if(params.run_type != pipeline_run_type::camera || !resolve_gi_settings(params, gi))
    {
        return;
    }
    auto& surface_cache = engine::context().get_cached<surface_cache_system>();
    // World half: identical for every camera, so it self-limits to once per frame.
    surface_cache.update_world(scn);
    // Camera half: the cascade is snapped around THIS viewer, so it belongs to the render
    // view. Two cameras sharing one cascade re-snapped it to each other's position every
    // frame and it never settled.
    auto& view_cache = rview.data().get_or_emplace<surface_cache_view>(surface_cache_view::view_key);
    const bool freeze_origin =
        (surface_cache.get_experiment_flags() & ANONYMOUS::gi_experiment_freeze_clipmap_origin) != 0u;
    if(!freeze_origin || !has_frozen_clipmap_camera_)
    {
        clipmap_camera_ = camera.get_position();
        has_frozen_clipmap_camera_ = freeze_origin;
    }
    view_cache.set_march_experiments(gi_pass::get_sdf_march_experiments(surface_cache.get_experiment_flags()));
    // The GPU composes the voxels when its program loaded; otherwise the CPU does, so the cascade is never left empty.
    view_cache.update(surface_cache.get_clipmap_instances(),
                      clipmap_camera_,
                      ANONYMOUS::make_gi_clipmap_settings(gi,
                                                          gi_clipmap_compose_pass_.is_valid(),
                                                          surface_cache.get_experiment_flags()),
                      surface_cache.get_content_revision());
    // The GPU composes the levels the update above marked dirty; without the program the CPU
    // composer already wrote and uploaded them.
    if(gi_clipmap_compose_pass_.is_valid())
    {
        gi_clipmap_compose_pass::run_params compose_params;
        compose_params.surface_cache = &surface_cache;
        compose_params.view_cache = &view_cache;
        gi_clipmap_compose_pass_.run(rview, compose_params);
    }
}

auto deferred::gi_short_range_ao_owns_view(const run_params& rparams) -> bool
{
    if(!reflection_screen_stack_enabled(rparams) || !gi_gather_pass_.has_short_range_ao())
    {
        return false;
    }
    // At any intensity: at 0 the gather skips the pass and the view has no screen-space AO at all.
    gi_settings gi;
    return resolve_gi_settings(rparams, gi) && gi.ambient_occlusion.enabled;
}

auto deferred::is_ssr_wanted(const run_params& rparams) -> bool
{
    return reflection_screen_stack_enabled(rparams) && rparams.fill_ssr_params && !gi_reflections_own_view(rparams);
}

auto deferred::gi_reflections_own_view(const run_params& rparams) -> bool
{
    if(!reflection_screen_stack_enabled(rparams) || !wants_hdr_buffers(rparams) || !gi_reflection_pass_.has_programs())
    {
        return false;
    }
    gi_settings gi;
    return resolve_gi_settings(rparams, gi) && gi.reflections.enabled;
}

auto deferred::run_gi_reflection_pass(gfx::render_view& rview, const gi_run_params& gather_params) -> bool
{
    const auto rbuffer = rview.fbo_find("RBUFFER");
    const auto pbuffer = rview.fbo_find("PBUFFER");
    if(!rbuffer || !pbuffer)
    {
        return false;
    }
    gi_reflection_pass::run_params params;
    params.gather = &gather_params;
    params.rough_specular = rview.tex_find("GI_ROUGH_SPECULAR");
    params.rough_specular_scale = get_gi_resolve_scale(rview);
    params.traced_output = rbuffer->get_texture(0);
    params.probe_output = pbuffer->get_texture(0);
    return gi_reflection_pass_.run(rview, params);
}

auto deferred::get_gi_resolve_scale(gfx::render_view& rview) -> float
{
    const auto* scale = rview.data().try_get<float>(gi_resolve_scale);
    return scale ? *scale : 1.0f;
}

auto deferred::resolve_gi_settings(const run_params& rparams, gi_settings& gi) -> bool
{
    // Off unless a gi_component asks for it, the same contract every other pass here follows, and off while
    // the surface cache does not run: then GTAO, ASSAO and SSR keep the AO and reflections GI would own. The
    // settings left in `gi` are meaningless when this returns false.
    if(!rparams.fill_gi_params || !engine::context().get_cached<surface_cache_system>().is_enabled())
    {
        return false;
    }
    rparams.fill_gi_params(gi);
    return true;
}

auto deferred::run_gi_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams) -> bool
{
    gfx::texture::ptr result;
    gi_settings gi;
    if(resolve_gi_settings(rparams, gi))
    {
        auto params = make_gi_run_params(camera, rview, gi);
        params.is_being_edited = rparams.is_being_edited;
        const auto& visualize = gi_visualize_settings_;
        params.visualize_traces.enabled = visualize.screen_probe_traces;
        params.visualize_traces.freeze = visualize.screen_probe_traces_freeze;
        params.visualize_traces.cursor = visualize.cursor;
        params.has_traced_reflections = gi_reflections_own_view(rparams);
        run_gi_surface_cache(camera, rview, *params.surface_cache, gi.scene);
        result = gi_gather_pass_.run(rview, params);
        rview.data().get_or_emplace<float>(gi_resolve_scale, 1.0f) = std::max(gi.diffuse.intensity, 0.0f);
        // The GI reflections run after the screen probe gather, whose rough specular they composite under the
        // traced layer.
        if(result && params.has_traced_reflections)
        {
            gi_reflections_written_ = run_gi_reflection_pass(rview, params);
        }
    }
    else
    {
        // GI is off for this camera: its surface cache, radiance cache and global distance field go (GTAO, ASSAO and
        // SSIL release theirs the same way). They are rebuilt when GI comes back.
        gi_surface_cache_pass_.release_targets();
        gi_gather_pass_.release_resources();
        rview.data().remove(surface_cache_view::view_key);
    }
    if(result)
    {
        // The accumulated result ping-pongs between two targets, so it is published under a
        // stable name for the indirect consumer rather than being looked up by its own.
        rview.tex_acquire("GI_RESOLVE") = result;
        return true;
    }
    // The consumer picks GI_RESOLVE over SSIL purely by presence, so a buffer left behind from when
    // the pass last ran would keep overriding SSIL with a frozen image.
    rview.tex_remove("GI_RESOLVE");
    rview.fbo_remove("GI_RESOLVE");
    // The rough specular goes with it: the reflections read neither without the other.
    rview.tex_remove("GI_ROUGH_SPECULAR");
    return false;
}

auto deferred::make_gi_run_params(const camera& camera, gfx::render_view& rview, const gi_settings& gi)
    -> gi_run_params
{
    gi_run_params params;
    params.settings = gi;
    params.g_buffer = rview.fbo_find("GBUFFER");
    // Still the PREVIOUS frame's depth at this point: the snapshot happens later in the
    // frame, which is exactly what temporal reprojection needs to validate history.
    params.prev_depth = rview.tex_find("PREV_DEPTH");
    // This frame's environment SH (the irradiance pass runs before GI), the sky of the rays that leave the scene.
    params.irradiance_sh = rview.tex_find("IRRADIANCE_SH");
    // This frame's Hi-Z pyramid (built earlier in the frame) for the screen traces.
    params.hiz = rview.tex_find("HIZBUFFER");
    // Last frame's post-TAA linear scene color, the radiance of screen trace hits.
    params.prev_color = rview.tex_find("PREV_SCENE_HDR");
    // This frame's velocity buffer while the velocity pass drew movers this frame or the last (the gather history
    // carries their fast update one frame): moving surfaces reproject from where they were. Without movers every
    // surface is static and the GI passes skip the velocity reads; a buffer from an earlier frame would move them by
    // stale motion.
    constexpr uint64_t gi_mover_frames = 1;
    const uint64_t render_frame = gfx::get_render_frame();
    const bool has_recent_movers = velocity_movers_frame_ != ~0ull && render_frame >= velocity_movers_frame_ &&
                                   render_frame - velocity_movers_frame_ <= gi_mover_frames;
    params.velocity = velocity_run_active_ && has_recent_movers ? rview.tex_find("VELOCITY") : nullptr;
    params.cam = &camera;
    // Every GI target, its history included, is in this run's pre-exposed space.
    params.pre_exposure = get_pre_exposure(rview);
    params.surface_cache = &engine::context().get_cached<surface_cache_system>();
    params.view_cache = rview.data().try_get<surface_cache_view>(surface_cache_view::view_key);
    params.gi_surface_cache = &gi_surface_cache_pass_;
    params.surface_cache_feedback = gi_surface_cache_pass_.get_feedback();
    const auto& ctx = engine::context();
    if(ctx.has<settings>())
    {
        params.gi_quality = ctx.get<settings>().global_illumination.global_illumination_quality;
        params.reflection_quality = ctx.get<settings>().global_illumination.reflection_quality;
    }
    params.camera_cut = is_camera_cut(camera);
    params.global_lighting_change = params.surface_cache->has_global_lighting_change();
    return params;
}

void deferred::run_gi_surface_cache(const camera& camera,
                                    gfx::render_view& rview,
                                    surface_cache_system& gi_scene,
                                    const gi_settings::scene_settings& scene_settings)
{
    APP_SCOPE_PERF("Rendering/Surface Cache");
    const auto& ctx = engine::context();
    const gi_project_settings project_settings =
        ctx.has<settings>() ? ctx.get<settings>().global_illumination : gi_project_settings{};
    const auto* view_cache = rview.data().try_get<surface_cache_view>(surface_cache_view::view_key);
    // The cards' sunlight takes this run's cloud shadow as the deferred directional light does.
    const auto cloud_uniforms = ANONYMOUS::make_cloud_shadow_uniforms(cloud_shadow_);
    gi_surface_cache_pass::cloud_shadow card_cloud_shadow;
    card_cloud_shadow.map = cloud_uniforms.is_applied ? cloud_shadow_.map : nullptr;
    card_cloud_shadow.placement = cloud_uniforms.placement;
    card_cloud_shadow.layer = cloud_uniforms.layer;
    card_cloud_shadow.signature = cloud_uniforms.is_applied ? cloud_shadow_.signature : 0u;
    gi_surface_cache_pass_.set_cloud_shadow(card_cloud_shadow);
    // The surface cache is shared by every camera: the frame's first one updates it and captures the cards; each one
    // keeps its object grid on its own distance field and lights the pages nearest to it.
    const bool is_updating_camera =
        gi_surface_cache_pass_.update(gi_scene,
                                      view_cache != nullptr ? &view_cache->get_clipmap() : nullptr,
                                      camera.get_position(),
                                      camera.get_frustum(),
                                      scene_settings,
                                      project_settings);
    if(view_cache != nullptr)
    {
        gi_surface_cache_pass_.update_object_grid(gi_scene, *view_cache);
    }
    if(is_updating_camera)
    {
        const auto& scene_stats = gi_surface_cache_pass_.get_scene().get_stats();
        stats_.gi_cards = scene_stats.cards;
        stats_.gi_captures = scene_stats.captures;
        stats_.gi_material_changes = scene_stats.material_changes;
        stats_.gi_direct_dirty_pages = scene_stats.direct_dirty_pages;
        stats_.gi_pages_used = scene_stats.pages_used;
        stats_.gi_direct_invalidated = scene_stats.direct_invalidated;
        capture_gi_cards(camera, gi_scene);
        gi_surface_cache_pass_.copy_captures();
    }
    gi_surface_cache_pass::lighting_inputs inputs;
    inputs.gi_scene = &gi_scene;
    inputs.view_cache = view_cache;
    // Last frame's environment SH: the irradiance pass runs later in the frame.
    inputs.environment_sh = rview.tex_find("IRRADIANCE_SH");
    inputs.view_exposure = get_pre_exposure(rview);
    gi_surface_cache_pass_.light(inputs);
}

void deferred::capture_gi_cards(const camera& camera, const surface_cache_system& gi_scene)
{
    const auto& captures = gi_surface_cache_pass_.get_scene().get_captures();
    auto& program = card_capture_program_;
    if(captures.empty() || !program.program || !program.program->is_valid())
    {
        return;
    }
    const auto& sources = gi_scene.get_gi_sources();
    // The camera's winding convention, which every capture's culling is matched against.
    const math::mat4 camera_view_proj = camera.get_projection() * camera.get_view();
    const float camera_orientation = math::determinant(math::mat3(camera_view_proj)) < 0.0f ? -1.0f : 1.0f;
    const math::vec2 clip_planes(camera.get_near_clip(), camera.get_far_clip());
    // No LOD fade: x = 0 never discards.
    const math::vec3 lod_params(0.0f, -1.0f, 1.0f);
    // One pass draws every capture: each draw's clip transform places it in its tile of the capture atlas and its
    // scissor keeps it there. The atlas holds this frame's captures only (the copy reads them next), so one clear of
    // the whole atlas serves them all - of its depth alone: the copy keeps a texel's colours only where the depth says
    // a surface was drawn.
    gfx::render_pass pass("GI/Card Capture");
    pass.bind(gi_surface_cache_pass_.get_capture_target().get());
    pass.clear(BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);
    for(const auto& cap : captures)
    {
        const auto& src = sources[cap.source_index];
        // The LOD the cards were built from (LOD 1 unless the asset sets one): the card planes sit on
        // that surface, and it costs a fraction of LOD 0.
        uint32_t capture_lod = src.owner ? src.owner->get_gi_cards_lod() : 0u;
        const auto* submesh = src.owner ? src.owner->get_submesh(src.submesh_index, capture_lod) : nullptr;
        if(submesh == nullptr && src.owner)
        {
            capture_lod = 0;
            submesh = src.owner->get_submesh(src.submesh_index);
        }
        if(submesh == nullptr || !src.material || !src.material->is<pbr_material>())
        {
            continue;
        }
        const auto view = gi_surface_cache_pass_.compute_capture_view(cap);
        program.program->begin();
        gfx::set_uniform(program.u_card_capture_view_proj, view.view_proj);
        // The camera sits far in front of the card so the shader's near-camera dither never fires.
        gfx::set_uniform(program.u_camera_wpos, math::vec4(view.far_eye, 0.0f));
        gfx::set_uniform(program.u_camera_clip_planes, clip_planes);
        gfx::set_uniform(program.u_lod_params, lod_params);
        gfx::set_world_transform(&src.local_to_world);
        src.owner->bind_render_buffers_for_submesh(submesh, capture_lod);
        const auto& pbr = static_cast<const pbr_material&>(*src.material);
        submit_pbr_material(program, pbr);
        // A card basis of the opposite orientation winds triangles the other way: flip culling.
        uint64_t state = pbr.get_render_states(true, true, true);
        if(view.orientation != camera_orientation)
        {
            const uint64_t cull = state & BGFX_STATE_CULL_MASK;
            state &= ~BGFX_STATE_CULL_MASK;
            state |= cull == BGFX_STATE_CULL_CW ? BGFX_STATE_CULL_CCW : (cull == BGFX_STATE_CULL_CCW ? BGFX_STATE_CULL_CW : 0);
        }
        bgfx::setState(state);
        bgfx::setScissor(uint16_t(view.scissor.left),
                         uint16_t(view.scissor.top),
                         uint16_t(view.scissor.width()),
                         uint16_t(view.scissor.height()));
        bgfx::submit(pass.id, program.program->native_handle());
        program.program->end();
    }
}

void deferred::run_debug_visualization_pass(const camera& camera,
                                            gfx::render_view& rview,
                                            const gfx::frame_buffer::ptr& output,
                                            const run_params& rparams)
{
    const tonemapping_method tonemapping = get_debug_tonemapping(rparams);
    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& irradiance_tex = create_or_get_irradiance_texture(rview);

    gfx::render_pass pass("Debug/Visualization Pass");
    pass.bind(output.get());
    pass.set_view_proj(view, proj);
    // pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);

    const auto output_size = output->get_size();

    debug_visualization_program_.program->begin();

    // The AO bent normal view lives past the G-buffer modes in the pass ids; the shader knows it as the mode
    // after those.
    const int shader_mode = debug_pass_ == debug_pass_ao_bent_normals ? debug_pass_gbuffer_modes : debug_pass_;
    // y = the debug-view scale (see set_debug_view_scale).
    float u_params[4] = {float(shader_mode), debug_view_scale_, 0.0f, 0.0f};

    gfx::set_uniform(debug_visualization_program_.u_params, u_params);
    gfx::set_uniform(debug_visualization_program_.u_pre_exposure, get_pre_exposure(rview).to_uniform().data());
    get_pre_exposure(rview).bind();

    size_t i = 0;
    for(; i < gbuffer->get_attachment_count(); ++i)
    {
        gfx::set_texture(debug_visualization_program_.s_tex[i], i, gbuffer->get_texture(i));
    }
    gfx::set_texture(debug_visualization_program_.s_tex[i], i, get_traced_reflection_layer(rview));
    ++i;
    gfx::set_texture(debug_visualization_program_.s_tex[i], i, irradiance_tex);
    ++i;
    // Whichever buffer is actually feeding the indirect consumer, so this view shows what is
    // being used rather than what used to be. Without this the surface cache result has no
    // isolated view at all, and its noise cannot be told apart from noise arriving from the
    // cache upstream of it.
    auto indirect_diffuse_tex = rview.tex_find("GI_RESOLVE");
    if(!indirect_diffuse_tex)
    {
        indirect_diffuse_tex = rview.tex_find("SSIL");
    }
    gfx::set_texture(debug_visualization_program_.s_tex[i],
                     i,
                     indirect_diffuse_tex ? indirect_diffuse_tex : default_textures::get().transparent_texture());
    const auto screen_ao = get_screen_ao_inputs(rview);
    gfx::set_texture(debug_visualization_program_.s_tex[8], 8, screen_ao.texture);
    gfx::set_uniform(debug_visualization_program_.u_screen_ao, screen_ao.params.data());
    const bool indirect_diffuse_is_ssil = indirect_diffuse_tex && !rview.tex_find("GI_RESOLVE");
    const float visualize_indirect[4] = {float(tonemapping),
                                         screen_ao.multi_bounce_albedo_cap,
                                         indirect_diffuse_is_ssil ? 1.0f : 0.0f,
                                         get_gi_resolve_scale(rview)};
    gfx::set_uniform(debug_visualization_program_.u_visualize_indirect, visualize_indirect);
    // The reflection views compose the two reflection layers the way the indirect pass does.
    const auto probe_layer = get_probe_layer_inputs(rview, get_probe_reflection_layer(rview));
    gfx::set_texture(debug_visualization_program_.s_tex[9],
                     9,
                     probe_layer.texture ? probe_layer.texture : default_textures::get().black_texture());
    gfx::set_uniform(debug_visualization_program_.u_probe_layer_params, probe_layer.params.data());
    gfx::set_texture(debug_visualization_program_.s_tex[10], 10, default_textures::get().specular_occlusion().texture.get());

    irect32_t rect(0, 0, irect32_t::value_type(output_size.width), irect32_t::value_type(output_size.height));
    bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, debug_visualization_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    debug_visualization_program_.program->end();

    bgfx::discard();
}

static_assert(deferred::debug_pass_gi_reflection_rays - deferred::debug_pass_gi_scene ==
                  int(gi_visualize_pass::view::dedicated_reflection_rays),
              "a GI debug pass id is debug_pass_gi_scene + its gi_visualize_pass::view");
static_assert(deferred::debug_pass_gi_performance_overview - deferred::debug_pass_gi_scene + 1 ==
                  int(gi_visualize_pass::view::count),
              "every gi_visualize_pass::view has a debug pass id");

void deferred::run_gi_visualize_pass(const camera& camera,
                                     gfx::render_view& rview,
                                     const gfx::frame_buffer::ptr& output,
                                     const run_params& rparams)
{
    gi_visualize_pass::run_params params;
    params.mode = gi_visualize_pass::view(debug_pass_ - debug_pass_gi_scene);
    params.output = output;
    params.cam = &camera;
    params.rview = &rview;
    params.surface_cache = &gi_surface_cache_pass_;
    params.gi_scene = &engine::context().get_cached<surface_cache_system>();
    params.view_cache = rview.data().try_get<surface_cache_view>(surface_cache_view::view_key);
    resolve_gi_settings(rparams, params.gi);
    params.exposure = get_pre_exposure(rview);
    params.tonemapping = get_debug_tonemapping(rparams);
    gi_visualize_pass_.run(params, debug_view_labels_);
}

void deferred::run_gi_visualize_overlays(const camera& camera,
                                         gfx::render_view& rview,
                                         const gfx::frame_buffer::ptr& output,
                                         const run_params& rparams)
{
    const auto& gbuffer = rview.fbo_find("GBUFFER");
    gi_visualize_pass::overlay_params params;
    params.output = output;
    params.scene_depth = gbuffer ? gbuffer->get_texture(4) : nullptr;
    params.cam = &camera;
    params.rview = &rview;
    params.gather = &gi_gather_pass_;
    params.print = &shader_print_;
    params.settings = gi_visualize_settings_;
    params.tonemapping = get_debug_tonemapping(rparams);
    // The overlays are the frame's printers: the buffer empties in a view ahead of theirs.
    shader_print_.begin_frame();
    gi_visualize_pass_.draw_overlays(params);
}

auto deferred::run_hiz_pass(const camera& camera,
                              gfx::render_view& rview,
                              const run_params& params,
                              const usize32_t& viewport_size,
                              delta_t dt) -> bool
{
    (void)dt;
    // The GI's screen traces march this same pyramid, so GI with screen traces on is a producer
    // condition of its own, whether or not the screen-space reflection stack runs.
    gi_settings gi;
    const bool gi_wants_hiz = params.run_type == pipeline_run_type::camera && resolve_gi_settings(params, gi) &&
                              (gi.diffuse.screen_traces || (gi.reflections.enabled && gi.reflections.screen_traces));
    const bool want_hiz =
        ((reflection_screen_stack_enabled(params) && (params.fill_ssr_params || params.fill_ssil_params)) ||
         gi_wants_hiz) &&
        get_render_feature_support().is_available(render_feature::hiz);

    if(!want_hiz)
    {
        rview.tex_remove("HIZBUFFER");
        // PREV_DEPTH deliberately survives. It is a SHARED history resource with more than one
        // consumer -- the GI validates reprojected history against it -- and this pass
        // runs before them, so dropping it here would destroy the next consumer's input before
        // it runs. Its lifetime belongs to the one place that decides whether to produce it,
        // at the end of the frame.
        return false;
    }

    create_or_resize_hiz_buffer(rview, viewport_size);

    APP_SCOPE_PERF("Rendering/SSR/Hi-Z Pass");

    const auto& gbuffer = rview.fbo_get("GBUFFER");
    if(!gbuffer)
    {
        return false;
    }

    hiz_pass::run_params hp;
    hp.depth_buffer = gbuffer->get_texture(4);
    hp.output_hiz = rview.tex_get("HIZBUFFER");
    hp.cam = &camera;

    hiz_pass_.run(rview, hp);
    return true;
}

deferred::deferred()
{
    init(engine::context());
}

deferred::~deferred()
{
    deinit(engine::context());
}

auto deferred::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    // Before any program, this pipeline's and its passes' alike (pipeline::init runs last).
    pre_exposure_sampler_ = std::make_shared<gfx::uniform>("s_pre_exposure", bgfx::UniformType::Sampler);

    auto load_program = [&](const std::string& vs, const std::string& fs)
    {
        auto vs_shader = am.get_asset<gfx::shader>("engine:/data/shaders/" + vs + ".sc");
        auto fs_shadfer = am.get_asset<gfx::shader>("engine:/data/shaders/" + fs + ".sc");

        return std::make_unique<gpu_program>(vs_shader, fs_shadfer);
    };

    geom_program_.cache_uniforms();
    geom_program_.program = load_program("deferred_geom/vs_deferred_geom", "deferred_geom/fs_deferred_geom");

    geom_program_skinned_.cache_uniforms();
    geom_program_skinned_.program = load_program("deferred_geom/vs_deferred_geom_skinned", "deferred_geom/fs_deferred_geom");

    geom_program_instanced_.cache_uniforms();
    geom_program_instanced_.program = load_program("deferred_geom/vs_deferred_geom_instanced", "deferred_geom/fs_deferred_geom");

    geom_program_instanced_material_.cache_uniforms();
    geom_program_instanced_material_.program = load_program("deferred_geom/vs_deferred_geom_instanced_material",
                                                            "deferred_geom/fs_deferred_geom_instance_material");

    geom_program_skinned_instanced_.cache_uniforms();
    geom_program_skinned_instanced_.program =
        load_program("deferred_geom/vs_deferred_geom_skinned_instanced", "deferred_geom/fs_deferred_geom");

    geom_program_skinned_instanced_material_.cache_uniforms();
    geom_program_skinned_instanced_material_.program =
        load_program("deferred_geom/vs_deferred_geom_skinned_instanced_material",
                     "deferred_geom/fs_deferred_geom_instance_material");

    card_capture_program_.cache_uniforms();
    card_capture_program_.program =
        load_program("deferred_geom/vs_deferred_geom_card_capture", "deferred_geom/fs_deferred_geom_card_capture");

    velocity_program_.cache_uniforms();
    velocity_program_.program = load_program("velocity/vs_velocity", "velocity/fs_velocity");

    velocity_program_skinned_.cache_uniforms();
    velocity_program_skinned_.program = load_program("velocity/vs_velocity_skinned", "velocity/fs_velocity");

    velocity_program_instanced_.cache_uniforms();
    velocity_program_instanced_.program = load_program("velocity/vs_velocity_instanced", "velocity/fs_velocity");

    velocity_program_skinned_instanced_.cache_uniforms();
    velocity_program_skinned_instanced_.program =
        load_program("velocity/vs_velocity_skinned_instanced", "velocity/fs_velocity");

    velocity_camera_program_.cache_uniforms();
    velocity_camera_program_.program = load_program("vs_clip_quad", "velocity/fs_velocity_camera");

    velocity_debug_program_.cache_uniforms();
    velocity_debug_program_.program = load_program("vs_clip_quad", "velocity/fs_velocity_debug");

    particle_overdraw_debug_program_.cache_uniforms();
    particle_overdraw_debug_program_.program = load_program("vs_clip_quad", "particles/fs_particle_overdraw_debug");

    exposure_debug_program_.cache_uniforms();
    exposure_debug_program_.program = load_program("vs_clip_quad", "exposure/fs_exposure_debug");

    sphere_ref_probe_program_.cache_uniforms();
    sphere_ref_probe_program_.program = load_program("vs_clip_quad_ex", "reflection_probe/fs_sphere_reflection_probe");

    box_ref_probe_program_.cache_uniforms();
    box_ref_probe_program_.program = load_program("vs_clip_quad_ex", "reflection_probe/fs_box_reflection_probe");

    indirect_lighting_program_.cache_uniforms();
    indirect_lighting_program_.program = load_program("vs_clip_quad", "fs_deferred_indirect_light");

    indirect_lighting_probe_program_.cache_uniforms();
    indirect_lighting_probe_program_.program = load_program("vs_clip_quad", "fs_deferred_indirect_light_probe");

    auto cs_irradiance = am.get_asset<gfx::shader>("engine:/data/shaders/irradiance/cs_irradiance_sh.sc");
    if(cs_irradiance)
    {
        irradiance_compute_program_.cache_uniforms();
        irradiance_compute_program_.program = std::make_unique<gpu_program>(cs_irradiance);
    }

    debug_visualization_program_.cache_uniforms();
    debug_visualization_program_.program = load_program("vs_clip_quad", "gbuffer/fs_gbuffer_visualize");

    // Color lighting.

    // Uniforms before programs (the cache_uniform order contract): every slot shares the
    // same uniform set, so registering one of each array is enough for all of them.
    for(auto& byLightType : color_lighting_no_shadow_)
    {
        byLightType.cache_uniforms();
    }
    for(auto& byLightType : color_lighting_)
    {
        for(auto& byDepthType : byLightType)
        {
            for(auto& bySmImpl : byDepthType)
            {
                bySmImpl.cache_uniforms();
            }
        }
    }
    for(auto& byLightType : color_lighting_pcf_tent_)
    {
        for(auto& byDepthType : byLightType)
        {
            for(auto& byTent : byDepthType)
            {
                byTent.cache_uniforms();
            }
        }
    }

    // clang-format off
    color_lighting_no_shadow_[uint8_t(light_type::spot)].program = load_program("vs_clip_quad", "fs_deferred_spot_light");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_spot_light_hard");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_pcf");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_pcss");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_vsm");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_esm");

    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_spot_light_hard_linear");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_pcf_linear");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_pcss_linear");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_vsm_linear");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_esm_linear");

    color_lighting_no_shadow_[uint8_t(light_type::point)].program = load_program("vs_clip_quad", "fs_deferred_point_light");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_point_light_hard");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_pcf");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_pcss");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_vsm");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_esm");

    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_point_light_hard_linear");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_pcf_linear");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_pcss_linear");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_vsm_linear");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_esm_linear");

    color_lighting_no_shadow_[uint8_t(light_type::directional)].program = load_program("vs_clip_quad", "fs_deferred_directional_light");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_directional_light_hard");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcf");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcss");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_vsm");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_esm");

    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_directional_light_hard_linear");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcf_linear");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcss_linear");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_vsm_linear");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_esm_linear");

    // Linear spot and point maps keep the point-tap PCF and have no tent sizes; point lights never reach the 7x7.
    color_lighting_pcf_tent_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][ANONYMOUS::pcf_tent3_slot].program = load_program("vs_clip_quad", "fs_deferred_spot_light_pcf_tent3");
    color_lighting_pcf_tent_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][ANONYMOUS::pcf_tent7_slot].program = load_program("vs_clip_quad", "fs_deferred_spot_light_pcf_tent7");
    color_lighting_pcf_tent_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][ANONYMOUS::pcf_tent3_slot].program = load_program("vs_clip_quad", "fs_deferred_point_light_pcf_tent3");
    color_lighting_pcf_tent_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][ANONYMOUS::pcf_tent3_slot].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcf_tent3");
    color_lighting_pcf_tent_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][ANONYMOUS::pcf_tent7_slot].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcf_tent7");
    color_lighting_pcf_tent_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][ANONYMOUS::pcf_tent3_slot].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcf_linear_tent3");
    color_lighting_pcf_tent_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][ANONYMOUS::pcf_tent7_slot].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcf_linear_tent7");
    // clang-format on

    ibl_brdf_lut_ = am.get_asset<gfx::texture>("engine:/data/textures/ibl_brdf_lut.png");

    return pipeline::init(ctx);
}

auto deferred::deinit(rtti::context& ctx) -> bool
{
    return true;
}


} // namespace rendering
} // namespace unravel
