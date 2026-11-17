#pragma once

#include <engine/rendering/ecs/components/decal_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(decal_component);
LOAD_EXTERN(decal_component);
REFLECT_EXTERN(decal_component);
} // namespace unravel
