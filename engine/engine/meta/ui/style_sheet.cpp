#include "style_sheet.hpp"
#include <filesystem/filesystem.h>
#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(style_sheet)
{
    // Register style_sheet with entt
    entt::meta_factory<style_sheet>{}
        .type("style_sheet"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "style_sheet"},
            entt::attribute{"pretty_name", "Style Sheet"},
            entt::attribute{"category", "UI"},
        })
        .data<&style_sheet::content>("content"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "content"},
            entt::attribute{"pretty_name", "Content"},
            entt::attribute{"tooltip", "The CSS/RCSS content of the style sheet"},
            entt::attribute{"multiline", true},
            entt::attribute{"wrap", true},
        });
}

SAVE(style_sheet)
{
    try_save(ar, ser20::make_nvp("content", obj.content));
}
SAVE_INSTANTIATE(style_sheet, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(style_sheet, ser20::oarchive_binary_t);

LOAD(style_sheet)
{
    try_load(ar, ser20::make_nvp("content", obj.content));
}
LOAD_INSTANTIATE(style_sheet, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(style_sheet, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const style_sheet::sptr& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        stream.write(obj->content.c_str(), obj->content.size());
    }
}

void save_to_file_bin(const std::string& absolute_path, const style_sheet::sptr& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("style_sheet", *obj));
    }
}

void load_from_file(const std::string& absolute_path, style_sheet::sptr& obj)
{
    std::ifstream stream(absolute_path);
    if(stream.good())
    {
        obj->content = fs::read_stream_str(stream);
    }
}

void load_from_file_bin(const std::string& absolute_path, style_sheet::sptr& obj)
{
    std::ifstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::iarchive_binary_t ar(stream);
        try_load(ar, ser20::make_nvp("style_sheet", *obj));
    }
}

} // namespace unravel
