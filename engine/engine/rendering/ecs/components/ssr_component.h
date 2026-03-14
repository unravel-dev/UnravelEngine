#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/ssr_pass.h>

namespace unravel
{

class ssr_component : public component_crtp<ssr_component>
{
public:
    /// Whether SSR is enabled
    bool enabled = true;

    /// SSR pass settings
    ssr_pass::ssr_settings settings{};

    /// Merges this component's settings into result. For first contribution, copies; otherwise lerps.
    static void merge_into(ssr_pass::ssr_settings& result,
                          const ssr_pass::ssr_settings& from,
                          float contribution,
                          bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }

        auto& r = result.fidelityfx;
        const auto& f = from.fidelityfx;

        r.max_steps = int(std::lerp(float(r.max_steps), float(f.max_steps), contribution));
        r.max_rays = int(std::lerp(float(r.max_rays), float(f.max_rays), contribution));
        r.depth_tolerance = std::lerp(r.depth_tolerance, f.depth_tolerance, contribution);
        r.brightness = std::lerp(r.brightness, f.brightness, contribution);
        r.facing_reflections_fading = std::lerp(r.facing_reflections_fading, f.facing_reflections_fading, contribution);
        r.roughness_depth_tolerance = std::lerp(r.roughness_depth_tolerance, f.roughness_depth_tolerance, contribution);
        r.fade_in_start = std::lerp(r.fade_in_start, f.fade_in_start, contribution);
        r.fade_in_end = std::lerp(r.fade_in_end, f.fade_in_end, contribution);
        r.enable_half_res = contribution >= 0.5f ? f.enable_half_res : r.enable_half_res;

        r.enable_cone_tracing = contribution >= 0.5f ? f.enable_cone_tracing : r.enable_cone_tracing;
        r.cone_tracing.cone_angle_bias = std::lerp(r.cone_tracing.cone_angle_bias, f.cone_tracing.cone_angle_bias, contribution);
        r.cone_tracing.max_mip_level = contribution >= 0.5f ? f.cone_tracing.max_mip_level : r.cone_tracing.max_mip_level;
        r.cone_tracing.blur_base_sigma = std::lerp(r.cone_tracing.blur_base_sigma, f.cone_tracing.blur_base_sigma, contribution);
        r.cone_tracing.roughness_multiplier = std::lerp(r.cone_tracing.roughness_multiplier, f.cone_tracing.roughness_multiplier, contribution);

        r.enable_temporal_accumulation = contribution >= 0.5f ? f.enable_temporal_accumulation : r.enable_temporal_accumulation;
        r.temporal.history_strength = std::lerp(r.temporal.history_strength, f.temporal.history_strength, contribution);
        r.temporal.depth_threshold = std::lerp(r.temporal.depth_threshold, f.temporal.depth_threshold, contribution);
        r.temporal.roughness_sensitivity = std::lerp(r.temporal.roughness_sensitivity, f.temporal.roughness_sensitivity, contribution);
        r.temporal.motion_scale_pixels = std::lerp(r.temporal.motion_scale_pixels, f.temporal.motion_scale_pixels, contribution);
        r.temporal.normal_dot_threshold = std::lerp(r.temporal.normal_dot_threshold, f.temporal.normal_dot_threshold, contribution);
        r.temporal.motion_scale_pixels = std::lerp(r.temporal.motion_scale_pixels, f.temporal.motion_scale_pixels, contribution);
        r.temporal.normal_dot_threshold = std::lerp(r.temporal.normal_dot_threshold, f.temporal.normal_dot_threshold, contribution);
        r.temporal.max_accum_frames = contribution >= 0.5f ? f.temporal.max_accum_frames : r.temporal.max_accum_frames;

        r.enable_spatial_denoise = contribution >= 0.5f ? f.enable_spatial_denoise : r.enable_spatial_denoise;
        r.spatial_denoise.depth_sigma = std::lerp(r.spatial_denoise.depth_sigma, f.spatial_denoise.depth_sigma, contribution);
        r.spatial_denoise.normal_power = std::lerp(r.spatial_denoise.normal_power, f.spatial_denoise.normal_power, contribution);
        r.spatial_denoise.luma_sigma = std::lerp(r.spatial_denoise.luma_sigma, f.spatial_denoise.luma_sigma, contribution);
    }
};

} // namespace unravel 