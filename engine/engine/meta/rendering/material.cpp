#include "material.hpp"

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT(material)
{
    rttr::registration::enumeration<cull_type>("cull_type")(
        rttr::value("None", cull_type::none),
        rttr::value("Clockwise", cull_type::clockwise),
        rttr::value("Counter Clockwise", cull_type::counter_clockwise));

    rttr::registration::class_<material>("material")
        .property("cull_type", &material::get_cull_type, &material::set_cull_type)(
            rttr::metadata("pretty_name", "Cull Type"));

    // Register cull_type enum with entt
    entt::meta_factory<cull_type>{}
        .type("cull_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cull_type"},
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
        })
        .data<&material::set_cull_type, &material::get_cull_type>("cull_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cull_type"},
            entt::attribute{"pretty_name", "Cull Type"},
        });
}

SAVE(material)
{
    try_save(ar, ser20::make_nvp("cull_type", obj.cull_type_));
}
SAVE_INSTANTIATE(material, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(material, ser20::oarchive_binary_t);

LOAD(material)
{
    try_load(ar, ser20::make_nvp("cull_type", obj.cull_type_));
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
