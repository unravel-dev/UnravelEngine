#pragma once

#include <engine/rendering/ecs/components/line_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(line_component);
LOAD_EXTERN(line_component);
REFLECT_EXTERN(line_component);
} // namespace unravel
