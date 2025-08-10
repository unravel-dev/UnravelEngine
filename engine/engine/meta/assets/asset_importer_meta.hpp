#pragma once
#include <engine/engine_export.h>

#include <engine/assets/asset_manager.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{

SAVE_EXTERN(asset_importer_meta);
LOAD_EXTERN(asset_importer_meta);
REFLECT_EXTERN(asset_importer_meta);

SAVE_EXTERN(texture_importer_meta);
LOAD_EXTERN(texture_importer_meta);
REFLECT_EXTERN(texture_importer_meta);

SAVE_EXTERN(mesh_importer_meta);
LOAD_EXTERN(mesh_importer_meta);
REFLECT_EXTERN(mesh_importer_meta);

SAVE_EXTERN(animation_importer_meta);
LOAD_EXTERN(animation_importer_meta);
REFLECT_EXTERN(animation_importer_meta);

} // namespace unravel



#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
SERIALIZE_REGISTER_TYPE_WITH_NAME(unravel::mesh_importer_meta, "mesh_importer_meta")
SERIALIZE_REGISTER_TYPE_WITH_NAME(unravel::texture_importer_meta, "texture_importer_meta")
SERIALIZE_REGISTER_TYPE_WITH_NAME(unravel::animation_importer_meta, "animation_importer_meta")
