#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/global_sdf_clipmap.h>
#include <engine/rendering/gi/global_sdf_clipmap_gpu.h>

#include <math/math.h>

#include <array>
#include <vector>

namespace unravel
{

/**
 * @brief The per-CAMERA half of the surface cache.
 *
 * Everything in @ref surface_cache_system is a function of the WORLD -- which meshes are resident,
 * where their instances are, which lights exist -- so one copy serves every camera. The cascade is
 * not: it is four levels snapped around a viewer, so it is a function of the camera as much as of
 * the scene.
 *
 * Keeping it on the service made two cameras fight over one cascade. Each pipeline run re-snapped
 * the origins to its own position, every level read as stale, and the budgeted recomposition
 * rebuilt one level per run forever without ever settling -- so each camera spent half its frames
 * tracing a cascade centred on the other one. Nothing errored; the cascade was simply always out of
 * date and always being rebuilt.
 *
 * Lives in @c gfx::render_view::data() alongside the other per-view state, rather than in the
 * render view proper, because the graphics library has no business knowing what a cascade is.
 */
class surface_cache_view
{
public:
    /// Name this is stored under in @c gfx::render_view::data().
    static constexpr const char* view_key = "GI_SURFACE_CACHE_VIEW";

    /// One completed relight-convergence readback (gi_light_voxel_pass::get_relight_sample).
    struct relight_sample
    {
        /// Increments per completed readback; 0 = none yet, or no statistic on this backend.
        uint64_t index = 0;
        /// Mean relative change per relit face (see GI_QUIESCENCE_LUMINANCE_FLOOR).
        float mean_change = 0.0f;
    };

    /// How much of the gate decision the CPU settled on its own. The convergence half of the
    /// test needs a statistic the GPU produces, and the GPU-resident gate
    /// (gi_quiescence_gate_pass) evaluates it without a readback - so update_quiescence
    /// reports which of the two answered, rather than only its own verdict.
    enum class quiescence_mode : uint8_t
    {
        /// A tracked input changed, the settle floor has not been reached, or a writer-side
        /// debug view is up: the passes must run whatever the relight is doing.
        run,
        /// Every CPU-known condition is satisfied; convergence alone decides.
        measure,
        /// GI_QUIESCENCE_MAX_FRAMES reached: skip regardless of convergence.
        skip,
    };

    /// The full result of update_quiescence: @ref quiescent is the CPU path's own answer
    /// (unchanged semantics, used when no GPU gate is available), @ref mode is what the
    /// GPU-resident gate needs to combine with its measured verdict.
    struct quiescence_verdict
    {
        bool quiescent = false;
        quiescence_mode mode = quiescence_mode::run;
        /// A tracked input changed THIS frame, so the GPU gate's sample ring is stale and
        /// must be cleared along with the CPU's.
        bool changed = false;
    };

    /**
     * @brief Recomposes the stale levels around @p camera_position and uploads them.
     *
     * @param instances Every resident field placement in the world, NOT only the visible ones --
     *        geometry behind the camera still bounces light.
     */
    /// @param clipmap_settings Authored per volume through gi_component, and applied every update so
    ///        a knob moved in the inspector takes effect without a restart. Passed rather than stored
    ///        because the cascade is downstream of the volume blend, which only the pipeline sees.
    ///        @c compose_on_gpu is additionally gated on the compute program having loaded, so a
    ///        scene asking for GPU composition on a backend that cannot provide it still composes.
    void update(const std::vector<global_sdf_instance>& instances,
                const math::vec3& camera_position,
                const global_sdf_clipmap::settings& clipmap_settings,
                uint64_t instances_revision = 0);

    auto get_clipmap() const -> const global_sdf_clipmap&
    {
        return clipmap_;
    }

    /// Non-const access for the compose pass, which consumes the dirty mask it composed.
    auto get_clipmap_mutable() -> global_sdf_clipmap&
    {
        return clipmap_;
    }

    auto get_clipmap_gpu() const -> const global_sdf_clipmap_gpu&
    {
        return clipmap_gpu_;
    }

    /// Non-const access for the compose pass, which consumes the one-time buffer-seed request.
    auto get_clipmap_gpu_mutable() -> global_sdf_clipmap_gpu&
    {
        return clipmap_gpu_;
    }

    /**
     * @brief Logs a line per composed level: instance counts, cull occupancy, and how many
     *        candidates survive to a field sample.
     *
     * Off by default -- composition runs several times a second while the camera moves, so this is
     * far too noisy to leave on. Kept because the shape of composition work depends entirely on how
     * a particular scene's instances are distributed, which no synthetic fixture reproduces; these
     * numbers are what distinguish "too many candidates per cell" from "the cheap reject is
     * failing" from "the per-sample cost is wrong".
     */
    void set_log_composition_stats(bool enabled)
    {
        log_composition_stats_ = enabled;
    }

