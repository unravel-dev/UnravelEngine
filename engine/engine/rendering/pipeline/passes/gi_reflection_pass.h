#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/gpu_program.h>

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
        /// Temporal window in frames for the stochastic ray; <= 1 bypasses the accumulation
        /// (raw passthrough) - the A/B knob for verifying the temporal is alive.
        int temporal_frames = gi::GI_REFLECTION_TEMPORAL_FRAMES;
        /// This frame's velocity buffer, passed explicitly by the pipeline. A valid texture
        /// IS the enable; null = legacy matrix reprojection of the receiver.
        gfx::texture::ptr velocity;
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

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_reflection_camera, "u_gi_reflection_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_reflection_jitter, "u_gi_reflection_jitter", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_light_voxel_params, "u_gi_light_voxel_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_probe_layer, "s_gi_probe_layer", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_hiz, "s_hiz", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_diffuse, "s_gi_diffuse", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_light_voxels, "s_light_voxels", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_env_sh, "s_gi_env_sh", gfx::uniform_type::Sampler);
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

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_reflection_camera, "u_gi_reflection_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_reflection_jitter, "u_gi_reflection_jitter", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_reflection_texel, "u_gi_reflection_texel", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_hiz, "s_hiz", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_diffuse, "s_gi_diffuse", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_env_sh, "s_gi_env_sh", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } classify_program_;

    struct reflection_args_program : uniforms_cache
    {
        gpu_program::ptr program;

        void cache_uniforms()
        {
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
        gfx::program::uniform_ptr s_gi_attr_albedo;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_reflection_camera, "u_gi_reflection_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_reflection_jitter, "u_gi_reflection_jitter", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_reflection_texel, "u_gi_reflection_texel", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_light_voxel_params, "u_gi_light_voxel_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_probe_layer, "s_gi_probe_layer", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_hiz, "s_hiz", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_diffuse, "s_gi_diffuse", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_light_voxels, "s_light_voxels", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_env_sh, "s_gi_env_sh", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_attr_albedo, "s_gi_attr_albedo", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } trace_program_;

    /// Compacted tracing-texel list: [0] append cursor (reset by args for the next frame),
    /// [1] staged trace count, [2+] packed coords. Raw uint indices - no typed-UAV floats.
    gfx::dynamic_index_buffer_handle refl_list_{bgfx::kInvalidHandle};
    uint32_t refl_list_capacity_{0};
    /// One entry: the trace launch, ceil(count / 64) groups folded into Y past the X limit.
    gfx::indirect_buffer_handle refl_args_{bgfx::kInvalidHandle};

    struct temporal_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_refl_prev_view_proj;
        gfx::program::uniform_ptr u_gi_refl_temporal;
        gfx::program::uniform_ptr u_gi_refl_velocity;
        gfx::program::uniform_ptr s_refl_raw;
        gfx::program::uniform_ptr s_refl_history;
        gfx::program::uniform_ptr s_refl_depth;
        gfx::program::uniform_ptr s_refl_velocity;

        void cache_uniforms()
        {
            cache_uniform(program.get(),
                          u_gi_refl_prev_view_proj,
                          "u_gi_refl_prev_view_proj",
                          gfx::uniform_type::Mat4);
            cache_uniform(program.get(), u_gi_refl_temporal, "u_gi_refl_temporal", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_refl_velocity, "u_gi_refl_velocity", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_refl_raw, "s_refl_raw", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_refl_history, "s_refl_history", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_refl_depth, "s_refl_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_refl_velocity, "s_refl_velocity", gfx::uniform_type::Sampler);
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
            cache_uniform(program.get(), u_gi_refl_composite, "u_gi_refl_composite", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_refl_acc, "s_refl_acc", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_hiz, "s_hiz", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } composite_program_;
};

} // namespace unravel
