#pragma once

#include <engine/rendering/dynamic_geometry/strip_style.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(strip_style);
LOAD_EXTERN(strip_style);
REFLECT_EXTERN(strip_style);
} // namespace unravel
