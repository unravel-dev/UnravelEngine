#include "shader_variants.hpp"

#include <filesystem/pack.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/map.hpp>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

#include <fstream>

namespace unravel
{

SAVE(compiled_shader_variant)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("bytes", obj.bytes));
    try_save(ar, ser20::make_nvp("varyings", obj.varyings));
}
SAVE_INSTANTIATE(compiled_shader_variant, ser20::oarchive_binary_t);

LOAD(compiled_shader_variant)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("bytes", obj.bytes));
    try_load(ar, ser20::make_nvp("varyings", obj.varyings));
}
LOAD_INSTANTIATE(compiled_shader_variant, ser20::iarchive_binary_t);

void save_to_file_bin(const std::string& absolute_path, const std::vector<compiled_shader_variant>& variants)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("variants", variants));
    }
}

void load_from_file_bin(const std::string& absolute_path, std::vector<compiled_shader_variant>& variants)
{
    auto input = fs::open_readable_stream(absolute_path);
    if(!input)
    {
        return;
    }
    ser20::iarchive_binary_t ar(*input);
    try_load(ar, ser20::make_nvp("variants", variants));
}

} // namespace unravel
