#pragma once

#include <engine/rendering/gi/lumen_constants.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/buffer_clear.h>
#include <engine/rendering/pipeline/passes/lumen_run_params.h>
#include <engine/rendering/pipeline/passes/lumen_adaptive_probes.h>
#include <engine/rendering/pipeline/passes/lumen_pass_common.h>
#include <engine/rendering/pipeline/passes/lumen_radiance_cache.h>
#include <engine/rendering/pipeline/passes/lumen_short_range_ao_pass.h>

#include <graphics/graphics.h>
#include <graphics/render_view.h>
#include <graphics/texture.h>

#include <array>

namespace unravel
{

/**
 * @brief The screen probe gather (software ray tracing through the screen and the global distance
 *        field): stateless uniform screen probes, plus adaptive probes where they cannot interpolate,
 *        traced every frame, composited with an absolute per-ray clamp, filtered in probe space, projected to
 *        SH3, integrated per pixel and accumulated by a per-pixel temporal (10 frames at the default update speed).
 *
 * The view's indirect diffuse: rgb = E / pi in the view's pre-exposed space, alpha = served. Rays hand the far field
 * to the radiance cache (lumen_radiance_cache) past a near field the probes trace themselves, and global distance
 * field hits read the surface cache (lumen_surface_cache_pass). The final gather quality
 * (gi_settings::diffuse_settings::quality) sets the rays per probe, the probe spacing and the full-resolution jitter
 * (lumen_pass_common.h). The constants are in engine/rendering/gi/lumen_constants.h; the plan and the measurements
 * in tasks/lumen_transform.
 */
class lumen_gather_pass
{
public:
    /// The render view's screen AO the gather publishes for the lighting composite (lumen_short_range_ao_pass), the
    /// render frame it was produced on (a texture from an older frame is stale) and the intensity the composite
    /// applies it with (gi_settings::ambient_occlusion_settings::intensity).
    static constexpr const char* screen_ao_texture = "LUMEN_SCREEN_AO";
    static constexpr const char* screen_ao_frame = "LUMEN_SCREEN_AO_FRAME";
    static constexpr const char* screen_ao_intensity = "LUMEN_SCREEN_AO_INTENSITY";
    /// The visualized probe's rays, one per texel of its tracing octahedron (at most the
    /// largest tracing resolution's), each visualized_trace_stride vec4s (lumen_visualize.sh
    /// LUMEN_VISUALIZE_TRACE_STRIDE).
    static constexpr uint32_t max_visualized_trace_count =
        lumen_pass::max_probe_trace_resolution * lumen_pass::max_probe_trace_resolution;
    static constexpr uint32_t visualized_trace_stride = 3;

    lumen_gather_pass() = default;
    ~lumen_gather_pass();
    lumen_gather_pass(const lumen_gather_pass&) = delete;
    auto operator=(const lumen_gather_pass&) -> lumen_gather_pass& = delete;

    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Gathers indirect diffuse for every visible surface, and the short-range AO below the probe lattice.
     * @return The diffuse history this frame wrote (full resolution; rgb = E / pi pre-exposed, to be scaled by the GI
     *         intensity; zero on the sky), or null when the pass could not run this frame. Its rough specular is
     *         published as GI_ROUGH_SPECULAR (same scale).
     */
    auto run(gfx::render_view& rview, const lumen_run_params& params) -> gfx::texture::ptr;

    /// Whether the gather runs the short-range AO under @p settings: enabled with a positive intensity.
    static auto uses_short_range_ao(const gi_settings::ambient_occlusion_settings& settings) -> bool;
    auto has_short_range_ao() const -> bool;

    /// Frees what the gather keeps across views (the radiance cache) when GI turns off for its camera; the per-view
    /// targets go with the render view's idle collection.
    void release_resources()
    {
        radiance_cache_.release_resources();
        // Its buffers are gone: no reader binds them until a gather updates the cache again.
        is_radiance_cache_ready_ = false;
    }

    /// The diffuse history the gather wrote for @p rview this frame (rgb = the result, a = the frames it accumulates,
    /// quantized to multiples of the maximum / 15: a 4-bit count); null when the gather did not run this frame.
    static auto get_current_history(gfx::render_view& rview) -> gfx::texture::ptr;

