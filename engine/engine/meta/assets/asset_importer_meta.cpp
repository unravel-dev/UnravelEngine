#include "asset_importer_meta.hpp"

#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/map.hpp>

namespace unravel
{
REFLECT(asset_importer_meta)
{
    // Register asset_importer_meta with entt
    entt::meta_factory<asset_importer_meta>{}
        .type("asset_importer_meta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "asset_importer_meta"},
            entt::attribute{"pretty_name", "Asset Importer Meta"},
        })
        .func<&asset_importer_meta::get_meta_type>("get_meta_type"_hs)
        .func<&asset_importer_meta::get_static_meta_type>("get_static_meta_type"_hs)
        .func<&asset_importer_meta::as_derived>("as_derived"_hs);
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
