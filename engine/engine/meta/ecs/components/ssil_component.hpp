#pragma once
#include <engine/rendering/ecs/components/ssil_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(ssil_component);
LOAD_EXTERN(ssil_component);
REFLECT_EXTERN(ssil_component);
} // namespace unravel
