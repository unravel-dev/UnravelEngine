#include "surface_cache_view.h"

#include <engine/profiler/profiler.h>

#include <logging/logging.h>

namespace unravel
{

void surface_cache_view::update(const std::vector<global_sdf_instance>& instances,
                                const math::vec3& camera_position,
                                const global_sdf_clipmap::settings& clipmap_settings,
                                uint64_t instances_revision)
{
    APP_SCOPE_PERF("GI/SurfaceCache/Update View");
    if(!initialized_)
    {
        clipmap_.init(clipmap_settings);
        if(!clipmap_gpu_.init(clipmap_settings.resolution))
        {
            APPLOG_WARNING("[SurfaceCache] Clipmap initialisation failed for this view. Only "
                           "per-instance field tracing will be available, so distant and offscreen "
                           "geometry will not contribute.");
        }
        initialized_ = true;
    }
    // Re-applied every update so an inspector change takes effect immediately. The GPU mirror has to
    // follow a layout change, since its texture is sized to the resolution -- and it is re-created
    // BEFORE the cascade is used, so a frame never samples a texture sized for the old one.
    else
    {
        // A composer change re-initialises the cascade: its staleness bookkeeping survives a
        // settings assignment, so a flip to the CPU composer with fingerprints intact would
        // compose nothing into the empty CPU voxel arrays and upload nothing - a cascade frozen
        // at creation, with nothing to say why. The mirror is re-created with it, so coverage the
        // GPU composer wrote does not outlive its composer.
        const bool composer_changed =
            clipmap_.get_settings().compose_on_gpu != clipmap_settings.compose_on_gpu;
        const bool layout_changed = clipmap_.apply_settings(clipmap_settings);
        if(composer_changed && !layout_changed)
        {
            clipmap_.init(clipmap_settings);
        }
        if((layout_changed || composer_changed) && !clipmap_gpu_.init(clipmap_settings.resolution))
        {
            APPLOG_WARNING("[SurfaceCache] Clipmap resize to {} failed; the cascade is now "
                           "unavailable for this view.",
                           clipmap_settings.resolution);
        }
    }
    // The cascade decides for itself which levels a change reached. A single global "something
    // moved" flag could only say "all of them", which would mean composing four levels in the
    // frame anything moved -- and composing a level is expensive enough that this would be the
    // whole cost of having animation in the scene.
    clipmap_.update(instances, camera_position, instances_revision);
    clipmap_gpu_.upload(clipmap_);
}

} // namespace unravel
