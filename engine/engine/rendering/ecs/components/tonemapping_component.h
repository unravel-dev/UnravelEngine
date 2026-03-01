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
        if(contribution >= 0.5f)
        {
            result.method = from.method;
        }
    }
};

} // namespace unravel
