#pragma once

#include <engine/rendering/material_instance.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(material_instance);
LOAD_EXTERN(material_instance);
REFLECT_EXTERN(material_instance);

} // namespace unravel
