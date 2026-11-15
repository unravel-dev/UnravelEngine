#pragma once
#include <engine/rendering/material_graph/standard_material.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(legacy_pbr_material);
LOAD_EXTERN(legacy_pbr_material);
REFLECT_EXTERN(legacy_pbr_material);

} // namespace unravel

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
// The name files written before every material was a graph give a PBR material.
SERIALIZE_REGISTER_TYPE_WITH_NAME(unravel::legacy_pbr_material, "pbr_material")
