#pragma once
#include <engine/rendering/ecs/components/tonemapping_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(tonemapping_component);
LOAD_EXTERN(tonemapping_component);
REFLECT_EXTERN(tonemapping_component);
} // namespace unravel
