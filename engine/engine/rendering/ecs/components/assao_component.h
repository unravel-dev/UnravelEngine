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
};

} // namespace unravel
