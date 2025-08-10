#pragma once
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(fxaa_component);
LOAD_EXTERN(fxaa_component);
REFLECT_EXTERN(fxaa_component);
} // namespace unravel
