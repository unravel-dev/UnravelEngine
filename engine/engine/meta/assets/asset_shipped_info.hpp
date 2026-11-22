#pragma once
#include <engine/engine_export.h>

#include <engine/assets/asset_manager.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{

SAVE_EXTERN(asset_shipped_info);
LOAD_EXTERN(asset_shipped_info);

SAVE_EXTERN(texture_shipped_info);
LOAD_EXTERN(texture_shipped_info);

} // namespace unravel

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
SERIALIZE_REGISTER_TYPE_WITH_NAME(unravel::texture_shipped_info, "texture_shipped_info")