    /// The radiance cache this frame's gather updated, or null when it did not run this frame (GI off releases the
    /// cache's buffers).
    auto get_radiance_cache() const -> const lumen_radiance_cache*
    {
        const bool is_current = is_radiance_cache_ready_ && radiance_cache_frame_ == gfx::get_render_frame();
        return is_current ? &radiance_cache_ : nullptr;
    }

    /// The probe atlas a gather placed (lumen_visualize_pass::draw_probe_placement draws it).
    struct probe_placement
    {
        ///< The probe records (lumen_common.sh LumenPackProbe), one texel per atlas tile.
        gfx::texture::ptr records;
        ///< The layout uniforms the records unpack with (lumen_common.sh u_lumen_frame, u_lumen_probes, u_lumen_view).
        std::array<float, 4> frame{};
        std::array<float, 4> probes{};
        std::array<float, 4> view{};
        ///< The adaptive probes the atlas holds (lumen_adaptive_probes::get_capacity).
        uint32_t adaptive_capacity = 0;
        ///< The render frame of the placement.
        uint32_t render_frame = 0;
    };

    /// The placement of the last frame the gather ran.
    auto get_probe_placement() const -> const probe_placement&
    {
        return probe_placement_;
    }

    /// Binds the placement's adaptive probe state (lumen_adaptive_probes.sh) at @p stage, read only.
    void bind_adaptive_state(uint8_t stage) const
    {
        adaptive_probes_.bind_state(stage, bgfx::Access::Read);
    }

    /// The rays the screen probe trace visualization draws (cs_lumen_probe_trace_visualize.sc), or an invalid
    /// handle before a frame recorded any.
    auto get_visualized_traces() const -> bgfx::DynamicVertexBufferHandle
    {
        return has_visualized_traces_ ? visualized_traces_ : bgfx::DynamicVertexBufferHandle{bgfx::kInvalidHandle};
    }

    /// The rays get_visualized_traces holds: one per texel of the probe at the resolution it was traced at.
    auto get_visualized_trace_count() const -> uint32_t
    {
        return visualized_trace_resolution_ * visualized_trace_resolution_;
    }

private:
    /// The programs whose layout depends on the probes' tracing resolution, compiled once per resolution (thread group
    /// size permutations: cs_lumen_*.sc at 8 x 8 rays, the _res4 / _res16 wrappers at 4 x 4 / 16 x 16).
    struct probe_programs
    {
        gpu_program::ptr generate_rays;
        ///< The trace's screen pass (cs_lumen_probe_trace.sc) and its far-field pass over the rays the screen left
        ///< (cs_lumen_probe_trace_far_field.sc), then its hit pass over the same rays
        ///< (cs_lumen_probe_trace_hit_shade.sc).
        gpu_program::ptr trace;
        gpu_program::ptr trace_far_field;
        gpu_program::ptr trace_hit_shade;
        gpu_program::ptr composite;
        gpu_program::ptr filter;
        gpu_program::ptr sh;
        gpu_program::ptr border;
        gpu_program::ptr integrate;
        ///< cs_lumen_integrate_ao.sc: the integrate with the short-range AO's accumulation.
        gpu_program::ptr integrate_short_range_ao;
        ///< cs_lumen_probe_trace_visualize.sc; the gather runs without it.
        gpu_program::ptr trace_visualize;

        auto is_valid() const -> bool;
    };

    /// The program set of tracing resolution @p trace_resolution (4, 8 or 16).
    auto get_probe_programs(uint32_t trace_resolution) const -> const probe_programs&;

