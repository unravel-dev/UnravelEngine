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
};

} // namespace unravel
