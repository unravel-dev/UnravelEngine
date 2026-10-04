#pragma once

#include <engine/rendering/gi/lumen_constants.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/lumen_run_params.h>
#include <engine/rendering/pipeline/passes/lumen_adaptive_probes.h>
#include <engine/rendering/pipeline/passes/lumen_radiance_cache.h>
#include <engine/rendering/pipeline/passes/lumen_short_range_ao_pass.h>

#include <graphics/render_view.h>
#include <graphics/texture.h>

#include <array>

namespace unravel
{

/**
 * @brief The Lumen screen probe gather (UE 5.8 Lumen, software ray tracing, Global Tracing, Epic
 *        scalability): stateless uniform screen probes, plus adaptive probes where they cannot interpolate,
 *        traced every frame, composited with an absolute per-ray clamp, filtered in probe space, projected to
 *        SH3, integrated per pixel and accumulated by a per-pixel temporal (10 frames at the default update speed).
 *
 * The view's indirect diffuse: rgb = E / pi in the view's pre-exposed space, alpha = served. Rays hand the far field
 * to the radiance cache (lumen_radiance_cache) past a near field the probes trace themselves, and global distance
 * field hits read the surface cache (lumen_surface_cache_pass). The constants and their UE sources are in
 * engine/rendering/gi/lumen_constants.h; the plan and the measurements in tasks/lumen_transform.
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
    /// UE LumenVisualizeTraces.ush: the visualized probe's rays, one per texel of its tracing octahedron, each
    /// visualized_trace_stride vec4s (lumen_visualize.sh LUMEN_VISUALIZE_TRACE_STRIDE).
    static constexpr uint32_t visualized_trace_count =
        uint32_t(gi::lumen::LUMEN_PROBE_TRACE_RES * gi::lumen::LUMEN_PROBE_TRACE_RES);
    static constexpr uint32_t visualized_trace_stride = 3;

    lumen_gather_pass() = default;
    ~lumen_gather_pass();
    lumen_gather_pass(const lumen_gather_pass&) = delete;
    auto operator=(const lumen_gather_pass&) -> lumen_gather_pass& = delete;

    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Gathers indirect diffuse for every visible surface, and the short-range AO below the probe lattice.
     * @return The resolve texture (full resolution), or null when the pass could not run this frame.
     */
    auto run(gfx::render_view& rview, const lumen_run_params& params) -> gfx::texture::ptr;

    /// Whether the gather runs the short-range AO under @p settings: enabled with a positive intensity (UE
    /// UseShortRangeAmbientOcclusion).
    static auto uses_short_range_ao(const gi_settings::ambient_occlusion_settings& settings) -> bool;
    auto has_short_range_ao() const -> bool;

    /// The diffuse history the gather wrote for @p rview this frame (rgb = the result, a = the frames it accumulates,
    /// quantized to multiples of the maximum / 15: UE's 4-bit count); null when the gather did not run this frame.
    static auto get_current_history(gfx::render_view& rview) -> gfx::texture::ptr;

    /// The radiance cache this frame's gather updated, or null when it did not run.
    auto get_radiance_cache() const -> const lumen_radiance_cache*
    {
        return is_radiance_cache_ready_ ? &radiance_cache_ : nullptr;
    }

    /// The probe atlas a gather placed (UE r.Lumen.ScreenProbeGather.Debug.ProbePlacement draws it).
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

    /// The rays r.Lumen.ScreenProbeGather.VisualizeTraces draws (cs_lumen_probe_trace_visualize.sc), or an invalid
    /// handle before a frame recorded any.
    auto get_visualized_traces() const -> bgfx::DynamicVertexBufferHandle
    {
        return has_visualized_traces_ ? visualized_traces_ : bgfx::DynamicVertexBufferHandle{bgfx::kInvalidHandle};
    }

private:
    /// Every uniform of the gather's programs. bgfx uniforms are name-global, so one set serves all.
    struct uniforms : uniforms_cache
    {
        gfx::program::uniform_ptr u_lumen_frame;
        gfx::program::uniform_ptr u_lumen_probes;
        gfx::program::uniform_ptr u_lumen_view;
        gfx::program::uniform_ptr u_lumen_trace;
        gfx::program::uniform_ptr u_lumen_temporal;
        gfx::program::uniform_ptr u_lumen_prev_view_proj;
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
        gfx::program::uniform_ptr s_lumen_rc_depth;
        gfx::program::uniform_ptr u_lumen_options;
        gfx::program::uniform_ptr u_lumen_settings;
        gfx::program::uniform_ptr u_lumen_ray_gen;
        gfx::program::uniform_ptr u_lumen_prev_probe;
        gfx::program::uniform_ptr u_lumen_prev_inv_view_proj;
        gfx::program::uniform_ptr s_lumen_ray_info;
        gfx::program::uniform_ptr s_lumen_screen_data;
        gfx::program::uniform_ptr s_lumen_history_records;
        gfx::program::uniform_ptr s_lumen_history_radiance;
        gfx::program::uniform_ptr s_lumen_rough_history;
        gfx::program::uniform_ptr s_lumen_probe_border;
        gfx::program::uniform_ptr u_lumen_visualize_traces;

        void cache_uniforms();
    } uniforms_;

