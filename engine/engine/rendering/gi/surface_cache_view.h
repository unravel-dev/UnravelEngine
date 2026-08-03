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
 * Everything in @ref surface_cache_service is a function of the WORLD -- which meshes are resident,
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
                const global_sdf_clipmap::settings& clipmap_settings);

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

private:
    global_sdf_clipmap clipmap_;
    global_sdf_clipmap_gpu clipmap_gpu_;
    /// Deferred to the first update so that constructing a view costs nothing. A camera that never
    /// enables GI never allocates the cascade texture.
    bool initialized_ = false;
    bool log_composition_stats_ = false;
};

} // namespace unravel
