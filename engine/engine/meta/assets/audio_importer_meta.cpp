#include "asset_importer_meta.hpp"

#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/map.hpp>

namespace unravel
{

REFLECT(audio_importer_meta)
{
    entt::meta_factory<audio_importer_meta::compression_quality>{}
        .type("audio_compression_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "audio_compression_quality"},
            entt::attribute{"pretty_name", "Compression"},
        })
        .data<audio_importer_meta::compression_quality::project_default>("project_default"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "project_default"},
            entt::attribute{"pretty_name", "Project Default"},
        })
        .data<audio_importer_meta::compression_quality::none>("none"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "none"},
            entt::attribute{"pretty_name", "None"},
        })
        .data<audio_importer_meta::compression_quality::low_quality>("low_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "low_quality"},
            entt::attribute{"pretty_name", "Low Quality"},
        })
        .data<audio_importer_meta::compression_quality::normal_quality>("normal_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "normal_quality"},
            entt::attribute{"pretty_name", "Normal Quality"},
        })
        .data<audio_importer_meta::compression_quality::high_quality>("high_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "high_quality"},
            entt::attribute{"pretty_name", "High Quality"},
        });

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
        })
        .data<&audio_importer_meta::compression>("compression"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "compression"},
            entt::attribute{"pretty_name", "Compression"},
            entt::attribute{"tooltip",
                            "How a WAV or FLAC file is stored in the game: as it is (None), or compressed to Vorbis, "
                            "which makes it several times smaller and is decoded when the clip loads. A higher quality "
                            "keeps more detail and makes a larger file. Project Default uses the project settings' "
                            "default. OGG and MP3 files always keep their own bytes."},
        });
}

SAVE(audio_importer_meta)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<asset_importer_meta>(&obj)));
    try_save(ar, ser20::make_nvp("force_to_mono", obj.force_to_mono));
    try_save(ar, ser20::make_nvp("compression", obj.compression));
}
SAVE_INSTANTIATE(audio_importer_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(audio_importer_meta, ser20::oarchive_binary_t);

LOAD(audio_importer_meta)
{
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<asset_importer_meta>(&obj)));
    try_load(ar, ser20::make_nvp("force_to_mono", obj.force_to_mono));
    try_load(ar, ser20::make_nvp("compression", obj.compression));
}
LOAD_INSTANTIATE(audio_importer_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(audio_importer_meta, ser20::iarchive_binary_t);

} // namespace unravel
