#pragma once

#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/ssr_pass.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{

class ssr_component : public component_crtp<ssr_component>
{
public:
    /// Whether SSR is enabled
    bool enabled = true;

    /// SSR pass settings
    ssr_pass::ssr_settings settings{};

    /// Merges this component's settings into result. First volume wins: subsequent contributions are ignored.
    static void merge_into(ssr_pass::ssr_settings& result,
                          const ssr_pass::ssr_settings& from,
                          float contribution,
                          bool is_first)
    {
        if(is_first)
        {
            result = from;
        }
    }
};

} // namespace unravel 