#include "shader_variants.hpp"

#include <filesystem/pack.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <engine/meta/core/compiled_file.hpp>
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
    write_compiled_file(absolute_path, "variants", variants);
}

void load_from_file_bin(const std::string& absolute_path, std::vector<compiled_shader_variant>& variants)
{
    load_compiled_file(absolute_path, "variants", variants);
}

} // namespace unravel
