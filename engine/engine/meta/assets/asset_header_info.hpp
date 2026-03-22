#pragma once
#include <engine/engine_export.h>

#include <engine/assets/asset_manager.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{

SAVE_EXTERN(asset_header_info);
LOAD_EXTERN(asset_header_info);

SAVE_EXTERN(texture_header_info);
LOAD_EXTERN(texture_header_info);

SAVE_EXTERN(mesh_header_info);
LOAD_EXTERN(mesh_header_info);

SAVE_EXTERN(animation_header_info);
LOAD_EXTERN(animation_header_info);

SAVE_EXTERN(audio_header_info);
LOAD_EXTERN(audio_header_info);

} // namespace unravel

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
SERIALIZE_REGISTER_TYPE_WITH_NAME(unravel::texture_header_info, "texture_header_info")
SERIALIZE_REGISTER_TYPE_WITH_NAME(unravel::mesh_header_info, "mesh_header_info")
SERIALIZE_REGISTER_TYPE_WITH_NAME(unravel::animation_header_info, "animation_header_info")
SERIALIZE_REGISTER_TYPE_WITH_NAME(unravel::audio_header_info, "audio_header_info")
