#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/pre_exposure.h>

#include <graphics/render_pass.h>
#include <graphics/render_view.h>
#include <graphics/texture.h>

#include "trace_resolution.h"

namespace unravel
{

/**
 * @brief The GI's world-space specular tier (plan phase 9), layered UNDER SSR.
 *
 * Draws into the reflection buffer over the authored probes, before SSR composites the sharp
 * on-screen result on top - contributing exactly what SSR cannot: reflected content that is
 * off screen or behind the camera. Screen space belongs to SSR, which composites over this
 * pass with its own spread and fades - this pass never traces the screen. Roughness-tiered:
 * wide lobes reuse last frame's resolved gather, sharper ones trace the SDF world tier
 * (roughness-adaptive mesh-exact range, clipmap finder + mesh refine, light voxels
 * at snapped hits; unrefined clipmap hits on sharp pixels leave the authored probes).
 * Everything is owned by gi_constants; the pass has no tuning surface beyond its enable.
 */
class gi_reflection_pass
{
public:
    struct run_params
    {
        gfx::frame_buffer::ptr g_buffer;
        /// The reflection accumulation target the probes rendered into (RBUFFER).
        gfx::frame_buffer::ptr output;
        /// The frame's Hi-Z pyramid; the pass is skipped without it (depth reconstructs
        /// from mip 0).
        gfx::texture::ptr hiz;
        /// Last frame's environment SH, the past-everything fallback.
        gfx::texture::ptr irradiance_sh;
        /// The authored probe layer - RBUFFER's texture right after the probe pass drew into
        /// it - the sky answer for trace misses (multi-probe blended, holds cloud/sun detail
        /// an SH cannot). Must be null when the probe stack did not run this frame: RBUFFER
        /// is then stale with last frame's composite + SSR, and reading it would feed the
        /// pass its own output. Null binds transparent black, degrading misses to the SH.
        gfx::texture::ptr probe_layer;
        /// Last frame's resolved GI (temporally filtered, denoised E/pi per pixel) - the rough
        /// specular source: a wide lobe converges to the diffuse irradiance, and this is the
        /// smoothest per-pixel estimate the engine owns (the Lumen recipe - reuse the gather,
        /// never a raw world lattice). Null on the first frames; the shader falls back to SH.
        gfx::texture::ptr gi_diffuse;
        /// Last frame's composited scene colour (PREV_SCENE_HDR, view depth in alpha when
        /// RGBA16F): the compute trace upgrades an on-screen world hit to the exact lit
        /// pixel when the depth buffer, the hit normal and last frame's stored depth all
        /// agree. Null degrades those hits to the voxel walk.
        gfx::texture::ptr prev_color;
        /// Temporal window in frames for the stochastic ray; <= 1 bypasses the accumulation
        /// (raw passthrough) - the A/B knob for verifying the temporal is alive.
        int temporal_frames = gi::GI_REFLECTION_TEMPORAL_FRAMES;
        /// Times a far-field ray caught in an object's fattened clipmap shell resumes past it
        /// before it is shaded as that surface (the GI setting reflection_finder_resumes;
        /// clamped to [GI_REFLECTION_FINDER_RESUMES_MIN, GI_REFLECTION_FINDER_RESUMES_MAX]).
        int finder_resumes = gi::GI_REFLECTION_FINDER_RESUMES;
        /// This frame's velocity buffer, passed explicitly by the pipeline. A valid texture
        /// IS the enable; null = legacy matrix reprojection of the receiver.
        gfx::texture::ptr velocity;
        /// True while the velocity pass drew ANY mover within one temporal window: caps the
        /// temporal's stillness release at GI_REFLECTION_MOVER_STILL_CAP so a still camera
        /// watching a moving emitter cannot hold the ghost's history unclamped (see the
        /// constant's justification). Off-screen movers are accepted as uncovered by design
        /// - the signal rides the velocity pass's own draw loop, never a registry scan.
        bool velocity_movers_recent = false;
        /// Trace + accumulation resolution, the SAME knob the whole gather runs at
        /// (gi_resolve_pass::settings::resolution, default half): the composite's edge-stopped
        /// 3x3 kernel reconstructs full resolution as a joint bilateral upsample, so below-full
        /// divisors quarter (or better) the cost of the two expensive stages. Pixel-exact
        /// on-screen mirrors stay SSR's job, layered on top; what this tier uniquely
        /// contributes (off-screen content) is voxel-resolution anyway.
        trace_resolution resolution = trace_resolution::half;
        const camera* cam{};
        surface_cache_system* surface_cache{};
        surface_cache_view* view_cache{};
        /// The view's scene-color pre-exposure. The traced radiance, the accumulated mean and
        /// the composited output are in pre-exposed space (Lumen's reflections): the stores
        /// convert on read, the history and last frame's resolve by P / Pprev.
        pre_exposure_state pre_exposure{};
    };

