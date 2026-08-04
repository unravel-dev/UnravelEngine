#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/gi_cache_pass.h>
#include <engine/rendering/pipeline/passes/gi_resolve_pass.h>

#include <cmath>

namespace unravel
{

/**
 * @brief The two halves of surface cache GI, carried together.
 *
 * They are separate passes but one feature: the cache pass populates and lights the world-space
 * entries, the resolve pass gathers them into the screen. Tuning either in isolation is misleading,
 * because the quality the resolve can deliver is bounded by what the cache holds, and the cost of
 * the cache is driven by how much of it the resolve keeps alive.
 */
struct gi_settings
{
    gi_cache_pass::settings cache{};
    gi_resolve_pass::settings resolve{};
    /// The cascade the two passes trace against. Authored here rather than compiled in because its
    /// cost and its reach are the same trade the other two make: resolution is cubic in memory and
    /// in composition work, while base extent and level scale set how far GI sees at all.
    global_sdf_clipmap::settings clipmap{};
};

/**
 * @brief Volume component exposing surface cache GI.
 *
 * Follows the same shape as @ref ssil_component: it resolves either from the camera entity or by
 * blending across post-process volumes, so a scene can vary GI settings by region.
 *
 * These settings were compiled-in defaults until now, and that made every quality-versus-cost
 * question a rebuild. That is not a small inconvenience here: the near-field trace distance, the
 * ray count, the cone relaxation and the trace resolution all trade image quality against frame
 * time in ways only a person looking at the scene can judge, and the right value differs per
 * project. Measured on Bistro, the same pass ranges from 1 ms to 9 ms across the plausible span of
 * these values, so they are not fine tuning.
 */
class gi_component : public component_crtp<gi_component>
{
public:
    bool enabled = true;
    gi_settings settings{};

    /**
     * @brief Blends one volume's contribution into the accumulated result.
     *
     * Continuous quantities interpolate; counts, budgets and mode switches take the dominant
     * volume's value rather than a fraction of one. Interpolating a ray count or a step budget
     * produces a value that is not any volume's intent and costs somewhere between the two, so the
     * blend is deliberately discrete for those.
     */
    static void merge_into(gi_settings& result, const gi_settings& from, float contribution, bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }
        const bool dominant = contribution >= 0.5f;

        // --- cache pass ---
        result.cache.insert_stride = dominant ? from.cache.insert_stride : result.cache.insert_stride;
        result.cache.insert_max_distance =
            std::lerp(result.cache.insert_max_distance, from.cache.insert_max_distance, contribution);
        result.cache.surface_offset_cells =
            std::lerp(result.cache.surface_offset_cells, from.cache.surface_offset_cells, contribution);
        result.cache.min_alpha = std::lerp(result.cache.min_alpha, from.cache.min_alpha, contribution);
        result.cache.max_samples = std::lerp(result.cache.max_samples, from.cache.max_samples, contribution);
        result.cache.bounce_rays = dominant ? from.cache.bounce_rays : result.cache.bounce_rays;
        result.cache.default_albedo = std::lerp(result.cache.default_albedo, from.cache.default_albedo, contribution);
        result.cache.max_albedo = std::lerp(result.cache.max_albedo, from.cache.max_albedo, contribution);
        result.cache.bounce_distance =
            std::lerp(result.cache.bounce_distance, from.cache.bounce_distance, contribution);
        result.cache.bounce_near_field =
            std::lerp(result.cache.bounce_near_field, from.cache.bounce_near_field, contribution);
        result.cache.bounce_max_steps = dominant ? from.cache.bounce_max_steps : result.cache.bounce_max_steps;
        result.cache.bounce_surface_bias =
            std::lerp(result.cache.bounce_surface_bias, from.cache.bounce_surface_bias, contribution);
        result.cache.shadow_distance =
            std::lerp(result.cache.shadow_distance, from.cache.shadow_distance, contribution);
        result.cache.shadow_normal_bias_voxels =
            std::lerp(result.cache.shadow_normal_bias_voxels, from.cache.shadow_normal_bias_voxels,
                      contribution);
        result.cache.shadow_near_field =
            std::lerp(result.cache.shadow_near_field, from.cache.shadow_near_field, contribution);
        result.cache.shadow_max_steps = dominant ? from.cache.shadow_max_steps : result.cache.shadow_max_steps;
        result.cache.shadow_ray_start_voxels = std::lerp(result.cache.shadow_ray_start_voxels,
                                                         from.cache.shadow_ray_start_voxels,
                                                         contribution);
        result.cache.shadow_surface_bias =
            std::lerp(result.cache.shadow_surface_bias, from.cache.shadow_surface_bias, contribution);
        result.cache.shadow_step_relaxation =
            std::lerp(result.cache.shadow_step_relaxation, from.cache.shadow_step_relaxation, contribution);
        result.cache.update_interval = dominant ? from.cache.update_interval : result.cache.update_interval;

