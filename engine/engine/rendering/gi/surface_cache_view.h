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
 * Keeping it on the service would make two cameras fight over one cascade. Each pipeline run would
 * re-snap the origins to its own position, every level would read as stale, and the budgeted
 * recomposition would rebuild one level per run forever without ever settling -- so each camera
 * would spend half its frames tracing a cascade centred on the other one, with no error to say so.
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
     * @param clipmap_settings The view's global distance field (Lumen's layout with the volume's distance field
     *        settings), applied every update so a knob moved in the inspector takes effect without a restart. Passed
     *        rather than stored because the cascade is downstream of the volume blend, which only the pipeline sees.
     *        @c compose_on_gpu is gated on the compute program having loaded, so a backend that cannot compose on
     *        the GPU still composes, on the CPU.
     * @param instances_revision surface_cache_system::get_content_revision (see global_sdf_clipmap::update).
     */
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

    /// See global_sdf_clipmap_gpu::set_march_experiments; applied by the next update.
    void set_march_experiments(uint32_t bits)
    {
        clipmap_gpu_.set_march_experiments(bits);
    }

    auto get_clipmap_gpu() const -> const global_sdf_clipmap_gpu&
    {
        return clipmap_gpu_;
    }

private:
    global_sdf_clipmap clipmap_;
    global_sdf_clipmap_gpu clipmap_gpu_;
    /// Deferred to the first update so that constructing a view costs nothing. A camera that never
    /// enables GI never allocates the cascade texture.
    bool initialized_ = false;
};

} // namespace unravel
