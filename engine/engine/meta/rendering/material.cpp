#include "material.hpp"

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT(material)
{
    entt::meta_factory<cull_type>{}
        .type("cull_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cull_type"},
            entt::attribute{"pretty_name", "Cull Type"},
        })
        .data<cull_type::none>("none"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "none"},
            entt::attribute{"pretty_name", "None"},
        })
        .data<cull_type::clockwise>("clockwise"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "clockwise"},
            entt::attribute{"pretty_name", "Clockwise"},
        })
        .data<cull_type::counter_clockwise>("counter_clockwise"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "counter_clockwise"},
            entt::attribute{"pretty_name", "Counter Clockwise"},
        });

    // Register material with entt
    entt::meta_factory<material>{}
        .type("material"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material"},
            entt::attribute{"pretty_name", "Material"},
        })
        .data<&material::set_cull_type, &material::get_cull_type>("cull_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cull_type"},
            entt::attribute{"pretty_name", "Cull Type"},
        })
        .func<&material::get_meta_type>("get_meta_type"_hs)
        .func<&material::get_static_meta_type>("get_static_meta_type"_hs)
        .func<&material::as_derived>("as_derived"_hs)
        ;
}

SAVE(material)
{
    try_save(ar, ser20::make_nvp("cull_type", obj.cull_type_));
    
    // Changes here should be reflected in ex::get_format_version<material>() in asset_extensions.h
}
SAVE_INSTANTIATE(material, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(material, ser20::oarchive_binary_t);

LOAD(material)
{
    try_load(ar, ser20::make_nvp("cull_type", obj.cull_type_));
    
    // Changes here should be reflected in ex::get_format_version<material>() in asset_extensions.h
}
LOAD_INSTANTIATE(material, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(material, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const std::shared_ptr<material>& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("material", obj));
    }
}

void save_to_file_bin(const std::string& absolute_path, const std::shared_ptr<material>& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("material", obj));
    }
}

void load_from_file(const std::string& absolute_path, std::shared_ptr<material>& obj)
{
    std::ifstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_iarchive_associative(stream);
        try_load(ar, ser20::make_nvp("material", obj));
    }
}

void load_from_file_bin(const std::string& absolute_path, std::shared_ptr<material>& obj)
{
    std::ifstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::iarchive_binary_t ar(stream);
        try_load(ar, ser20::make_nvp("material", obj));
    }
}

} // namespace unravel
