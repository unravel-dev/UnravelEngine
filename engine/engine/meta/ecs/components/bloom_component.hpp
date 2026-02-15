#pragma once

#include <engine/rendering/ecs/components/bloom_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(bloom_component);
LOAD_EXTERN(bloom_component);
REFLECT_EXTERN(bloom_component);
} // namespace unravel
