#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/bloom_pass.h>

namespace unravel
{

class bloom_component : public component_crtp<bloom_component>
{
public:
    bool enabled = true;
    bloom_pass::settings settings{};
};

} // namespace unravel
