#include "surface_cache_view.h"

#include <engine/profiler/profiler.h>
#include <engine/rendering/gi/gi_constants.h>

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
        if(!clipmap_gpu_.init(clipmap_settings.resolution, clipmap_settings.compose_on_gpu))
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
        // A composer change rebuilds BOTH halves, not just the mirror. The mirror's surface
        // buffer flags encode who writes them (see global_sdf_clipmap_gpu::init), and the CPU
        // cascade's staleness bookkeeping survives a settings assignment - so a flip to the CPU
        // composer with fingerprints intact would compose nothing into the empty CPU voxel
        // arrays and upload nothing: a cascade frozen at creation, with nothing to say why.
        const bool composer_changed =
            clipmap_.get_settings().compose_on_gpu != clipmap_settings.compose_on_gpu;
        const bool layout_changed = clipmap_.apply_settings(clipmap_settings);
        if(composer_changed && !layout_changed)
        {
            clipmap_.init(clipmap_settings);
        }
        if(layout_changed || composer_changed)
        {
            if(!clipmap_gpu_.init(clipmap_settings.resolution, clipmap_settings.compose_on_gpu))
            {
                APPLOG_WARNING("[SurfaceCache] Clipmap resize to {} failed; the cascade is now "
                               "unavailable for this view.",
                               clipmap_settings.resolution);
            }
        }
    }
    // The cascade decides for itself which levels a change reached. A single global "something
    // moved" flag could only say "all of them", which meant composing four levels in the frame
    // anything moved -- and composing a level is expensive enough that this was the whole cost of
    // having animation in the scene.
    const uint32_t composed = clipmap_.update(instances, camera_position, instances_revision);
    if(composed > 0)
    {
        // A recompose rewrote surface voxels; the world side must relight them.
        quiescence_frames_ = 0;
    }
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

auto surface_cache_view::update_quiescence(uint64_t light_hash, const math::vec3& camera_position)
    -> bool
{
    bool changed = false;
    bool lighting_changed = false;
    if(light_hash != quiescence_light_hash_)
    {
        quiescence_light_hash_ = light_hash;
        changed = true;
        lighting_changed = true;
    }
    const uint64_t epoch = clipmap_.get_content_epoch();
    if(epoch != quiescence_content_epoch_)
    {
        quiescence_content_epoch_ = epoch;
        changed = true;
        lighting_changed = true;
    }
    // The lighting-only counter (see get_lighting_quiet_frames): origins and probe cells
    // below deliberately do not touch it - camera travel does not stale accumulated light.
    if(lighting_changed)
    {
        lighting_quiet_frames_ = 0;
    }
    else if(lighting_quiet_frames_ < 0x40000000u)
    {
        ++lighting_quiet_frames_;
    }
    for(uint32_t level = 0; level < global_sdf_clipmap::level_count; ++level)
    {
        // The composed origin: any recompose (content or camera driven) moves through here.
        const auto& origin = clipmap_.get_level(level).origin;
        if(origin != quiescence_origins_[level])
        {
            quiescence_origins_[level] = origin;
            changed = true;
        }
        // The probe window cell, in the exact form the probe pass derives it: a crossing
        // scrolls probe slots and bumps the vis-memo generation, both of which need the
        // passes live.
        const float spacing =
            clipmap_.get_level(level).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
        const float safe_spacing = spacing > 0.0f ? spacing : 1.0f;
        const math::ivec3 cell(int(std::floor(camera_position.x / safe_spacing + 0.5f)),
                               int(std::floor(camera_position.y / safe_spacing + 0.5f)),
                               int(std::floor(camera_position.z / safe_spacing + 0.5f)));
        if(cell != quiescence_probe_cells_[level])
        {
            quiescence_probe_cells_[level] = cell;
            changed = true;
        }
    }
    if(changed)
    {
        quiescence_frames_ = 0;
        return false;
    }
    if(quiescence_frames_ < quiescence_settle_frames)
    {
        ++quiescence_frames_;
        return false;
    }
    return true;
}

} // namespace unravel
