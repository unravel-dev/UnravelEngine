#pragma once

#include <serialization/serialization.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace unravel
{

/// One variant of a compiled .eshader: its name, the untouched shaderc output, and what gfx::shader::varyings holds. The
/// varyings are the file's, also those of an #if another variant takes, since only shaderc evaluates an #if.
struct compiled_shader_variant
{
    std::string name;
    std::vector<std::uint8_t> bytes;
    std::map<std::string, std::string> varyings;
};

SAVE_EXTERN(compiled_shader_variant);
LOAD_EXTERN(compiled_shader_variant);

void save_to_file_bin(const std::string& absolute_path, const std::vector<compiled_shader_variant>& variants);
void load_from_file_bin(const std::string& absolute_path, std::vector<compiled_shader_variant>& variants);

} // namespace unravel
