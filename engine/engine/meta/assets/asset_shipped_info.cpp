#include "asset_shipped_info.hpp"

#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

SAVE(asset_shipped_info)
{
}
SAVE_INSTANTIATE(asset_shipped_info, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(asset_shipped_info, ser20::oarchive_binary_t);

LOAD(asset_shipped_info)
{
}
LOAD_INSTANTIATE(asset_shipped_info, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(asset_shipped_info, ser20::iarchive_binary_t);

//-----------------------------------------------------------------------------
SAVE(texture_shipped_info)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<asset_shipped_info>(&obj)));
    try_save(ar, ser20::make_nvp("normal_map", obj.normal_map));
}
SAVE_INSTANTIATE(texture_shipped_info, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(texture_shipped_info, ser20::oarchive_binary_t);

LOAD(texture_shipped_info)
{
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<asset_shipped_info>(&obj)));
    try_load(ar, ser20::make_nvp("normal_map", obj.normal_map));
}
LOAD_INSTANTIATE(texture_shipped_info, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(texture_shipped_info, ser20::iarchive_binary_t);

} // namespace unravel