    /// The frame's placement and sizes, shared by every dispatch.
    struct frame_layout
    {
        usize32_t view_size{};
        uint32_t probes_x{};
        uint32_t probes_y{};
        /// The adaptive probes the atlas holds below the uniform rows (lumen_adaptive_probes::get_capacity).
        uint32_t adaptive_capacity{};
        /// Rows of the probe atlas: the uniform probes' and the adaptive capacity's.
        uint32_t atlas_rows{};
        std::array<float, 4> frame{};
        /// frame without the visualized traces' fixed jitter: the radiance cache and the short-range AO keep the
        /// view's frame (UE View.StateFrameIndex).
        std::array<float, 4> view_frame{};
        std::array<float, 4> probes{};
        std::array<float, 4> view{};
        /// xy = last frame's probe placement jitter in pixels.
        std::array<float, 4> prev_probe{};
    };

    /// The frame's probe textures, owned by the render view. Records and the filter's atlases alternate
    /// per frame: last frame's records and final filtered radiance are the importance sampler's history.
    struct probe_targets
    {
        gfx::texture::ptr records;
        gfx::texture::ptr trace_radiance;
        gfx::texture::ptr probe_radiance;
        std::array<gfx::texture::ptr, 2> filtered;
        gfx::texture::ptr sh;
        ///< The final filtered radiance with its octahedral border, bilinear (cs_lumen_probe_border.sc).
        gfx::texture::ptr probe_border;
        gfx::texture::ptr ray_info;
        gfx::texture::ptr screen_data;
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
    /// The frame's layout; @p is_jitter_fixed holds the placement and ray jitter at UE's fixed index while the traces
    /// are visualized.
    static auto make_frame_layout(const usize32_t& view_size, bool is_jitter_fixed) -> frame_layout;
    auto acquire_probe_targets(gfx::render_view& rview, const frame_layout& layout) const -> probe_targets;
    /// The filter atlas the last of LUMEN_FILTER_PASSES writes.
    static auto final_filter_index() -> size_t;
    auto acquire_history(gfx::render_view& rview, const lumen_run_params& params, const usize32_t& size)
        -> history_targets;

    /// Experiment toggles (surface_cache_system::get_experiment_flags, set by the editor's
    /// gi_set_experiment_flags MCP tool): each bit turns one stage of the gather off for an in-session A/B.
    /// Zero in production.
    enum experiment : uint32_t
    {
        experiment_no_spatial_filter = 1u << 0u,
        experiment_uniform_rays = 1u << 1u,
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
        ///< The resolve paints the pixels the uniform probes cannot interpolate (cs_lumen_integrate.sc).
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
    /// Structured importance sampling: each probe's 64 ray slots (cs_lumen_probe_generate_rays.sc).
    void run_generate_rays(const lumen_run_params& params,
                           const frame_layout& layout,
                           const probe_targets& targets,
                           const history_targets& history,
                           bool radiance_cache_ready);
    void run_trace(const lumen_run_params& params,
                   const frame_layout& layout,
                   const probe_targets& targets,
                   bool radiance_cache_ready);
    /// The trace programs' inputs: every stage but the output (5) and the uniforms.
    void bind_trace_inputs(const lumen_run_params& params,
                           const frame_layout& layout,
                           const probe_targets& targets,
                           bool radiance_cache_ready);
    /// UE ScreenProbeSetupVisualizeTraces: the rays of the probe params.visualize_traces shows, traced again.
    void run_visualize_traces(const lumen_run_params& params,
                              const frame_layout& layout,
                              const probe_targets& targets,
                              bool radiance_cache_ready);
    void run_composite(const frame_layout& layout, const probe_targets& targets);
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
    void run_integrate(const lumen_run_params& params,
                       const frame_layout& layout,
                       const probe_targets& targets,
                       const history_targets& history,
                       const gfx::texture::ptr& resolve,
                       const gfx::texture::ptr& rough_specular);
    /// The short-range AO over this frame's history taps, published as screen_ao_texture.
    void run_short_range_ao(gfx::render_view& rview,
                            const lumen_run_params& params,
                            const frame_layout& layout,
                            const history_targets& history);

    gpu_program::ptr place_program_;
    gpu_program::ptr generate_rays_program_;
    gpu_program::ptr trace_program_;
    gpu_program::ptr composite_program_;
    gpu_program::ptr filter_program_;
    gpu_program::ptr sh_program_;
    gpu_program::ptr border_program_;
    gpu_program::ptr integrate_program_;
    ///< cs_lumen_probe_trace_visualize.sc; the gather runs without it.
    gpu_program::ptr trace_visualize_program_;

    lumen_adaptive_probes adaptive_probes_;
    lumen_radiance_cache radiance_cache_;
    ///< The radiance cache updated this frame (see get_radiance_cache).
    bool is_radiance_cache_ready_ = false;
    lumen_short_range_ao_pass short_range_ao_;
    ///< See get_visualized_traces.
    bgfx::DynamicVertexBufferHandle visualized_traces_{bgfx::kInvalidHandle};
    bool has_visualized_traces_ = false;
    ///< See get_probe_placement.
    probe_placement probe_placement_{};

    /// This frame's experiment toggles (enum experiment).
    uint32_t experiments_ = 0;
    /// This frame's u_lumen_settings (lumen_pass::make_settings_uniform), set with the layout uniforms.
    std::array<float, 4> settings_uniform_{};
    /// Consecutive frames without a usable history; reported once when it persists.
    static constexpr uint32_t history_warning_frames = 120;
    uint32_t frames_without_history_ = 0;
};

} // namespace unravel