    ~gi_reflection_pass();

    auto init(rtti::context& ctx) -> bool;
    auto run(gfx::render_view& rview, const run_params& params) -> bool;

private:
    struct reflection_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_reflection_camera;
        gfx::program::uniform_ptr u_gi_reflection_jitter;
        /// x = far-field finder resumes (run_params::finder_resumes); yzw unused.
        gfx::program::uniform_ptr u_gi_reflection_trace;
        gfx::program::uniform_ptr u_gi_light_voxel_params;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_gi_probe_layer;
        gfx::program::uniform_ptr s_hiz;
        gfx::program::uniform_ptr s_gi_diffuse;
        gfx::program::uniform_ptr s_light_voxels;
        gfx::program::uniform_ptr s_gi_env_sh;
        /// View pre-exposure (pre_exposure.sh): the space the traced radiance is written in.
        gfx::program::uniform_ptr u_pre_exposure;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_camera, "u_gi_reflection_camera", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_jitter, "u_gi_reflection_jitter", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_trace, "u_gi_reflection_trace", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_light_voxel_params, "u_gi_light_voxel_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", bgfx::UniformType::Vec4, gi::GI_SDF_GRID_PARAMS_VEC4);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", bgfx::UniformType::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_probe_layer, "s_gi_probe_layer", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_hiz, "s_hiz", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_diffuse, "s_gi_diffuse", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_light_voxels, "s_light_voxels", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_env_sh, "s_gi_env_sh", bgfx::UniformType::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } program_;

