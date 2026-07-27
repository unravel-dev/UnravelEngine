#pragma once
#include <engine/rendering/ecs/components/surface_cache_gi_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(surface_cache_gi_component);
LOAD_EXTERN(surface_cache_gi_component);
REFLECT_EXTERN(surface_cache_gi_component);
} // namespace unravel
