#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/bloom_pass.h>
#include <cmath>

namespace unravel
{

class bloom_component : public component_crtp<bloom_component>
{
public:
    bool enabled = true;
    bloom_pass::settings settings{};

    /// Merges this component's settings into result. For first contribution, copies; otherwise lerps.
    static void merge_into(bloom_pass::settings& result,
                          const bloom_pass::settings& from,
                          float contribution,
                          bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }
        result.threshold = std::lerp(result.threshold, from.threshold, contribution);
        result.soft_knee = std::lerp(result.soft_knee, from.soft_knee, contribution);
        result.clamp = std::lerp(result.clamp, from.clamp, contribution);
        result.intensity = std::lerp(result.intensity, from.intensity, contribution);
        result.mip_count = static_cast<int>(std::lround(std::lerp(float(result.mip_count), float(from.mip_count), contribution)));
    }
};

} // namespace unravel
