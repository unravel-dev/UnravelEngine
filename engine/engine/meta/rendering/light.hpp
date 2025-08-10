#pragma once

#include <engine/rendering/light.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(light);
LOAD_EXTERN(light);
REFLECT_EXTERN(light);

} // namespace unravel