    /**
     * @brief Whether every input of the light-voxel and world-probe passes has been still
     *        long enough, and the relight has provably converged, so that re-running them
     *        would rewrite values no reader can distinguish.
     *
     * The world side is a fixed point when nothing changes: the probe trace rewrites the same
     * stratum values forever (the windowed mean's zero-steady-state-variance property), the
     * convolve re-integrates an unchanged atlas, and the light voxels re-light unchanged
     * content. This tracks the full input set - light-buffer hash, clipmap content epoch,
     * every level's composed origin, and every level's probe-window cell - and any change
     * resets the gate; the passes resume the same frame.
     *
     * CONVERGENCE, MEASURED. Stillness of the inputs is not convergence of the volume: the
     * relight folds each visit into a per-voxel EMA and the closed-room bounce loop stretches
     * its tail, so a fixed settle count froze a sealed room mid-decay at whatever residual it
     * had reached (a room that read lit and stayed lit). The light-voxel pass now reads back
     * the mean relative change per relit face (@p relight); the gate opens when that mean is
     * below GI_QUIESCENCE_CONVERGED_MEAN, or has stopped falling (a stationary dithered
     * equilibrium: GI_QUIESCENCE_STATIONARY_FRACTION), never before GI_QUIESCENCE_MIN_FRAMES
     * and always by GI_QUIESCENCE_MAX_FRAMES. Without the statistic (index 0) the fixed
     * @ref quiescence_settle_frames remains.
     *
     * @param wants_debug A writer-side SDF debug view is up: those views paint per frame
     *        through these very dispatches, so the gate is held open.
     *
     * @return Both halves of the decision - see @ref quiescence_verdict. The returned
     *         @c quiescent is the answer for the readback path; a GPU-resident gate reads
     *         @c mode instead and supplies the convergence half itself.
     */
    auto update_quiescence(uint64_t light_hash,
                           uint64_t environment_hash,
                           const math::vec3& camera_position,
                           const relight_sample& relight,
                           bool wants_debug) -> quiescence_verdict;

    /// Frames the full quiescence input set (light hash, environment revision, content epoch,
    /// window origins, probe cells) has held unchanged - 0 on any change.
    auto get_quiet_frames() const -> uint32_t
    {
        return quiescence_frames_;
    }

    /// Frames since the LIGHT SET changed (the light-buffer hash). Content changes - an
    /// instance moved, appeared, vanished, changed material - are deliberately NOT in this
    /// signal any more: they are region-local and carried by
    /// surface_cache_system::get_dirty_bounds, which the temporal tests per pixel. A light
    /// change is genuinely global (a sun or a room light reaches everything), so the temporal
    /// accumulators key their screen-wide fast-flush window off this alone: a camera pan keeps
    /// full temporal depth, a light edit drops to the fast caps until the stale energy has
    /// provably washed out (quiescence_settle_frames of fast-rate blending).
    auto get_lighting_quiet_frames() const -> uint32_t
    {
        return lighting_quiet_frames_;
    }

    /// The FALLBACK settle when no convergence statistic is available (update_quiescence):
    /// sixteen complete probe windows (GI_WORLD_PROBE_WINDOW frames each). The bounce
    /// FEEDBACK settles within one window, but the light-voxel relight converges by EMA
    /// (GI_LIGHT_VOXEL_EMA_BLEND 0.125, one visit per 4-frame rotation): after the last
    /// content change a voxel still holds 0.875^(frames/4) of its stale radiance. The old
    /// 64-frame settle froze that tail at ~13% - invisible on flat surfaces, but a departed
    /// emitter's residual stayed a visible line wherever reflections amplify (measured:
    /// the red edge lines after emissive movers passed). 256 frames leaves ~0.02%, below
    /// perception at any amplification the reflection path can apply. Camera-driven churn
    /// resets the counter anyway, so the cost is only ~3 extra seconds of GI passes after
    /// an edit in an otherwise parked shot. See update_quiescence.
    static constexpr uint32_t quiescence_settle_frames = 16u * 16u;
    static_assert(quiescence_settle_frames >= 32u, "must cover at least two probe windows");

private:
    /// The convergence half of update_quiescence: the sample ring's two tests, plus the
    /// frame-count floor and ceiling. Split out so the mode the GPU gate reads is settled
    /// before any statistic is consulted. Mirrored by cs_gi_quiescence_gate.sc.
    auto evaluate_relight_quiescence(const relight_sample& relight) const -> bool;

    global_sdf_clipmap clipmap_;
    global_sdf_clipmap_gpu clipmap_gpu_;
    /// Deferred to the first update so that constructing a view costs nothing. A camera that never
    /// enables GI never allocates the cascade texture.
    bool initialized_ = false;
    bool log_composition_stats_ = false;
    /// update_quiescence state: the last-seen input set and how long it has held.
    uint64_t quiescence_light_hash_ = 0;
    /// The environment radiance revision (deferred::irradiance_pass_result::environment_hash).
    /// Treated exactly like the light hash: the world probes integrate the environment SH on
    /// every sky miss, so a sky edit stales the atlas globally.
    uint64_t quiescence_environment_hash_ = 0;
    uint64_t quiescence_content_epoch_ = 0;
    std::array<math::vec3, global_sdf_clipmap::level_count> quiescence_origins_{};
    std::array<math::ivec3, global_sdf_clipmap::level_count> quiescence_probe_cells_{};
    uint32_t quiescence_frames_ = 0;
    /// See get_lighting_quiet_frames; saturates so it never wraps back into "recent".
    uint32_t lighting_quiet_frames_ = 0;
    /// The convergence samples seen since the last input change, newest at head - 1; sized
    /// for the stationarity comparison (two windows GI_QUIESCENCE_COMPARE_FRAMES apart).
    std::array<float, 64> relight_ring_{};
    uint32_t relight_ring_head_ = 0;
    uint32_t relight_ring_count_ = 0;
    uint64_t relight_sample_consumed_ = 0;
};

} // namespace unravel
