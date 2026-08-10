#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/tonemapping_pass.h>

namespace unravel
{

class tonemapping_component : public component_crtp<tonemapping_component>
{
public:
    /// Whether tonemapping is enabled
    bool enabled = true;

    /// Tonemapping settings
    tonemapping_pass::settings settings{};

    /// Merges this component's settings into result. For first contribution, copies; otherwise lerps.
    static void merge_into(tonemapping_pass::settings& result,
                          const tonemapping_pass::settings& from,
                          float contribution,
                          bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }
        result.exposure = std::lerp(result.exposure, from.exposure, contribution);
        result.temperature = std::lerp(result.temperature, from.temperature, contribution);
        result.tint = std::lerp(result.tint, from.tint, contribution);
        result.contrast = std::lerp(result.contrast, from.contrast, contribution);
        result.saturation = std::lerp(result.saturation, from.saturation, contribution);
        if(contribution >= 0.5f)
        {
            result.method = from.method;
            result.dithering = from.dithering;
        }
    }
};

} // namespace unravel
