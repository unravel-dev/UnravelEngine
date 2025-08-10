#pragma once
#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>

namespace unravel
{

class fxaa_component : public component_crtp<fxaa_component>
{
public:
    /// Whether FXAA is enabled
    bool enabled = true;
};

} // namespace unravel
