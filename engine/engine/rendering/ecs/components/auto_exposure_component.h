#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/auto_exposure_pass.h>
#include <cmath>

namespace unravel
{

class auto_exposure_component : public component_crtp<auto_exposure_component>
{
public:
    bool enabled = true;
    auto_exposure_pass::settings settings{};

    /// Merges this component's settings into result. For first contribution, copies; otherwise lerps.
    static void merge_into(auto_exposure_pass::settings& result,
                          const auto_exposure_pass::settings& from,
                          float contribution,
                          bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }
        result.compensation = std::lerp(result.compensation, from.compensation, contribution);
        result.min_ev = std::lerp(result.min_ev, from.min_ev, contribution);
        result.max_ev = std::lerp(result.max_ev, from.max_ev, contribution);
        result.low_percentile = std::lerp(result.low_percentile, from.low_percentile, contribution);
        result.high_percentile = std::lerp(result.high_percentile, from.high_percentile, contribution);
        result.speed_up = std::lerp(result.speed_up, from.speed_up, contribution);
        result.speed_down = std::lerp(result.speed_down, from.speed_down, contribution);
        result.dark_adaptation = std::lerp(result.dark_adaptation, from.dark_adaptation, contribution);
        result.metering_area = std::lerp(result.metering_area, from.metering_area, contribution);
        result.local_highlight_contrast =
            std::lerp(result.local_highlight_contrast, from.local_highlight_contrast, contribution);
        result.local_shadow_contrast =
            std::lerp(result.local_shadow_contrast, from.local_shadow_contrast, contribution);
        result.local_detail_strength =
            std::lerp(result.local_detail_strength, from.local_detail_strength, contribution);
        result.local_blurred_blend = std::lerp(result.local_blurred_blend, from.local_blurred_blend, contribution);
        result.local_blurred_kernel_percent =
            std::lerp(result.local_blurred_kernel_percent, from.local_blurred_kernel_percent, contribution);
        result.local_middle_grey_bias =
            std::lerp(result.local_middle_grey_bias, from.local_middle_grey_bias, contribution);
        // Metering mode is discrete and cannot be interpolated; the dominant volume wins.
        if(contribution >= 0.5f)
        {
            result.metering_mode = from.metering_mode;
        }
    }
};

} // namespace unravel
