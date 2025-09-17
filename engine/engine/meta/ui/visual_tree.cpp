#include "visual_tree.hpp"
#include <filesystem/filesystem.h>
#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(visual_tree)
{
    // Register visual_tree with entt
    entt::meta_factory<visual_tree>{}
        .type("visual_tree"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "visual_tree"},
            entt::attribute{"pretty_name", "Visual Tree"},
            entt::attribute{"category", "UI"},
        })
        .data<&visual_tree::content>("content"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "content"},
            entt::attribute{"pretty_name", "Content"},
            entt::attribute{"tooltip", "The HTML/RML content of the visual tree"},
            entt::attribute{"multiline", true},
            entt::attribute{"wrap", true},
        });
}

SAVE(visual_tree)
{
    try_save(ar, ser20::make_nvp("content", obj.content));
}
SAVE_INSTANTIATE(visual_tree, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(visual_tree, ser20::oarchive_binary_t);

LOAD(visual_tree)
{
    try_load(ar, ser20::make_nvp("content", obj.content));
}
LOAD_INSTANTIATE(visual_tree, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(visual_tree, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const visual_tree::sptr& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        stream.write(obj->content.c_str(), obj->content.size());
        // auto ar = ser20::create_oarchive_associative(stream);
        // try_save(ar, ser20::make_nvp("visual_tree", *obj));
    }
}

void save_to_file_bin(const std::string& absolute_path, const visual_tree::sptr& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("visual_tree", *obj));
    }
}

void load_from_file(const std::string& absolute_path, visual_tree::sptr& obj)
{
    std::ifstream stream(absolute_path);
    if(stream.good())
    {
        // auto ar = ser20::create_iarchive_associative(stream);
        // try_load(ar, ser20::make_nvp("visual_tree", *obj));
        //stream.read(obj->content.data(), obj->content.size());
        obj->content = fs::read_stream_str(stream);
    }
}

void load_from_file_bin(const std::string& absolute_path, visual_tree::sptr& obj)
{
    std::ifstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::iarchive_binary_t ar(stream);
        try_load(ar, ser20::make_nvp("visual_tree", *obj));
    }
}

} // namespace unravel
