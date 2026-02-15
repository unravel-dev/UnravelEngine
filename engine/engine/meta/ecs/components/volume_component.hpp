#pragma once

#include <engine/rendering/ecs/components/volume_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(volume_component);
LOAD_EXTERN(volume_component);
REFLECT_EXTERN(volume_component);
} // namespace unravel
