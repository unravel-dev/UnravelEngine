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
     */
    auto update_quiescence(uint64_t light_hash,
                           const math::vec3& camera_position,
                           const relight_sample& relight) -> bool;

    /// Frames the full quiescence input set (light hash, content epoch, window origins,
    /// probe cells) has held unchanged - 0 on any change.
    auto get_quiet_frames() const -> uint32_t
    {
        return quiescence_frames_;
    }

    /// Frames since the LIGHTING-relevant subset changed: the light hash and the content
    /// epoch only. The epoch is already suppressed while origins move (see the fingerprint
    /// cache), so camera travel does NOT reset this - it fires exactly when accumulated
    /// lighting went stale (an instance moved / appeared / changed material, a light
    /// changed). The temporal accumulators key their fast-flush window off this: a camera
    /// pan keeps full temporal depth, an edit drops to the fast caps until the stale
    /// energy has provably washed out (quiescence_settle_frames of fast-rate blending).
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
    global_sdf_clipmap clipmap_;
    global_sdf_clipmap_gpu clipmap_gpu_;
    /// Deferred to the first update so that constructing a view costs nothing. A camera that never
    /// enables GI never allocates the cascade texture.
    bool initialized_ = false;
    bool log_composition_stats_ = false;
    /// update_quiescence state: the last-seen input set and how long it has held.
    uint64_t quiescence_light_hash_ = 0;
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
