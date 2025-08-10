#pragma once

#include <engine/ecs/components/transform_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{

SAVE_EXTERN(transform_component);
LOAD_EXTERN(transform_component);
REFLECT_EXTERN(transform_component);

} // namespace unravel