    /// Every uniform of the gather's programs. bgfx uniforms are name-global, so one set serves all.
    struct uniforms : uniforms_cache
    {
        gfx::program::uniform_ptr u_lumen_frame;
        gfx::program::uniform_ptr u_lumen_probes;
        gfx::program::uniform_ptr u_lumen_view;
        gfx::program::uniform_ptr u_lumen_trace;
        gfx::program::uniform_ptr u_lumen_temporal;
        gfx::program::uniform_ptr u_pre_exposure;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_lumen_hit_lighting;
        gfx::program::uniform_ptr s_lumen_depth;
        gfx::program::uniform_ptr s_lumen_normal;
        gfx::program::uniform_ptr s_lumen_probe_records;
        gfx::program::uniform_ptr s_lumen_hiz;
        gfx::program::uniform_ptr s_lumen_prev_color;
        gfx::program::uniform_ptr s_lumen_prev_depth;
        gfx::program::uniform_ptr s_lumen_env_sh;
        gfx::program::uniform_ptr s_lumen_trace_radiance;
        gfx::program::uniform_ptr s_lumen_probe_radiance;
        gfx::program::uniform_ptr s_lumen_probe_filtered;
        gfx::program::uniform_ptr s_lumen_probe_sh;
        gfx::program::uniform_ptr s_lumen_history;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_sdf_clipmap_coverage;
        gfx::program::uniform_ptr s_lumen_rc_final;
        gfx::program::uniform_ptr s_sdf_clipmap_mip;
        gfx::program::uniform_ptr u_lumen_options;
        gfx::program::uniform_ptr u_lumen_settings;
        gfx::program::uniform_ptr u_lumen_ray_gen;
        gfx::program::uniform_ptr u_lumen_prev_probe;
        gfx::program::uniform_ptr s_lumen_ray_info;
        gfx::program::uniform_ptr s_lumen_screen_data;
        gfx::program::uniform_ptr s_lumen_history_records;
        gfx::program::uniform_ptr s_lumen_history_radiance;
        gfx::program::uniform_ptr s_lumen_rough_history;
        gfx::program::uniform_ptr s_lumen_probe_border;
        gfx::program::uniform_ptr u_lumen_visualize_traces;
        gfx::program::uniform_ptr s_lumen_probe_moving;
        /// Last frame's view projection and the velocity buffer (lumen_motion.sh).
        lumen_pass::motion_uniforms motion;

        void cache_uniforms();
    } uniforms_;

    /// The frame's placement and sizes, shared by every dispatch.
    struct frame_layout
    {
        usize32_t view_size{};
        /// Rays per axis of a probe (lumen_pass::get_probe_trace_resolution) and the probe spacing in pixels.
        uint32_t trace_resolution{};
        uint32_t downsample{};
        uint32_t probes_x{};
        uint32_t probes_y{};
        /// The adaptive probes the atlas holds below the uniform rows (lumen_adaptive_probes::get_capacity).
        uint32_t adaptive_capacity{};
        /// Rows of the probe atlas: the uniform probes' and the adaptive capacity's.
        uint32_t atlas_rows{};
        std::array<float, 4> frame{};
        /// frame without the visualized traces' fixed jitter: the radiance cache and the short-range AO keep the
        /// view's own frame index.
        std::array<float, 4> view_frame{};
        std::array<float, 4> probes{};
        std::array<float, 4> view{};
        /// xy = last frame's probe placement jitter in pixels.
        std::array<float, 4> prev_probe{};
        /// The integrate draws one probe per pixel (the tier's, then the experiment toggle).
        bool is_interpolation_stochastic{};
    };

    /// The frame's probe textures, owned by the render view. Records and the filtered radiance alternate per frame:
    /// last frame's records and filtered radiance are the importance sampler's history. The filter passes before the
    /// last alternate with the trace radiance, which the composite has consumed.
    struct probe_targets
    {
        gfx::texture::ptr records;
        gfx::texture::ptr trace_radiance;
        gfx::texture::ptr probe_radiance;
        gfx::texture::ptr filtered;
        gfx::texture::ptr sh;
        ///< The final filtered radiance with its octahedral border, bilinear (cs_lumen_probe_border.sc).
        gfx::texture::ptr probe_border;
        gfx::texture::ptr ray_info;
        gfx::texture::ptr screen_data;
        ///< One texel per probe: the fraction of its rays that hit a moving surface (cs_lumen_probe_composite.sc).
        gfx::texture::ptr probe_moving;
        gfx::texture::ptr history_records;
        gfx::texture::ptr history_radiance;
        /// Last frame wrote the history textures with this frame's layout.
        bool has_probe_history{};
    };

    /// The temporal history pairs of this frame (diffuse, rough specular) and whether the read halves
    /// continue last frame.
    struct history_targets
    {
        gfx::texture::ptr read;
        gfx::texture::ptr write;
        gfx::texture::ptr rough_read;
        gfx::texture::ptr rough_write;
        bool has_history{};
    };

    auto has_programs() const -> bool;
    /// The frame's layout at the final gather quality @p quality; @p is_jitter_fixed holds the placement and ray jitter
    /// at a fixed index while the traces are visualized.
    static auto make_frame_layout(const usize32_t& view_size,
                                  float quality,
                                  gi_project_settings::quality_level tier,
                                  bool is_jitter_fixed) -> frame_layout;
    auto acquire_probe_targets(gfx::render_view& rview, const frame_layout& layout) const -> probe_targets;
    auto acquire_history(gfx::render_view& rview, const lumen_run_params& params, const usize32_t& size)
        -> history_targets;

