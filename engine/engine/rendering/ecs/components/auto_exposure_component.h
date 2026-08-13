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
        result.min_ev = std::lerp(result.min_ev, from.min_ev, contribution);
        result.max_ev = std::lerp(result.max_ev, from.max_ev, contribution);
        result.compensation = std::lerp(result.compensation, from.compensation, contribution);
        result.dark_adaptation = std::lerp(result.dark_adaptation, from.dark_adaptation, contribution);
        result.adaptation_speed_up = std::lerp(result.adaptation_speed_up, from.adaptation_speed_up, contribution);
        result.adaptation_speed_down = std::lerp(result.adaptation_speed_down, from.adaptation_speed_down, contribution);
        result.low_percentile = std::lerp(result.low_percentile, from.low_percentile, contribution);
        result.high_percentile = std::lerp(result.high_percentile, from.high_percentile, contribution);
        result.metering_area = std::lerp(result.metering_area, from.metering_area, contribution);
        // Metering mode is discrete and cannot be interpolated; the dominant volume wins.
        if(contribution >= 0.5f)
        {
            result.metering_mode = from.metering_mode;
        }
    }
};

} // namespace unravel
