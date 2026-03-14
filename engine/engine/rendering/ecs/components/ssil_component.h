#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/ssil_pass.h>

namespace unravel
{

class ssil_component : public component_crtp<ssil_component>
{
public:
    bool enabled = true;
    ssil_pass::ssil_settings settings{};

    static void merge_into(ssil_pass::ssil_settings& result,
                           const ssil_pass::ssil_settings& from,
                           float contribution,
                           bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }

        result.max_rays = int(std::lerp(float(result.max_rays), float(from.max_rays), contribution));
        result.max_steps = int(std::lerp(float(result.max_steps), float(from.max_steps), contribution));
        result.depth_tolerance = std::lerp(result.depth_tolerance, from.depth_tolerance, contribution);
        result.brightness = std::lerp(result.brightness, from.brightness, contribution);
        result.max_distance = std::lerp(result.max_distance, from.max_distance, contribution);
        result.enable_half_res = contribution >= 0.5f ? from.enable_half_res : result.enable_half_res;

        result.enable_spatial_denoise = contribution >= 0.5f ? from.enable_spatial_denoise : result.enable_spatial_denoise;
        result.spatial_denoise.depth_sigma = std::lerp(result.spatial_denoise.depth_sigma, from.spatial_denoise.depth_sigma, contribution);
        result.spatial_denoise.normal_power = std::lerp(result.spatial_denoise.normal_power, from.spatial_denoise.normal_power, contribution);
        result.spatial_denoise.luma_sigma = std::lerp(result.spatial_denoise.luma_sigma, from.spatial_denoise.luma_sigma, contribution);
        result.spatial_denoise.passes = int(std::lerp(float(result.spatial_denoise.passes), float(from.spatial_denoise.passes), contribution));

        result.enable_temporal_accumulation = contribution >= 0.5f ? from.enable_temporal_accumulation : result.enable_temporal_accumulation;
        result.temporal.history_strength = std::lerp(result.temporal.history_strength, from.temporal.history_strength, contribution);
        result.temporal.depth_threshold = std::lerp(result.temporal.depth_threshold, from.temporal.depth_threshold, contribution);
        result.temporal.max_accum_frames = contribution >= 0.5f ? from.temporal.max_accum_frames : result.temporal.max_accum_frames;
    }
};

} // namespace unravel
