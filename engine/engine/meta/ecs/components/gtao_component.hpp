#pragma once
#include <engine/rendering/ecs/components/gtao_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(gtao_component);
LOAD_EXTERN(gtao_component);
REFLECT_EXTERN(gtao_component);
} // namespace unravel