    /// Experiment toggles (surface_cache_system::get_experiment_flags, set by the editor's
    /// gi_set_experiment_flags MCP tool): each bit turns one stage of the gather off for an in-session A/B.
    /// Zero in production.
    enum experiment : uint32_t
    {
        experiment_no_spatial_filter = 1u << 0u,
        experiment_uniform_rays = 1u << 1u,
        ///< The composite re-bins with a second slice of the ray jitter (a constant offset of it) instead of
        ///< LumenProbeRebinDither.
        experiment_jitter_slice_dither = 1u << 3u,
        ///< Rays reaching the distance-field stage paint (start / near field, hit, 0) instead of radiance.
        experiment_show_sdf_start = 1u << 7u,
        experiment_no_radiance_cache = 1u << 2u,
        experiment_no_full_res_jitter = 1u << 4u,
        experiment_reject_near_screen_hits = 1u << 5u,
        experiment_voxel_screen_hits = 1u << 6u,
        experiment_skip_near_field = 1u << 8u,
        experiment_show_sdf_bias = 1u << 9u,
        experiment_no_rough_specular = 1u << 11u,
        experiment_rejected_hits_vouch_nothing = 1u << 12u,
        experiment_show_ray_sources = 1u << 13u,
        ///< Keep one tracing stage's radiance: low = screen, high = distance field, both = radiance cache.
        experiment_keep_stage_low = 1u << 14u,
        experiment_keep_stage_high = 1u << 15u,
        ///< The diffuse paints the pixels the uniform probes cannot interpolate (cs_lumen_integrate.sc).
        experiment_show_interpolation_fallback = 1u << 22u,
        ///< The short-range AO's noise rotates by a per-frame hash instead of the R2 sequence over frames.
        experiment_short_range_ao_hash_noise = 1u << 23u,
        ///< No adaptive probes: the uniform probes alone (lumen_adaptive_probes).
        experiment_no_adaptive_probes = 1u << 24u,
        ///< Every frame starts without history: no probe history, no per-pixel history (diagnostic).
        experiment_no_history = 1u << 29u,
    };

    /// Sets the per-frame layout uniforms every gather program reads (lumen_common.sh).
    void set_layout_uniforms(const frame_layout& layout);
    void run_place(const lumen_run_params& params, const frame_layout& layout, const probe_targets& targets);
    /// The adaptive probes below the uniform ones and this frame's per-probe dispatch arguments.
    void run_adaptive_probes(const lumen_run_params& params,
                             const frame_layout& layout,
                             const probe_targets& targets);
    /// Structured importance sampling: each probe's ray slots (cs_lumen_probe_generate_rays.sc).
    void run_generate_rays(const lumen_run_params& params,
                           const frame_layout& layout,
                           const probe_targets& targets,
                           const history_targets& history,
                           bool radiance_cache_ready);
    void run_trace(const lumen_run_params& params,
                   const frame_layout& layout,
                   const probe_targets& targets,
                   bool radiance_cache_ready);
    /// The rays the trace's screen pass can leave to its far-field pass this frame (cs_lumen_probe_trace.sc
    /// b_lumen_trace_rays: a count, then trace_ray_stride uints per ray) and the far-field hits it leaves to the hit
    /// pass (b_lumen_trace_hits: a count, then one ray slot per hit), grown to @p rays when smaller.
    void ensure_trace_rays(uint32_t rays);
    /// Writes the indirect arguments of a pass with one thread per entry of @p rays (its first uint counts them).
    void dispatch_trace_args(uint16_t view_id,
                             bgfx::DynamicIndexBufferHandle rays,
                             bgfx::IndirectBufferHandle args) const;
    /// The trace programs' inputs: every stage but the output (5) and the uniforms.
    void bind_trace_inputs(const lumen_run_params& params,
                           const frame_layout& layout,
                           const probe_targets& targets,
                           bool radiance_cache_ready);
    /// The rays of the probe params.visualize_traces shows, traced again.
    void run_visualize_traces(const lumen_run_params& params,
                              const frame_layout& layout,
                              const probe_targets& targets,
                              bool radiance_cache_ready);
    void run_composite(const frame_layout& layout, const probe_targets& targets);
    /// Binds the velocity buffer at @p velocity_stage with last frame's view projection (lumen_motion.sh).
    void bind_motion(const lumen_run_params& params, uint8_t velocity_stage) const;
    /// Binds the radiance cache for the hand-off read at stages 7-9, or neutral textures without it.
    void bind_radiance_cache(bool radiance_cache_ready) const;
    /// Binds the surface cache for global-SDF hits at stages 13-15 (hits shade black without it).
    void bind_surface_cache(const lumen_run_params& params) const;
    /// The spatial filter passes; returns the atlas the last pass wrote.
    auto run_filter(const lumen_run_params& params, const frame_layout& layout, const probe_targets& targets)
        -> gfx::texture::ptr;
    void run_sh(const frame_layout& layout, const probe_targets& targets, const gfx::texture::ptr& filtered);
    /// The final filtered radiance with its octahedral border, for the rough specular's bilinear lookups.
    void run_border(const frame_layout& layout, const probe_targets& targets, const gfx::texture::ptr& filtered);
    /// The per-pixel integrate and temporal; with @p short_range_ao's search, the AO accumulates beside the diffuse.
    void run_integrate(const lumen_run_params& params,
                       const frame_layout& layout,
                       const probe_targets& targets,
                       const history_targets& history,
                       const lumen_short_range_ao_pass::run_params& short_range_ao_params,
                       const lumen_short_range_ao_pass::frame_targets& short_range_ao);
    /// The short-range AO pass's inputs this frame.
    auto make_short_range_ao_params(const lumen_run_params& params, const frame_layout& layout) const
        -> lumen_short_range_ao_pass::run_params;
    /// Publishes this frame's screen AO (screen_ao_texture) for the lighting composite.
    static void publish_short_range_ao(gfx::render_view& rview,
                                       const lumen_run_params& params,
                                       const lumen_short_range_ao_pass::frame_targets& short_range_ao);

