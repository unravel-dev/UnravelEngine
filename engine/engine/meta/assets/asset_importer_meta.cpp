#include "asset_importer_meta.hpp"

#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/map.hpp>

namespace unravel
{
REFLECT(asset_importer_meta)
{
    rttr::registration::class_<asset_importer_meta>("asset_importer_meta");

    // Register asset_importer_meta with entt
    entt::meta_factory<asset_importer_meta>{}
        .type("asset_importer_meta"_hs);
}

SAVE(asset_importer_meta)
{
}
SAVE_INSTANTIATE(asset_importer_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(asset_importer_meta, ser20::oarchive_binary_t);

LOAD(asset_importer_meta)
{
}
LOAD_INSTANTIATE(asset_importer_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(asset_importer_meta, ser20::iarchive_binary_t);


} // namespace unravel
