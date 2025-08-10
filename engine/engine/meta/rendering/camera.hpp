#pragma once

#include <engine/rendering/camera.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(camera);
LOAD_EXTERN(camera);
REFLECT_EXTERN(camera);

} // namespace unravel
