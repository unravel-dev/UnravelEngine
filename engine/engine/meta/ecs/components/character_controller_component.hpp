#pragma once

#include <engine/physics/ecs/components/character_controller_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(character_controller_component);
LOAD_EXTERN(character_controller_component);
REFLECT_EXTERN(character_controller_component);
} // namespace unravel
