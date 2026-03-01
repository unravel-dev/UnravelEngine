#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/assao_pass.h>

namespace unravel
{

class assao_component : public component_crtp<assao_component>
{
public:
    /// Whether ASSAO is enabled
    bool enabled = true;

    /// ASSAO settings
    assao_pass::settings settings{};

    /// Merges this component's settings into result. For first contribution, copies; otherwise lerps.
    static void merge_into(assao_pass::settings& result,
                          const assao_pass::settings& from,
                          float contribution,
                          bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }
        result.radius = std::lerp(result.radius, from.radius, contribution);
        result.shadow_multiplier = std::lerp(result.shadow_multiplier, from.shadow_multiplier, contribution);
        result.shadow_power = std::lerp(result.shadow_power, from.shadow_power, contribution);
        result.shadow_clamp = std::lerp(result.shadow_clamp, from.shadow_clamp, contribution);
        result.horizon_angle_threshold = std::lerp(result.horizon_angle_threshold, from.horizon_angle_threshold, contribution);
        result.fade_out_from = std::lerp(result.fade_out_from, from.fade_out_from, contribution);
        result.fade_out_to = std::lerp(result.fade_out_to, from.fade_out_to, contribution);
        result.quality_level = contribution >= 0.5f ? from.quality_level : result.quality_level;
        result.adaptive_quality_limit = std::lerp(result.adaptive_quality_limit, from.adaptive_quality_limit, contribution);
        result.blur_pass_count = contribution >= 0.5f ? from.blur_pass_count : result.blur_pass_count;
        result.sharpness = std::lerp(result.sharpness, from.sharpness, contribution);
        result.temporal_supersampling_angle_offset = std::lerp(result.temporal_supersampling_angle_offset, from.temporal_supersampling_angle_offset, contribution);
        result.temporal_supersampling_radius_offset = std::lerp(result.temporal_supersampling_radius_offset, from.temporal_supersampling_radius_offset, contribution);
        result.detail_shadow_strength = std::lerp(result.detail_shadow_strength, from.detail_shadow_strength, contribution);
        result.generate_normals = contribution >= 0.5f ? from.generate_normals : result.generate_normals;
    }
};

} // namespace unravel
