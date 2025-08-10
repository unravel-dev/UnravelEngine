#pragma once

#include <engine/rendering/model.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(model);
LOAD_EXTERN(model);
REFLECT_EXTERN(model);

} // namespace unravel
