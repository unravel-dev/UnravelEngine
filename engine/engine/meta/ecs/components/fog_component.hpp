#pragma once

#include <engine/rendering/ecs/components/fog_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(fog_component);
LOAD_EXTERN(fog_component);
REFLECT_EXTERN(fog_component);
} // namespace unravel
