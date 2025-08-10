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
};

} // namespace unravel 