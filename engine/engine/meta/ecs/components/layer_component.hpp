#pragma once
#include <engine/ecs/components/layer_component.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(layer_component);
LOAD_EXTERN(layer_component);
REFLECT_EXTERN(layer_component);


} // namespace unravel
