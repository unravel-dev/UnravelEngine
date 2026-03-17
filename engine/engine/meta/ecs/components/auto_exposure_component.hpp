#pragma once
#include <engine/rendering/ecs/components/auto_exposure_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(auto_exposure_component);
LOAD_EXTERN(auto_exposure_component);
REFLECT_EXTERN(auto_exposure_component);
} // namespace unravel
