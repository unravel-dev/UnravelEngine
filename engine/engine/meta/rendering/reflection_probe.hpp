#pragma once
#include <engine/rendering/reflection_probe.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(reflection_probe);
LOAD_EXTERN(reflection_probe);
REFLECT_EXTERN(reflection_probe);

} // namespace unravel
