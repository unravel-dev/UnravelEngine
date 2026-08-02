#pragma once
#include <engine/rendering/ecs/components/gi_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(gi_component);
LOAD_EXTERN(gi_component);
REFLECT_EXTERN(gi_component);
} // namespace unravel
