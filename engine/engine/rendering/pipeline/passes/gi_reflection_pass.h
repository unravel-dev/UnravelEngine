#pragma once

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/gi_pass_common.h>
#include <engine/rendering/pipeline/passes/gi_run_params.h>

#include <graphics/render_view.h>
#include <graphics/texture.h>
#include <math/math.h>

#include <string>

namespace unravel
{

/**
 * @brief GI reflections, traced in software: one ray per pixel below the trace roughness, the Hi-Z screen trace and
 *        then the global distance field with surface cache hits, the ratio-estimator resolve, the dual-reprojection
 *        temporal and the variance-gated bilateral, composited into the reflection buffers.
 *
 * Runs after the GI gather, whose rough specular fills the untraced layer: the pass owns both reflection
 * buffers, in place of SSR and the reflection probes (no other specular is composited under the traced one). The
 * reflection quality (gi_settings::reflection_settings::quality) sets the rays the resolve reuses and, at its
 * lowest, traces one pixel of each 2 x 2 block (gi_pass_common.h). The constants are in
 * engine/rendering/gi/gi_pass_constants.h.
 *
 * run_front_layer traces the front see-through layer the same way with the passes' "front" programs
 * (gi_reflection_common.sh GI_REFLECTION_FRONT_LAYER): from the nearest surface of the translucency pass with GI
 * Reflections, over the screen box of those surfaces, into a target their "gi_reflect" variants read.
 */
class gi_reflection_pass
{
public:
    struct run_params
    {
        /// The gather's inputs this frame: the G-buffer, last frame's depth and scene colour, the Hi-Z, the
        /// environment SH, the camera, the pre-exposure, the global SDF clipmap, the surface cache and the
        /// settings (gi_settings::reflections).
        const gi_run_params* gather{};
        /// The gather's rough specular history (full resolution; rgb pre-exposed, before rough_specular_scale); null
        /// = none.
        gfx::texture::ptr rough_specular;
        /// The rough specular history's scale (the GI intensity).
        float rough_specular_scale = 1.0f;
        /// The traced and untraced reflection layers the indirect pass reads (RBUFFER and PBUFFER), RGBA16F and
        /// compute writable. The probe pass has drawn this frame's reflection probes into probe_output when the
        /// pass runs: its misses read them as the sky before the composite replaces them.
        gfx::texture::ptr traced_output;
        gfx::texture::ptr probe_output;
    };

    /// The trace texels and pixels a run covers: the whole view, or for the front layer its surfaces' box plus
    /// front_region_margin out to whole trace groups.
    struct dispatch_region
    {
        math::ivec2 trace_origin{};
        math::ivec2 trace_size{};
        math::ivec2 pixel_origin{};
        math::ivec2 pixel_size{};
    };

    /// The front layer's run (run_front_layer). Its targets cover the region only: the view's pixel_origin is their
    /// first texel, and they are at least target_size.
    struct front_layer_params
    {
        /// The gather's inputs this frame (as run_params::gather); the rays hit the opaque scene its G-buffer holds.
        const gi_run_params* gather{};
        /// The front layer: its device depth (1 where no such surface is) and its G-buffer target 1 layout
        /// (octahedral normal, facing, roughness; roughness 1 where no such surface is).
        gfx::texture::ptr depth;
        gfx::texture::ptr normal;
        /// Last frame's front layer depth over last frame's region, when the layer was drawn last frame; null = no
        /// history.
        gfx::texture::ptr prev_depth;
        /// The result, RGBA16F and compute writable: rgb = the traced reflection x its traced weight (pre-exposed),
        /// a = the front surface's distance from the camera where it traced (negative for a back face), 0 elsewhere.
        gfx::texture::ptr output;
        /// The pixels the passes run over (get_front_region).
        dispatch_region region;
        /// The size of the targets above and of the run's own (update_front_target_size).
        usize32_t target_size{};
    };

