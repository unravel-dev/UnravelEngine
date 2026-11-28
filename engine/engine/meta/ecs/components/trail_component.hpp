#pragma once

#include <engine/rendering/ecs/components/trail_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(trail_component);
LOAD_EXTERN(trail_component);
REFLECT_EXTERN(trail_component);
} // namespace unravel
