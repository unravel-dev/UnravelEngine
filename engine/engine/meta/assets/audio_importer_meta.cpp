#include "asset_importer_meta.hpp"

#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/map.hpp>

namespace unravel
{

REFLECT(audio_importer_meta)
{
    // Register audio_importer_meta with entt
    entt::meta_factory<audio_importer_meta>{}
        .type("audio_importer_meta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "audio_importer_meta"},
            entt::attribute{"pretty_name", "Audio Importer Meta"},
        })
        .func<&audio_importer_meta::get_meta_type>("get_meta_type"_hs)
        .func<&audio_importer_meta::get_static_meta_type>("get_static_meta_type"_hs)
        .func<&audio_importer_meta::as_derived>("as_derived"_hs)
        .data<&audio_importer_meta::force_to_mono>("force_to_mono"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "force_to_mono"},
            entt::attribute{"pretty_name", "Force To Mono"},
        });
}

SAVE(audio_importer_meta)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<asset_importer_meta>(&obj)));
    try_save(ar, ser20::make_nvp("force_to_mono", obj.force_to_mono));
}
SAVE_INSTANTIATE(audio_importer_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(audio_importer_meta, ser20::oarchive_binary_t);

LOAD(audio_importer_meta)
{
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<asset_importer_meta>(&obj)));
    try_load(ar, ser20::make_nvp("force_to_mono", obj.force_to_mono));
}
LOAD_INSTANTIATE(audio_importer_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(audio_importer_meta, ser20::iarchive_binary_t);

} // namespace unravel
