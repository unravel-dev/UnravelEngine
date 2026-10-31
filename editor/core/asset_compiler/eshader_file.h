#pragma once

#include <filesystem/filesystem.h>
#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <map>
#include <string>
#include <vector>

namespace unravel
{
namespace asset_compiler
{

/// A shader's stage, which its vs_, fs_ or cs_ name prefix gives.
enum class shader_stage
{
    vertex,
    fragment,
    compute,
};

/// One variant a .eshader declares: the name C++ asks for and what the code compiles with.
struct eshader_variant
{
    std::string name;
    /// NAME or NAME=VALUE.
    std::vector<std::string> defines;
};

/// One name a .eshader's [inputs] or [outputs] declares.
struct eshader_declaration
{
    /// flat, smooth, noperspective or centroid; empty for the default interpolation.
    std::string interpolation;
    std::string type;
    std::string name;
    /// A vertex output's value where the code does not write it; empty for zero.
    std::string default_value;
};

/**
 * @brief A parsed .eshader: a shader's variants, inputs, outputs and code in one file.
 *
 * shaderc cannot read the file. The compiler parses it and hands shaderc the parts, once per variant.
 *
 * @code
 * [variants]
 * default
 * skinned   SKINNED
 *
 * [inputs]
 * vec3 a_position
 * #if SKINNED
 * vec4 a_weight
 * vec4 a_indices
 * #endif
 *
 * [outputs]
 * vec2 v_texcoord0
 * vec3 v_normal = vec3(0.0, 0.0, 1.0)
 *
 * [code]
 * ...
 * @endcode
 *
 * - The file's vs_, fs_ or cs_ name prefix gives the stage. One file holds one stage.
 * - [code] is required, comes last and runs to the end of the file, so no code line can start a section. It holds no
 *   $input or $output lines: the compiler writes them from the sections.
 * - [variants] holds one variant per line: a name, then its defines. It must list default, the variant a shader is when
 *   nothing names one. Without the section the file has only that one, without defines.
 * - [inputs] and [outputs] hold one name per line: a type and a name, optionally after an interpolation such as flat.
 *   An integer varying must be flat. #if lines work as they do in code, so a variant can have its own. They hold no
 *   semantics: the compiler generates those.
 * - A vertex shader's [inputs] are what the mesh provides, so only the names get_vertex_input_tag knows are allowed.
 *   Its [outputs] start with v_ and may give a default with = value, which is zero otherwise.
 * - A fragment shader's [inputs] start with v_ and must declare the same names, the same way, as its vertex shader's
 *   [outputs]. It has no [outputs]. A compute shader has neither section.
 * - Blank lines and // comments are allowed everywhere before [code], also after a section's name.
 */
struct eshader_file
{
    std::vector<eshader_variant> variants;
    std::vector<eshader_declaration> declarations;
    /// What shaderc reads of the lines up to [code]: the $input, $output and #if lines, and an empty line for each of the
    /// others, so that every line stays on the line number it has in the file.
    std::string interface_lines;
    std::string code;
};

/**
 * @brief Parses the text of a .eshader of the given stage.
 * @param[out] error What is wrong and on which line, when this returns false.
 */
auto parse_eshader_file(hpp::string_view text, shader_stage stage, eshader_file& file, std::string& error) -> bool;

/**
 * @brief Reads and parses the .eshader at @p path, whose file name also gives its stage.
 * @param[out] error What is wrong, when this returns false.
 */
auto load_eshader_file(const fs::path& path, eshader_file& file, std::string& error) -> bool;

/// The stage a shader's name gives by its vs_, fs_ or cs_ prefix; nothing for another name.
auto get_shader_stage(hpp::string_view name) -> hpp::optional<shader_stage>;

/**
 * @brief The semantic a vertex input of this name binds to: bgfx's own list, so a_position is POSITION. Empty for a name
 *        bgfx has no vertex attribute or instance data slot for, which no mesh can feed.
 */
auto get_vertex_input_tag(hpp::string_view name) -> std::string;

/// The vertex inputs get_vertex_input_tag knows and what each maps to, one line each, for an error message.
auto describe_vertex_inputs() -> std::string;

/// The file shaderc reads for this file's code. A preprocessor error in it names @p source_path and the line it has there.
auto make_eshader_source_text(const eshader_file& file, const fs::path& source_path) -> std::string;

/// The varying file shaderc reads for this file, with the semantics generated.
auto make_eshader_varying_text(const eshader_file& file) -> std::string;

/// The varyings this file declares, each name with its type, after its interpolation if it has one: "flat vec4".
auto get_eshader_varyings(const eshader_file& file) -> std::map<std::string, std::string>;

} // namespace asset_compiler
} // namespace unravel
