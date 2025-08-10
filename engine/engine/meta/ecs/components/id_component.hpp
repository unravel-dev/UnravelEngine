#pragma once
#include <engine/ecs/components/id_component.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(id_component);
LOAD_EXTERN(id_component);
REFLECT_EXTERN(id_component);

} // namespace unravel
