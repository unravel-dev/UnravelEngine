#pragma once
#include <engine/rendering/ecs/components/ssr_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(ssr_component);
LOAD_EXTERN(ssr_component);
REFLECT_EXTERN(ssr_component);
} // namespace unravel 