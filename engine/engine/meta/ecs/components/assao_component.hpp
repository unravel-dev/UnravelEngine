#pragma once
#include <engine/rendering/ecs/components/assao_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(assao_component);
LOAD_EXTERN(assao_component);
REFLECT_EXTERN(assao_component);
} // namespace unravel