    /// The pixels around the front layer's surfaces the passes also cover: what the resolve and the spatial filter
    /// read around a pixel, and the temporal's border.
    static constexpr int32_t front_region_margin = 32;

    /// How the front layer's targets follow its region: they grow at once to the region rounded up to
    /// front_target_step pixels, and shrink to it once it has fit a smaller size for front_target_shrink_frames
    /// frames in a row, so a moving camera does not recreate them every frame.
    struct front_target_size
    {
        math::ivec2 size{};
        uint32_t smaller_frames{};
    };
    static constexpr int32_t front_target_step = 256;
    static constexpr uint32_t front_target_shrink_frames = 60;
    /// The front layer's targets' size this frame for a region of @p region_size pixels in a @p view_size view,
    /// advancing @p state.
    static auto update_front_target_size(front_target_size& state,
                                         const math::ivec2& region_size,
                                         const usize32_t& view_size) -> usize32_t;

    /// The render view's trace targets (gi_reflection_common.sh: the ray, the trace hit, the trace radiance).
    static constexpr const char* ray_texture = "GI_REFLECTION_RAY";
    static constexpr const char* hit_texture = "GI_REFLECTION_HIT";
    static constexpr const char* radiance_texture = "GI_REFLECTION_RADIANCE";
    /// This frame's reflection tiles (gi_reflection_common.sh): a skipped tile's trace texels hold no ray.
    static constexpr const char* tiles_texture = "GI_REFLECTION_TILES";

    /// What a reader of the trace targets above needs to date and size them.
    struct trace_state
    {
        static constexpr const char* view_key = "GI_REFLECTION_TRACE_STATE";

        /// The history ping-pong, whose last advance is also the frame the targets were traced on.
        gi_pass::ping_pong_state history;
        /// The trace downsample factor of that frame.
        uint32_t downsample{1};
        /// The pixels the run covered that frame: where its history targets hold it.
        math::ivec2 region_origin{};
        math::ivec2 region_size{};
    };

    /// The period of the noise sequences and of the downsampled traces' pixel rotation, in frames
    /// (gi_reflection_common.sh reads frame % this as u_gi_frame.y).
    static constexpr uint32_t state_frame_period = 8;

    /// The trace targets' size for a @p view_size view at the trace downsample factor @p downsample.
    static auto get_trace_size(const usize32_t& view_size, uint32_t downsample) -> usize32_t;

    /// Experiment toggles (surface_cache_system::get_experiment_flags), above the gather's bits: each one
    /// changes one stage for an in-session A/B. Zero in production.
    enum experiment : uint32_t
    {
        ///< The traces paint their type instead of radiance: screen hits red, distance-field hits green (yellow when
        ///< lit by last frame's scene colour), misses blue.
        experiment_show_trace_types = 1u << 16u,
    };

    auto init(rtti::context& ctx) -> bool;
    auto has_programs() const -> bool;
    /// The "front" programs run_front_layer needs loaded.
    auto has_front_programs() const -> bool;

    /// The roughness below which pixels trace reflection rays under @p settings (in [0, 1]); the traced weight
    /// fades to zero over GI_ROUGHNESS_FADE_LENGTH below it.
    static auto get_max_roughness_to_trace(const gi_settings::reflection_settings& settings) -> float;

    /**
     * @brief Traces, denoises and composites this frame's reflections into params.traced_output and
     *        params.probe_output.
     * @return False when the pass could not run; the reflection buffers are then left as they were.
     */
    auto run(gfx::render_view& rview, const run_params& params) -> bool;

