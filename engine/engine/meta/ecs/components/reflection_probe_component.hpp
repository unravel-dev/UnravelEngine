#pragma once
#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(reflection_probe_component);
LOAD_EXTERN(reflection_probe_component);
REFLECT_EXTERN(reflection_probe_component);
} // namespace unravel
