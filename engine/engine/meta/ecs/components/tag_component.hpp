#pragma once
#include <engine/ecs/components/tag_component.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{

SAVE_EXTERN(tag_component);
LOAD_EXTERN(tag_component);
REFLECT_EXTERN(tag_component);

} // namespace unravel