    /**
     * @brief Traces, denoises and composites the front layer's reflections into params.output, after run() this frame.
     * @return False when the pass could not run; params.output is then left as it was.
     */
    auto run_front_layer(gfx::render_view& rview, const front_layer_params& params) -> bool;
    /// Releases the targets and the trace state run_front_layer keeps in @p rview.
    static void release_front_layer(gfx::render_view& rview);
    /// The front layer's dispatch region this frame for its surfaces' pixels [@p region_min, @p region_max), at the
    /// view size and trace downsample run() traced this frame.
    auto get_front_region(const math::ivec2& region_min, const math::ivec2& region_max) const -> dispatch_region;
    /// The pixels [min, max) the last successful run_front_layer wrote into its output, which holds them from min.
    auto get_front_written_min() const -> math::ivec2
    {
        return front_written_min_;
    }
    auto get_front_written_max() const -> math::ivec2
    {
        return front_written_max_;
    }

    /// The front layer's dispatch region for @p region_min, @p region_max in a @p view_size view at the trace downsample
    /// @p downsample.
    static auto get_front_dispatch_region(const math::ivec2& region_min,
                                          const math::ivec2& region_max,
                                          const usize32_t& view_size,
                                          uint32_t downsample) -> dispatch_region;

private:
    /// Every uniform of the reflection programs. bgfx uniforms are name-global, so one set serves all.
    struct uniforms : uniforms_cache
    {
        gfx::program::uniform_ptr u_gi_frame;
        gfx::program::uniform_ptr u_gi_view;
        gfx::program::uniform_ptr u_gi_reflection;
        gfx::program::uniform_ptr u_gi_reflection_quality;
        gfx::program::uniform_ptr u_gi_settings;
        gfx::program::uniform_ptr u_gi_prev_view_proj;
        gfx::program::uniform_ptr u_pre_exposure;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr s_gi_depth;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_gi_hiz;
        gfx::program::uniform_ptr s_gi_prev_color;
        gfx::program::uniform_ptr s_gi_prev_depth;
        gfx::program::uniform_ptr s_gi_env_sh;
        gfx::program::uniform_ptr s_gi_probe_layer;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_sdf_clipmap_coverage;
        gfx::program::uniform_ptr s_sdf_clipmap_mip;
        gfx::program::uniform_ptr s_gi_reflection_ray;
        gfx::program::uniform_ptr s_gi_reflection_radiance;
        gfx::program::uniform_ptr s_gi_reflection_hit;
        gfx::program::uniform_ptr s_gi_reflection_resolved;
        gfx::program::uniform_ptr s_gi_reflection_history;
        gfx::program::uniform_ptr s_gi_reflection_frames_history;
        gfx::program::uniform_ptr s_gi_reflection_specular;
        gfx::program::uniform_ptr s_gi_reflection_frames;
        gfx::program::uniform_ptr s_gi_rough_specular;
        gfx::program::uniform_ptr s_gi_reflection_tiles;
        gfx::program::uniform_ptr s_gi_reflection_tiles_history;
        /// The surface cache feedback (cs_gi_reflection_feedback.eshader): the tile jitter and the table's mask.
        gfx::program::uniform_ptr u_gi_feedback;
        /// The resolve's minimum neighbour weight (cs_gi_reflection_resolve.eshader).
        gfx::program::uniform_ptr u_gi_reflection_resolve;
        /// The front layer's dispatch region this frame and last frame (gi_reflection_common.sh).
        gfx::program::uniform_ptr u_gi_reflection_region;
        gfx::program::uniform_ptr u_gi_reflection_history_region;
        /// The opaque scene's depth of the front layer's distance-field stage.
        gfx::program::uniform_ptr s_gi_scene_depth;
        /// The velocity buffer for the screen trace and the temporal (gi_motion.sh).
        gi_pass::motion_uniforms motion;

        void cache_uniforms();
    } uniforms_;

    /// The programs of one layer: the default variants for the opaque scene, the "front" ones for the front layer (no
    /// surface cache feedback there).
    struct program_set
    {
        gpu_program::ptr screen;
        gpu_program::ptr feedback;
        gpu_program::ptr world;
        gpu_program::ptr resolve;
        gpu_program::ptr temporal;
        gpu_program::ptr spatial;

        /// Every program but the optional feedback is loaded.
        auto is_valid() const -> bool;
    };

