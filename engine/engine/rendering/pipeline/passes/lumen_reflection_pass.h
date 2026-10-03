#pragma once

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/gi_resolve_pass.h>

#include <graphics/render_view.h>
#include <graphics/texture.h>

namespace unravel
{

/**
 * @brief Lumen reflections (UE 5.8 Lumen, software ray tracing, Global Tracing, Epic scalability): one ray per
 *        pixel below the trace roughness, the Hi-Z screen trace and then the global distance field with surface
 *        cache hits, the ratio-estimator resolve, the dual-reprojection temporal and the variance-gated bilateral,
 *        composited into the reflection buffers.
 *
 * Runs after the Lumen gather, whose rough specular fills the untraced layer: the pass owns both reflection
 * buffers, in place of SSR, the GI reflection tier and the reflection probes (UE composites no other specular
 * under Lumen's). The constants and their UE sources are in engine/rendering/gi/lumen_constants.h; the plan and
 * the measurements in tasks/lumen_transform.
 */
class lumen_reflection_pass
{
public:
    struct run_params
    {
        /// The gather's inputs this frame: the G-buffer, last frame's depth and scene colour, the Hi-Z, the
        /// environment SH, the camera, the pre-exposure, the global SDF clipmap and the surface cache.
        const gi_resolve_pass::run_params* gather{};
        /// The gather's rough specular (full resolution; rgb pre-exposed, a = 0 where it holds no estimate).
        gfx::texture::ptr rough_specular;
        /// The traced and untraced reflection layers the indirect pass reads (RBUFFER and PBUFFER), RGBA16F and
        /// compute writable. The probe pass has drawn this frame's reflection probes into probe_output when the
        /// pass runs: its misses read them as the sky before the composite replaces them.
        gfx::texture::ptr traced_output;
        gfx::texture::ptr probe_output;
    };

    /// Experiment toggles (surface_cache_system::get_experiment_flags), above the gather's bits: each one
    /// changes one stage for an in-session A/B. Zero in production.
    enum experiment : uint32_t
    {
        ///< The previous reflections (the GI reflection tier, SSR and the probes) instead of this pass.
        experiment_previous_reflections = 1u << 16u,
        ///< Rays skip the screen trace and start in the distance field.
        experiment_no_screen_traces = 1u << 17u,
        ///< Every roughness traces (UE r.Lumen.Reflections.MaxRoughnessToTrace 1).
        experiment_trace_all_roughness = 1u << 18u,
        ///< The traces paint their type instead of radiance (UE DEBUG_VISUALIZE_TRACE_TYPES): screen hits red,
        ///< distance-field hits green (yellow when lit by last frame's scene colour), misses blue.
        experiment_show_trace_types = 1u << 28u,
        ///< The rays rotate their noise over frames by a per-frame hash (BlueNoise2D) instead of the R2 sequence.
        experiment_reflection_hash_noise = 1u << 30u,
    };

    auto init(rtti::context& ctx) -> bool;
    auto has_programs() const -> bool;

    /// The roughness below which pixels trace reflection rays under @p experiments (UE
    /// r.Lumen.Reflections.MaxRoughnessToTrace); the traced weight fades to zero over LUMEN_ROUGHNESS_FADE_LENGTH below it.
    static auto get_max_roughness_to_trace(uint32_t experiments) -> float;

    /**
     * @brief Traces, denoises and composites this frame's reflections into params.traced_output and
     *        params.probe_output.
     * @return False when the pass could not run; the reflection buffers are then left as they were.
     */
    auto run(gfx::render_view& rview, const run_params& params) -> bool;

private:
    /// Every uniform of the reflection programs. bgfx uniforms are name-global, so one set serves all.
    struct uniforms : uniforms_cache
    {
        gfx::program::uniform_ptr u_lumen_frame;
        gfx::program::uniform_ptr u_lumen_view;
        gfx::program::uniform_ptr u_lumen_reflection;
        gfx::program::uniform_ptr u_lumen_prev_view_proj;
        gfx::program::uniform_ptr u_pre_exposure;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr s_lumen_depth;
        gfx::program::uniform_ptr s_lumen_normal;
        gfx::program::uniform_ptr s_lumen_hiz;
        gfx::program::uniform_ptr s_lumen_prev_color;
        gfx::program::uniform_ptr s_lumen_prev_depth;
        gfx::program::uniform_ptr s_lumen_env_sh;
        gfx::program::uniform_ptr s_lumen_probe_layer;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_sdf_clipmap_coverage;
        gfx::program::uniform_ptr s_lumen_reflection_ray;
        gfx::program::uniform_ptr s_lumen_reflection_radiance;
        gfx::program::uniform_ptr s_lumen_reflection_hit;
        gfx::program::uniform_ptr s_lumen_reflection_resolved;
        gfx::program::uniform_ptr s_lumen_reflection_history;
        gfx::program::uniform_ptr s_lumen_reflection_frames_history;
        gfx::program::uniform_ptr s_lumen_reflection_specular;
        gfx::program::uniform_ptr s_lumen_reflection_frames;
        gfx::program::uniform_ptr s_lumen_rough_specular;

        void cache_uniforms();
    } uniforms_;

    /// This frame's trace and denoiser targets.
    struct frame_targets
    {
        gfx::texture::ptr ray;
        gfx::texture::ptr radiance;
        gfx::texture::ptr hit;
        gfx::texture::ptr resolved;
        gfx::texture::ptr history_write;
        gfx::texture::ptr frames_write;
        gfx::texture::ptr history_read;
        gfx::texture::ptr frames_read;
        /// The read halves hold last frame at this size.
        bool has_history{};
    };

    static auto acquire_targets(gfx::render_view& rview, const usize32_t& size) -> frame_targets;
    /// Sets the per-frame uniforms every reflection program reads (lumen_reflection_common.sh).
    void set_frame_uniforms(const run_params& params, const frame_targets& targets) const;
    void run_screen(const run_params& params, const frame_targets& targets) const;
    void run_world(const run_params& params, const frame_targets& targets) const;
    void run_resolve(const run_params& params, const frame_targets& targets) const;
    void run_temporal(const run_params& params, const frame_targets& targets) const;
    void run_spatial(const run_params& params, const frame_targets& targets) const;

    gpu_program::ptr screen_program_;
    gpu_program::ptr world_program_;
    gpu_program::ptr resolve_program_;
    gpu_program::ptr temporal_program_;
    gpu_program::ptr spatial_program_;

    /// This frame's experiment toggles (enum experiment).
    uint32_t experiments_ = 0;
    /// The view size the dispatches cover.
    usize32_t view_size_{};
};

} // namespace unravel
