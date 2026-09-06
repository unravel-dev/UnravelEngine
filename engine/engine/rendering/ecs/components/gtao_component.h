#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/gtao_pass.h>

namespace unravel
{

/**
 * @brief Ground Truth Ambient Occlusion volume / camera component (gtao_pass::settings).
 *
 * Independent of the ASSAO component: a camera or volume carrying both runs both passes,
 * so author one or the other.
 */
class gtao_component : public component_crtp<gtao_component>
{
public:
    bool enabled = true;
    gtao_pass::settings settings{};

    static void merge_into(gtao_pass::settings& result,
                           const gtao_pass::settings& from,
                           float contribution,
                           bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }
        result.radius = std::lerp(result.radius, from.radius, contribution);
        result.falloff_range = std::lerp(result.falloff_range, from.falloff_range, contribution);
        result.max_screen_radius = std::lerp(result.max_screen_radius, from.max_screen_radius, contribution);
        result.final_power = std::lerp(result.final_power, from.final_power, contribution);
        result.intensity = std::lerp(result.intensity, from.intensity, contribution);
        result.thin_occluder_compensation =
            std::lerp(result.thin_occluder_compensation, from.thin_occluder_compensation, contribution);
        result.quality_level = contribution >= 0.5f ? from.quality_level : result.quality_level;
        result.resolution = contribution >= 0.5f ? from.resolution : result.resolution;
        result.denoise_passes = contribution >= 0.5f ? from.denoise_passes : result.denoise_passes;
        result.enable_temporal = contribution >= 0.5f ? from.enable_temporal : result.enable_temporal;
        result.temporal_history = std::lerp(result.temporal_history, from.temporal_history, contribution);
        result.temporal_depth_threshold =
            std::lerp(result.temporal_depth_threshold, from.temporal_depth_threshold, contribution);
        result.bent_normal_strength = std::lerp(result.bent_normal_strength, from.bent_normal_strength, contribution);
        result.multi_bounce = contribution >= 0.5f ? from.multi_bounce : result.multi_bounce;
        result.generate_normals = contribution >= 0.5f ? from.generate_normals : result.generate_normals;
        result.normal_map_detail = std::lerp(result.normal_map_detail, from.normal_map_detail, contribution);
    }
};

} // namespace unravel