    /// What one run traces from and writes to.
    struct layer_run
    {
        const gi_run_params* gather{};
        const program_set* programs{};
        /// Where the rays start: the device depth and the G-buffer target 1 layout.
        gfx::texture::ptr depth;
        gfx::texture::ptr normal;
        /// The opaque scene's depth, which on-screen hits test against.
        gfx::texture::ptr scene_depth;
        /// Last frame's depth of the surfaces traced from, which the temporal validates its history against.
        gfx::texture::ptr prev_depth;
        /// The velocity of the surfaces traced from; null = they reproject by the camera.
        gfx::texture::ptr velocity;
        gfx::texture::ptr rough_specular;
        float rough_specular_scale = 1.0f;
        gfx::texture::ptr probe_output;
        gfx::texture::ptr output;
        /// The render view's names of the run's history, tiles and trace state start with this. The trace targets (ray,
        /// radiance, hit) are the opaque run's: they are spent once its resolve has read them.
        std::string target_prefix;
        dispatch_region region;
        /// The size of the run's own targets (history, frame counts, tiles): the view's, or the front layer's.
        usize32_t target_size{};
        bool is_front{};
    };

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
        /// The reflection tiles: this frame's and last frame's (whose skipped tiles hold no history).
        gfx::texture::ptr tiles_write;
        gfx::texture::ptr tiles_read;
        /// The read halves hold last frame over [history_origin, history_origin + history_size).
        bool has_history{};
        math::ivec2 history_origin{};
        math::ivec2 history_size{};
    };

    /// The targets of @p layer: the traces at @p trace_size, the resolve at the view's @p size, the run's own at its
    /// target_size; no history after a @p camera_cut.
    static auto acquire_targets(gfx::render_view& rview,
                                const layer_run& layer,
                                const usize32_t& size,
                                const usize32_t& trace_size,
                                bool camera_cut) -> frame_targets;
    /// Whether last frame's read halves of @p targets hold its history for @p layer: at the view's size for the opaque
    /// run, over last frame's region for the front layer's.
    static auto holds_history(const frame_targets& targets, const layer_run& layer) -> bool;
    /// Runs every step of @p layer.
    void run_layer(gfx::render_view& rview, const layer_run& layer);
    /// Sets the per-frame uniforms every reflection program reads (gi_reflection_common.sh).
    void set_frame_uniforms(const layer_run& layer, const frame_targets& targets) const;
    void run_screen(const layer_run& layer, const frame_targets& targets) const;
    void run_world(const layer_run& layer, const frame_targets& targets) const;
    /// The surface cache feedback of the distance-field stage's hits (gi_surface_cache_feedback), when the run has
    /// one: the cards' hi-res pages its rays want.
    void run_feedback(const layer_run& layer, const frame_targets& targets) const;
    void run_resolve(const layer_run& layer, const frame_targets& targets) const;
    void run_temporal(const layer_run& layer, const frame_targets& targets) const;
    void run_spatial(const layer_run& layer, const frame_targets& targets) const;
    /// The groups of a per-trace-texel and of a per-pixel dispatch over @p layer's region.
    static auto get_trace_groups(const layer_run& layer) -> math::uvec2;
    static auto get_pixel_groups(const layer_run& layer) -> math::uvec2;
    /// Whether run_front_layer can trace @p params this frame: its programs, the gather's scene at the view size run()
    /// traced this frame, a region and every target at its target_size.
    auto can_run_front_layer(const front_layer_params& params) const -> bool;

    program_set programs_;
    program_set front_programs_;

    /// This frame's experiment toggles (enum experiment).
    uint64_t experiments_ = 0;
    /// The view size the per-pixel dispatches cover, and this frame's trace size and downsample factor.
    usize32_t view_size_{};
    usize32_t trace_size_{};
    uint32_t downsample_ = 1;
    /// See get_front_written_min.
    math::ivec2 front_written_min_{};
    math::ivec2 front_written_max_{};
};

} // namespace unravel
