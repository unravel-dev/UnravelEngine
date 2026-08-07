#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/gi_resolve_pass.h>

#include <cmath>

namespace unravel
{

/**
 * @brief The GI v2 settings surface (tasks/gi_rewrite_plan.md, section 5).
 *
 * Deliberately small: the gather is constant-driven (gi_constants.{h,sh} owns every
 * cross-pass value with its unit and derivation), so what remains is what a person looking
 * at a scene can legitimately judge - energy, resolution, filter behaviour - plus the
 * cascade geometry every GI structure anchors to.
 */
struct gi_settings
{
    gi_resolve_pass::settings resolve{};
    /// Resolution 128 is REQUIRED for the world probes (their lattice axis derives from
    /// it); other values disable them and with them the gather.
    global_sdf_clipmap::settings clipmap{.resolution = 128, .compose_on_gpu = true};
};

/**
 * @brief Volume component exposing GI, blending across post-process volumes like
 *        ssil_component.
 */
class gi_component : public component_crtp<gi_component>
{
public:
    bool enabled = true;
    gi_settings settings{};

    /// Continuous quantities interpolate; counts and mode switches take the dominant
    /// volume's value (a blended count is no volume's intent).
    static void merge_into(gi_settings& result, const gi_settings& from, float contribution, bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }
        const bool dominant = contribution >= 0.5f;
        auto& r = result.resolve;
        const auto& f = from.resolve;
        r.intensity = std::lerp(r.intensity, f.intensity, contribution);
        r.resolution = dominant ? f.resolution : r.resolution;
        r.probe_spacing = dominant ? f.probe_spacing : r.probe_spacing;
        r.enable_screen_trace = dominant ? f.enable_screen_trace : r.enable_screen_trace;
        r.debug_view = dominant ? f.debug_view : r.debug_view;
        r.enable_temporal = dominant ? f.enable_temporal : r.enable_temporal;
        r.max_accum_frames = std::lerp(r.max_accum_frames, f.max_accum_frames, contribution);
        r.reprojection_tolerance =
            std::lerp(r.reprojection_tolerance, f.reprojection_tolerance, contribution);
        r.enable_spatial_denoise = dominant ? f.enable_spatial_denoise : r.enable_spatial_denoise;
        r.denoise_passes = dominant ? f.denoise_passes : r.denoise_passes;
        r.denoise_normal_power = std::lerp(r.denoise_normal_power, f.denoise_normal_power, contribution);
        r.denoise_luma_phi = std::lerp(r.denoise_luma_phi, f.denoise_luma_phi, contribution);
        r.denoise_plane_tolerance =
            std::lerp(r.denoise_plane_tolerance, f.denoise_plane_tolerance, contribution);
        r.denoise_low_count_boost =
            std::lerp(r.denoise_low_count_boost, f.denoise_low_count_boost, contribution);
        r.enable_bilateral_upsample =
            dominant ? f.enable_bilateral_upsample : r.enable_bilateral_upsample;
        r.upsample_normal_power = std::lerp(r.upsample_normal_power, f.upsample_normal_power, contribution);
        r.upsample_plane_tolerance =
            std::lerp(r.upsample_plane_tolerance, f.upsample_plane_tolerance, contribution);
        // Cascade geometry is discrete: a resolution between two volumes is neither volume
        // and forces a rebuild per blend step.
        auto& rc = result.clipmap;
        const auto& fc = from.clipmap;
        rc.compose_on_gpu = dominant ? fc.compose_on_gpu : rc.compose_on_gpu;
        rc.resolution = dominant ? fc.resolution : rc.resolution;
        rc.base_extent = dominant ? fc.base_extent : rc.base_extent;
        rc.level_scale = dominant ? fc.level_scale : rc.level_scale;
        rc.blend_voxels = std::lerp(rc.blend_voxels, fc.blend_voxels, contribution);
        rc.max_levels_per_update = dominant ? fc.max_levels_per_update : rc.max_levels_per_update;
        rc.cull_composition = dominant ? fc.cull_composition : rc.cull_composition;
    }
};

} // namespace unravel