    gpu_program::ptr place_program_;
    /// cs_lumen_trace_far_field_args.sc: the far-field and hit passes' dispatch arguments.
    gpu_program::ptr far_field_args_program_;
    /// See ensure_trace_rays, and the far-field and hit passes' indirect dispatch arguments.
    bgfx::DynamicIndexBufferHandle trace_rays_{bgfx::kInvalidHandle};
    bgfx::DynamicIndexBufferHandle trace_hits_{bgfx::kInvalidHandle};
    uint32_t trace_rays_capacity_ = 0;
    bgfx::IndirectBufferHandle far_field_args_{bgfx::kInvalidHandle};
    bgfx::IndirectBufferHandle hit_args_{bgfx::kInvalidHandle};
    /// Zeroes the ray and hit counts ahead of the passes that append to them (the GPU writes those buffers, so no CPU
    /// update may).
    buffer_clear ray_count_clear_;
    /// One program set per tracing resolution, 4 x 4, 8 x 8 and 16 x 16 rays (get_probe_programs).
    std::array<probe_programs, 3> probe_programs_;
    /// This frame's set (run).
    const probe_programs* programs_ = nullptr;

    lumen_adaptive_probes adaptive_probes_;
    lumen_radiance_cache radiance_cache_;
    ///< The radiance cache was updated on the render frame radiance_cache_frame_ (see get_radiance_cache).
    bool is_radiance_cache_ready_ = false;
    uint32_t radiance_cache_frame_ = 0;
    lumen_short_range_ao_pass short_range_ao_;
    ///< See get_visualized_traces and get_visualized_trace_count.
    bgfx::DynamicVertexBufferHandle visualized_traces_{bgfx::kInvalidHandle};
    bool has_visualized_traces_ = false;
    uint32_t visualized_trace_resolution_ = 0;
    ///< See get_probe_placement.
    probe_placement probe_placement_{};

    /// This frame's experiment toggles (enum experiment).
    uint64_t experiments_ = 0;
    /// This frame drops every gather history (a camera cut or a global lighting change).
    bool starts_history_over_ = false;
    /// A view above LUMEN_PROBE_MAX_VIEW_EXTENT has been reported (once per pass).
    bool has_logged_view_too_large_ = false;
    /// This frame's u_lumen_settings (lumen_pass::make_settings_uniform), set with the layout uniforms.
    std::array<float, 4> settings_uniform_{};
    /// Consecutive frames without a usable history; reported once when it persists.
    static constexpr uint32_t history_warning_frames = 120;
    uint32_t frames_without_history_ = 0;
};

} // namespace unravel
