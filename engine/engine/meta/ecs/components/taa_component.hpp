#pragma once
#include <engine/rendering/ecs/components/taa_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(taa_component);
LOAD_EXTERN(taa_component);
REFLECT_EXTERN(taa_component);
} // namespace unravel
