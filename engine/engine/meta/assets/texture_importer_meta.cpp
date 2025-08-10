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
