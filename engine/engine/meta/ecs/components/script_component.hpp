#pragma once
#include <engine/scripting/ecs/components/script_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(script_component);
LOAD_EXTERN(script_component);
REFLECT_EXTERN(script_component);

} // namespace unravel
