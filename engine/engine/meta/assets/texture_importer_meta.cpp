#include "asset_importer_meta.hpp"

#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/map.hpp>

namespace unravel
{

REFLECT(texture_importer_meta)
{
    rttr::registration::enumeration<texture_importer_meta::texture_size>("texture_size")(
        rttr::value("Project Default", texture_importer_meta::texture_size::project_default),
        rttr::value("32", texture_importer_meta::texture_size::size_32),
        rttr::value("64", texture_importer_meta::texture_size::size_64),
        rttr::value("128", texture_importer_meta::texture_size::size_128),
        rttr::value("256", texture_importer_meta::texture_size::size_256),
        rttr::value("512", texture_importer_meta::texture_size::size_512),
        rttr::value("1024", texture_importer_meta::texture_size::size_1024),
        rttr::value("2048", texture_importer_meta::texture_size::size_2048),
        rttr::value("4096", texture_importer_meta::texture_size::size_4096),
        rttr::value("8192", texture_importer_meta::texture_size::size_8192),
        rttr::value("16384", texture_importer_meta::texture_size::size_16384)
        );

    rttr::registration::enumeration<texture_importer_meta::texture_type>("texture_type")(
        rttr::value("Auto", texture_importer_meta::texture_type::automatic),
        rttr::value("Normal Map", texture_importer_meta::texture_type::normal_map),
        rttr::value("Equirect. Proj.", texture_importer_meta::texture_type::equirect));

    rttr::registration::enumeration<texture_importer_meta::compression_quality>("compression_quality")(
        rttr::value("Project Default", texture_importer_meta::compression_quality::project_default),
        rttr::value("None", texture_importer_meta::compression_quality::none),
        rttr::value("Low Quality", texture_importer_meta::compression_quality::low_quality),
        rttr::value("Normal Quality", texture_importer_meta::compression_quality::normal_quality),
        rttr::value("High Quality", texture_importer_meta::compression_quality::high_quality)
        );

    rttr::registration::class_<texture_importer_meta::quality_meta>("quality_meta")
        .property("max_size", &texture_importer_meta::quality_meta::max_size)(rttr::metadata("pretty_name", "Max Size"))
        .property("compression",
                  &texture_importer_meta::quality_meta::compression)(rttr::metadata("pretty_name", "Compression"));

    rttr::registration::class_<texture_importer_meta>("texture_importer_meta")
        .property("type", &texture_importer_meta::type)(rttr::metadata("pretty_name", "Type"))
        .property("generate_mipmaps",
                  &texture_importer_meta::generate_mipmaps)(rttr::metadata("pretty_name", "Generate Mipmaps"))
        .property("quality", &texture_importer_meta::quality)(rttr::metadata("pretty_name", "Quality"));

    // Register texture_importer_meta::texture_size enum with entt
    entt::meta_factory<texture_importer_meta::texture_size>{}
        .type("texture_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_size"},
        })
        .data<texture_importer_meta::texture_size::project_default>("project_default"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "project_default"},
            entt::attribute{"pretty_name", "Project Default"},
        })
        .data<texture_importer_meta::texture_size::size_32>("size_32"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_32"},
            entt::attribute{"pretty_name", "32"},
        })
        .data<texture_importer_meta::texture_size::size_64>("size_64"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_64"},
            entt::attribute{"pretty_name", "64"},
        })
        .data<texture_importer_meta::texture_size::size_128>("size_128"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_128"},
            entt::attribute{"pretty_name", "128"},
        })
        .data<texture_importer_meta::texture_size::size_256>("size_256"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_256"},
            entt::attribute{"pretty_name", "256"},
        })
        .data<texture_importer_meta::texture_size::size_512>("size_512"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_512"},
            entt::attribute{"pretty_name", "512"},
        })
        .data<texture_importer_meta::texture_size::size_1024>("size_1024"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_1024"},
            entt::attribute{"pretty_name", "1024"},
        })
        .data<texture_importer_meta::texture_size::size_2048>("size_2048"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_2048"},
            entt::attribute{"pretty_name", "2048"},
        })
        .data<texture_importer_meta::texture_size::size_4096>("size_4096"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_4096"},
            entt::attribute{"pretty_name", "4096"},
        })
        .data<texture_importer_meta::texture_size::size_8192>("size_8192"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_8192"},
            entt::attribute{"pretty_name", "8192"},
        })
        .data<texture_importer_meta::texture_size::size_16384>("size_16384"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_16384"},
            entt::attribute{"pretty_name", "16384"},
        });

    // Register texture_importer_meta::texture_type enum with entt
    entt::meta_factory<texture_importer_meta::texture_type>{}
        .type("texture_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_type"},
        })
        .data<texture_importer_meta::texture_type::automatic>("automatic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "automatic"},
            entt::attribute{"pretty_name", "Auto"},
        })
        .data<texture_importer_meta::texture_type::normal_map>("normal_map"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "normal_map"},
            entt::attribute{"pretty_name", "Normal Map"},
        })
        .data<texture_importer_meta::texture_type::equirect>("equirect"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "equirect"},
            entt::attribute{"pretty_name", "Equirect. Proj."},
        });

    // Register texture_importer_meta::compression_quality enum with entt
    entt::meta_factory<texture_importer_meta::compression_quality>{}
        .type("compression_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "compression_quality"},
        })
        .data<texture_importer_meta::compression_quality::project_default>("project_default"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "project_default"},
            entt::attribute{"pretty_name", "Project Default"},
        })
        .data<texture_importer_meta::compression_quality::none>("none"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "none"},
            entt::attribute{"pretty_name", "None"},
        })
        .data<texture_importer_meta::compression_quality::low_quality>("low_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "low_quality"},
            entt::attribute{"pretty_name", "Low Quality"},
        })
        .data<texture_importer_meta::compression_quality::normal_quality>("normal_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "normal_quality"},
            entt::attribute{"pretty_name", "Normal Quality"},
        })
        .data<texture_importer_meta::compression_quality::high_quality>("high_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "high_quality"},
            entt::attribute{"pretty_name", "High Quality"},
        });

    // Register texture_importer_meta::quality_meta with entt
    entt::meta_factory<texture_importer_meta::quality_meta>{}
        .type("quality_meta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "quality_meta"},
        })
        .data<&texture_importer_meta::quality_meta::max_size>("max_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_size"},
            entt::attribute{"pretty_name", "Max Size"},
        })
        .data<&texture_importer_meta::quality_meta::compression>("compression"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "compression"},
            entt::attribute{"pretty_name", "Compression"},
        });

    // Register texture_importer_meta with entt
    entt::meta_factory<texture_importer_meta>{}
        .type("texture_importer_meta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_importer_meta"},
        })
        .data<&texture_importer_meta::type>("type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "type"},
            entt::attribute{"pretty_name", "Type"},
        })
        .data<&texture_importer_meta::generate_mipmaps>("generate_mipmaps"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "generate_mipmaps"},
            entt::attribute{"pretty_name", "Generate Mipmaps"},
        })
        .data<&texture_importer_meta::quality>("quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "quality"},
            entt::attribute{"pretty_name", "Quality"},
        });
}

