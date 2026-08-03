#include "surface_cache_view.h"

#include <engine/profiler/profiler.h>

#include <logging/logging.h>

namespace unravel
{

void surface_cache_view::update(const std::vector<global_sdf_instance>& instances,
                                const math::vec3& camera_position,
                                const global_sdf_clipmap::settings& clipmap_settings)
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
    else if(clipmap_.apply_settings(clipmap_settings))
    {
        if(!clipmap_gpu_.init(clipmap_settings.resolution))
        {
            APPLOG_WARNING("[SurfaceCache] Clipmap resize to {} failed; the cascade is now unavailable "
                           "for this view.",
                           clipmap_settings.resolution);
        }
    }
    // The cascade decides for itself which levels a change reached. A single global "something
    // moved" flag could only say "all of them", which meant composing four levels in the frame
    // anything moved -- and composing a level is expensive enough that this was the whole cost of
    // having animation in the scene.
    const uint32_t composed = clipmap_.update(instances, camera_position);
    if(composed > 0 && log_composition_stats_)
    {
        const auto& stats = clipmap_.get_last_compose_stats();
        const double mean_candidates =
            stats.cull_cells > 0 ? double(stats.cull_references) / double(stats.cull_cells) : 0.0;
        const double sampled_fraction =
            stats.candidate_tests > 0 ? double(stats.field_samples) / double(stats.candidate_tests) : 0.0;
        APPLOG_INFO("[SurfaceCache] Composed level {0}: {1} relevant instances, {2} cells, "
                    "{3} refs ({4:.1f} mean / {5} worst per cell), {6} candidate tests, "
                    "{7} field samples ({8:.1f}%), {9} level(s) this update, {10} still stale.",
                    stats.level,
                    stats.relevant_instances,
                    stats.cull_cells,
                    stats.cull_references,
                    mean_candidates,
                    stats.max_candidates_in_cell,
                    stats.candidate_tests,
                    stats.field_samples,
                    sampled_fraction * 100.0,
                    composed,
                    clipmap_.get_stale_level_count());
    }
    clipmap_gpu_.upload(clipmap_);
}

} // namespace unravel