    /// The deliverable trace path: classify answers sky / degenerate / rough texels and
    /// compacts the tracing ones into a dense list, args sizes the indirect launch, and the
    /// 64-lane trace groups run only rays - the fragment form (program_, kept as the
    /// fallback) paid a whole wave wherever one quad pixel traced, and its worst-case
    /// register footprint throttled even the early-out pixels.
    struct reflection_classify_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_reflection_camera;
        gfx::program::uniform_ptr u_gi_reflection_jitter;
        gfx::program::uniform_ptr u_gi_reflection_texel;
        gfx::program::uniform_ptr s_hiz;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_gi_diffuse;
        gfx::program::uniform_ptr s_gi_env_sh;
        /// View pre-exposure (pre_exposure.sh): the rough tier answers here, in the same space.
        gfx::program::uniform_ptr u_pre_exposure;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_camera, "u_gi_reflection_camera", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_jitter, "u_gi_reflection_jitter", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_texel, "u_gi_reflection_texel", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_hiz, "s_hiz", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_diffuse, "s_gi_diffuse", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_env_sh, "s_gi_env_sh", bgfx::UniformType::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } classify_program_;

    struct reflection_args_program : uniforms_cache
    {
        gpu_program::ptr program;
        /// The environment SH texture the args pass stages into the list's SH block: the
        /// trace kernel reads the sky from the list, which freed its stage 14 for the
        /// previous-frame colour.
        gfx::program::uniform_ptr s_gi_env_sh;

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_gi_env_sh, "s_gi_env_sh", bgfx::UniformType::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } args_program_;

    struct reflection_trace_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_reflection_camera;
        gfx::program::uniform_ptr u_gi_reflection_jitter;
        gfx::program::uniform_ptr u_gi_reflection_texel;
        /// x = far-field finder resumes (run_params::finder_resumes); yzw unused.
        gfx::program::uniform_ptr u_gi_reflection_trace;
        gfx::program::uniform_ptr u_gi_light_voxel_params;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_gi_probe_layer;
        gfx::program::uniform_ptr s_hiz;
        gfx::program::uniform_ptr s_gi_diffuse;
        gfx::program::uniform_ptr s_light_voxels;
        /// Stage 14 of the compute form: last frame's composited colour (the sky SH moved
        /// into the list buffer's SH block to free the stage).
        gfx::program::uniform_ptr s_gi_prev_color;
        gfx::program::uniform_ptr s_gi_attr_albedo;
        /// Reprojection of a world hit into last frame's snapshot (the unjittered pair).
        gfx::program::uniform_ptr u_gi_refl_prev_view_proj;
        /// x = 0 no previous colour, 1 colour only, 2 colour with view depth in alpha.
        gfx::program::uniform_ptr u_gi_reflection_screen;
        /// View pre-exposure (pre_exposure.sh): the space the traced radiance is written in.
        gfx::program::uniform_ptr u_pre_exposure;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_camera, "u_gi_reflection_camera", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_jitter, "u_gi_reflection_jitter", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_texel, "u_gi_reflection_texel", bgfx::UniformType::Vec4);
            cache_uniform(program.get(),
                          u_gi_refl_prev_view_proj,
                          "u_gi_refl_prev_view_proj",
                          bgfx::UniformType::Mat4);
            cache_uniform(program.get(), u_gi_reflection_screen, "u_gi_reflection_screen", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_trace, "u_gi_reflection_trace", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_light_voxel_params, "u_gi_light_voxel_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", bgfx::UniformType::Vec4, gi::GI_SDF_GRID_PARAMS_VEC4);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", bgfx::UniformType::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_probe_layer, "s_gi_probe_layer", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_hiz, "s_hiz", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_diffuse, "s_gi_diffuse", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_light_voxels, "s_light_voxels", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_prev_color, "s_gi_prev_color", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_attr_albedo, "s_gi_attr_albedo", bgfx::UniformType::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } trace_program_;

    /// Compacted tracing-texel list: [0] append cursor (reset by args for the next frame),
    /// [1] staged trace count, [2+] packed coords. Raw uint indices - no typed-UAV floats.
    bgfx::DynamicIndexBufferHandle refl_list_{bgfx::kInvalidHandle};
    uint32_t refl_list_capacity_{0};
    /// One entry: the trace launch, ceil(count / 64) groups folded into Y past the X limit.
    bgfx::IndirectBufferHandle refl_args_{bgfx::kInvalidHandle};

    struct temporal_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_refl_prev_view_proj;
        gfx::program::uniform_ptr u_gi_refl_temporal;
        gfx::program::uniform_ptr u_gi_refl_velocity;
        gfx::program::uniform_ptr u_gi_reflection_camera;
        /// This frame's R2 offset - the SAME value the trace programs get, so the resolve can
        /// re-derive the ray each neighbouring texel fired (gi_reflection_sampling.sh).
        gfx::program::uniform_ptr u_gi_reflection_jitter;
        gfx::program::uniform_ptr s_refl_raw;
        gfx::program::uniform_ptr s_refl_history;
        gfx::program::uniform_ptr s_refl_depth;
        gfx::program::uniform_ptr s_refl_velocity;
        gfx::program::uniform_ptr s_refl_normal;
        /// View pre-exposure (pre_exposure.sh): corrects the history from last frame's scale.
        gfx::program::uniform_ptr u_pre_exposure;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
            cache_uniform(program.get(),
                          u_gi_refl_prev_view_proj,
                          "u_gi_refl_prev_view_proj",
                          bgfx::UniformType::Mat4);
            cache_uniform(program.get(), u_gi_refl_temporal, "u_gi_refl_temporal", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_refl_velocity, "u_gi_refl_velocity", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_camera, "u_gi_reflection_camera", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_gi_reflection_jitter, "u_gi_reflection_jitter", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_refl_raw, "s_refl_raw", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_refl_history, "s_refl_history", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_refl_depth, "s_refl_depth", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_refl_velocity, "s_refl_velocity", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_refl_normal, "s_refl_normal", bgfx::UniformType::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } temporal_program_;

    struct composite_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_refl_composite;
        gfx::program::uniform_ptr s_refl_acc;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_hiz;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_refl_composite, "u_gi_refl_composite", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_refl_acc, "s_refl_acc", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_hiz, "s_hiz", bgfx::UniformType::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } composite_program_;

    /// Composed-content epoch of the view's clipmap at the last run, and the frame it last
    /// advanced: the STRUCTURAL half of the temporal's stillness-release cap. The mover
    /// signal rides the velocity pass's draw loop and cannot see an instance destroyed
    /// while parked - it never draws again - but the composed field it vanishes from
    /// advances the epoch. ~0ull = not seen yet (pass birth stamps once; history is fresh
    /// then anyway).
    uint64_t content_epoch_seen_ = ~0ull;
    uint64_t content_changed_frame_ = ~0ull;
};

} // namespace unravel