SAVE(texture_importer_meta::quality_meta)
{
    try_save(ar, ser20::make_nvp("max_size", obj.max_size));
    try_save(ar, ser20::make_nvp("compression", obj.compression));
}
SAVE_INSTANTIATE(texture_importer_meta::quality_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(texture_importer_meta::quality_meta, ser20::oarchive_binary_t);

LOAD(texture_importer_meta::quality_meta)
{
    try_load(ar, ser20::make_nvp("max_size", obj.max_size));
    try_load(ar, ser20::make_nvp("compression", obj.compression));
}
LOAD_INSTANTIATE(texture_importer_meta::quality_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(texture_importer_meta::quality_meta, ser20::iarchive_binary_t);

SAVE(texture_importer_meta)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<asset_importer_meta>(&obj)));
    try_save(ar, ser20::make_nvp("type", obj.type));
    try_save(ar, ser20::make_nvp("generate_mipmaps", obj.generate_mipmaps));
    try_save(ar, ser20::make_nvp("quality", obj.quality));
}
SAVE_INSTANTIATE(texture_importer_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(texture_importer_meta, ser20::oarchive_binary_t);

LOAD(texture_importer_meta)
{
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<asset_importer_meta>(&obj)));
    try_load(ar, ser20::make_nvp("type", obj.type));
    try_load(ar, ser20::make_nvp("generate_mipmaps", obj.generate_mipmaps));
    try_load(ar, ser20::make_nvp("quality", obj.quality));
}
LOAD_INSTANTIATE(texture_importer_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(texture_importer_meta, ser20::iarchive_binary_t);

} // namespace unravel
