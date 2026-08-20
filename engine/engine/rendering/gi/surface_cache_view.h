#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/global_sdf_clipmap.h>
#include <engine/rendering/gi/global_sdf_clipmap_gpu.h>

#include <math/math.h>

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
     *        long enough that re-running them would rewrite bit-identical values.
     *
     * The world side is a fixed point when nothing changes: the probe trace rewrites the same
     * stratum values forever (the windowed mean's zero-steady-state-variance property), the
     * convolve re-integrates an unchanged atlas, and the light voxels re-light unchanged
     * content. This tracks the full input set - light-buffer hash, clipmap content epoch,
     * every level's composed origin, and every level's probe-window cell - and reports
     * quiescent only after they have ALL held for @ref quiescence_settle_frames, so the
     * probe<->voxel feedback loop has provably converged through several complete windows
     * before anything is skipped. Any change resets the counter and the passes resume the
     * same frame.
     */
    auto update_quiescence(uint64_t light_hash, const math::vec3& camera_position) -> bool;

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

    /// Four complete probe windows (GI_WORLD_PROBE_WINDOW frames each): the bounce feedback
    /// settles well within one, so this carries a wide margin. See update_quiescence.
    static constexpr uint32_t quiescence_settle_frames = 4u * 16u;
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
};

} // namespace unravel