        // --- resolve pass ---
        result.resolve.ray_count = dominant ? from.resolve.ray_count : result.resolve.ray_count;
        result.resolve.max_distance =
            std::lerp(result.resolve.max_distance, from.resolve.max_distance, contribution);
        result.resolve.normal_bias_voxels =
            std::lerp(result.resolve.normal_bias_voxels, from.resolve.normal_bias_voxels, contribution);
        result.resolve.intensity = std::lerp(result.resolve.intensity, from.resolve.intensity, contribution);
        result.resolve.near_field_distance =
            std::lerp(result.resolve.near_field_distance, from.resolve.near_field_distance, contribution);
        result.resolve.near_field_fade_distance = std::lerp(result.resolve.near_field_fade_distance,
                                                            from.resolve.near_field_fade_distance,
                                                            contribution);
        result.resolve.max_steps = dominant ? from.resolve.max_steps : result.resolve.max_steps;
        result.resolve.surface_bias = std::lerp(result.resolve.surface_bias, from.resolve.surface_bias, contribution);
        result.resolve.step_relaxation =
            std::lerp(result.resolve.step_relaxation, from.resolve.step_relaxation, contribution);
        result.resolve.ray_start_voxels =
            std::lerp(result.resolve.ray_start_voxels, from.resolve.ray_start_voxels, contribution);
        result.resolve.debug_ray_diagnostics =
            dominant ? from.resolve.debug_ray_diagnostics : result.resolve.debug_ray_diagnostics;
        result.resolve.interpolate_cache = dominant ? from.resolve.interpolate_cache : result.resolve.interpolate_cache;
        result.resolve.occlude_on_cache_miss =
            dominant ? from.resolve.occlude_on_cache_miss : result.resolve.occlude_on_cache_miss;
        result.resolve.resolution = dominant ? from.resolve.resolution : result.resolve.resolution;

        result.resolve.enable_temporal = dominant ? from.resolve.enable_temporal : result.resolve.enable_temporal;
        result.resolve.max_accum_frames =
            std::lerp(result.resolve.max_accum_frames, from.resolve.max_accum_frames, contribution);
        result.resolve.reprojection_tolerance =
            std::lerp(result.resolve.reprojection_tolerance, from.resolve.reprojection_tolerance, contribution);
        result.resolve.history_clamp_sigma =
            std::lerp(result.resolve.history_clamp_sigma, from.resolve.history_clamp_sigma, contribution);

        result.resolve.enable_spatial_denoise =
            dominant ? from.resolve.enable_spatial_denoise : result.resolve.enable_spatial_denoise;
        result.resolve.denoise_passes = dominant ? from.resolve.denoise_passes : result.resolve.denoise_passes;
        result.resolve.denoise_normal_power =
            std::lerp(result.resolve.denoise_normal_power, from.resolve.denoise_normal_power, contribution);
        result.resolve.denoise_luma_phi =
            std::lerp(result.resolve.denoise_luma_phi, from.resolve.denoise_luma_phi, contribution);
        result.resolve.denoise_plane_tolerance =
            std::lerp(result.resolve.denoise_plane_tolerance, from.resolve.denoise_plane_tolerance, contribution);
        result.resolve.denoise_low_count_boost =
            std::lerp(result.resolve.denoise_low_count_boost, from.resolve.denoise_low_count_boost, contribution);

        // --- clipmap ---
        // Every one of these is discrete: a cascade resolution or a level count between two volumes'
        // values is not either volume's intent, and the layout ones additionally force a rebuild, so
        // interpolating them would rebuild the cascade on every step of a blend.
        result.clipmap.compose_on_gpu = dominant ? from.clipmap.compose_on_gpu : result.clipmap.compose_on_gpu;
        result.clipmap.resolution = dominant ? from.clipmap.resolution : result.clipmap.resolution;
        result.clipmap.base_extent = dominant ? from.clipmap.base_extent : result.clipmap.base_extent;
        result.clipmap.level_scale = dominant ? from.clipmap.level_scale : result.clipmap.level_scale;
        result.clipmap.blend_voxels =
            std::lerp(result.clipmap.blend_voxels, from.clipmap.blend_voxels, contribution);
        result.clipmap.max_levels_per_update =
            dominant ? from.clipmap.max_levels_per_update : result.clipmap.max_levels_per_update;
        result.clipmap.cull_composition =
            dominant ? from.clipmap.cull_composition : result.clipmap.cull_composition;

        result.resolve.enable_bilateral_upsample =
            dominant ? from.resolve.enable_bilateral_upsample : result.resolve.enable_bilateral_upsample;
        result.resolve.upsample_normal_power =
            std::lerp(result.resolve.upsample_normal_power, from.resolve.upsample_normal_power, contribution);
        result.resolve.upsample_plane_tolerance =
            std::lerp(result.resolve.upsample_plane_tolerance, from.resolve.upsample_plane_tolerance, contribution);
    }
};

} // namespace unravel
