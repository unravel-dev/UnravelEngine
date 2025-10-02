#include "ui_tree.hpp"
#include <filesystem/filesystem.h>
#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(ui_tree)
{
    // Register ui_tree with entt
    entt::meta_factory<ui_tree>{}
        .type("ui_tree"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ui_tree"},
            entt::attribute{"pretty_name", "Visual Tree"},
            entt::attribute{"category", "UI"},
        })
        .data<&ui_tree::content>("content"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "content"},
            entt::attribute{"pretty_name", "Content"},
            entt::attribute{"tooltip", "The HTML/RML content of the visual tree"},
            entt::attribute{"multiline", true},
            entt::attribute{"wrap", true},
        });
}

SAVE(ui_tree)
{
    try_save(ar, ser20::make_nvp("content", obj.content));
}
SAVE_INSTANTIATE(ui_tree, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ui_tree, ser20::oarchive_binary_t);

LOAD(ui_tree)
{
    try_load(ar, ser20::make_nvp("content", obj.content));
}
LOAD_INSTANTIATE(ui_tree, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ui_tree, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const ui_tree::sptr& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        stream.write(obj->content.c_str(), obj->content.size());
    }
}

void save_to_file_bin(const std::string& absolute_path, const ui_tree::sptr& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("ui_tree", *obj));
    }
}

void load_from_file(const std::string& absolute_path, ui_tree::sptr& obj)
{
    std::ifstream stream(absolute_path);
    if(stream.good())
    {
        obj->content = fs::read_stream_str(stream);
    }
}

void load_from_file_bin(const std::string& absolute_path, ui_tree::sptr& obj)
{
    std::ifstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::iarchive_binary_t ar(stream);
        try_load(ar, ser20::make_nvp("ui_tree", *obj));
    }
}

} // namespace unravel
