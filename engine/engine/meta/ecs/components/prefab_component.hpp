#pragma once
#include <engine/ecs/components/prefab_component.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{

SAVE_EXTERN(prefab_component);
LOAD_EXTERN(prefab_component);
REFLECT_EXTERN(prefab_component);

SAVE_EXTERN(prefab_id_component);
LOAD_EXTERN(prefab_id_component);
REFLECT_EXTERN(prefab_id_component);

} // namespace unravel
