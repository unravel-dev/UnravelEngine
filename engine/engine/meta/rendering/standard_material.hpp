#pragma once
#include <engine/rendering/material.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(pbr_material);
LOAD_EXTERN(pbr_material);
REFLECT_EXTERN(pbr_material);

} // namespace unravel

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
SERIALIZE_REGISTER_TYPE_WITH_NAME(unravel::pbr_material, "pbr_material")
