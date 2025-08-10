#pragma once
#include <engine/rendering/ecs/components/text_component.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{

SAVE_EXTERN(text_component);
LOAD_EXTERN(text_component);
REFLECT_EXTERN(text_component);

} // namespace unravel
