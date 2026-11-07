/*
 * Validation suite for .eshader files: a shader's variants, inputs, outputs and code in one file, compiled to one file
 * per renderer holding a shaderc binary per variant.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "eshader files"
 *
 * This pins:
 *   - the parser: sections, the default variant, comments, a BOM, CRLF, lines keeping their numbers, and every error;
 *   - vertex inputs: every name bgfx binds and nothing else, the error listing what is supported and what it maps to,
 *     and the table agreeing with bgfx's count;
 *   - what the compiler generates: the $input and $output lines, #if lines kept, semantics, zero defaults, what a
 *     program compares of its two stages;
 *   - the format: is_format, the .sh trap, the compiled file's mapping back to its source, deploy's paths;
 *   - gfx::shader::find_variant: the default is the shader itself;
 *   - a program's stages: a varying declared two ways is found;
 *   - dependencies: an include in the code counts, also one among the engine's shaders;
 *   - a compiled file reads back from a compressed pack, the way a deployed game finds it;
 *   - the compile: each variant's binary equals a file that defines the same in its code on every renderer of this
 *     platform, every declaration type compiles, a failed variant leaves the compiled file and its manifest as
 *     they were and no temp file behind;
 *   - every .eshader the engine and the editor ship compiles on every renderer of this platform, and no two of a file's
 *     variants are the same binary on all of them: a define that changes nothing is what a mistyped one looks like;
 *   - every engine shader compiles to WGSL within the limits every WebGPU device grants (invocations per workgroup
 *     and along each axis, storage textures per stage), but the global illumination's, which the browser player never
 *     runs; a compute shader whose workgroup size the check cannot read fails it.
 * Everything lives in a temp folder and the tests' app protocol.
 */

#include "../tests.h"

#include <asset_compiler/asset_compiler.h>
#include <asset_compiler/asset_dependencies.h>
#include <asset_compiler/asset_manifest.h>
#include <asset_compiler/eshader_file.h>
#include <deploy/files.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/meta/rendering/shader_variants.hpp>
#include <engine/rendering/gpu_program.h>
#include <filesystem/filesystem.h>
#include <filesystem/pack.h>
#include <graphics/graphics.h>
#include <graphics/shader.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace
{
namespace ac = unravel::asset_compiler;
using ac::shader_stage;
using compiled_variants = std::vector<unravel::compiled_shader_variant>;

/// The folder under the tests' app protocol the shader sources are written to.
constexpr const char* SHADER_FOLDER = "data/eshader_tests";
/// bgfx's instance data slots (BGFX_CONFIG_MAX_INSTANCE_DATA_COUNT) and the tag the first one binds to.
constexpr int INSTANCE_SLOT_COUNT = 16;
constexpr int FIRST_INSTANCE_TAG_INDEX = 31;
/// bgfx's numbered vertex attributes.
constexpr int COLOR_ATTRIBUTE_COUNT = 4;
constexpr int TEXCOORD_ATTRIBUTE_COUNT = 16;

/// WebGPU's core limits, which every device grants, as the browser player's shaders must fit them.
constexpr unsigned long WEBGPU_MAX_WORKGROUP_INVOCATIONS = 256;
constexpr std::array<unsigned long, 3> WEBGPU_MAX_WORKGROUP_SIZE = {256, 256, 64};
constexpr std::size_t WEBGPU_MAX_STORAGE_TEXTURES_PER_STAGE = 4;
/// The engine's shaders the browser player never runs: global illumination reads and writes RG11B10F storage
/// textures, which core WebGPU does not allow and only some devices offer, so web profiles turn it off.
constexpr const char* WEBGPU_EXEMPT_FOLDER = "gi";

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto get_temp_folder() -> fs::path
{
    return fs::temp_directory_path() / "unravel-tests-eshader-files";
}

void write_text(const fs::path& file, const std::string& text)
{
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

auto read_bytes(const fs::path& file) -> std::string
{
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

auto contains_text(const std::string& text, const std::string& part) -> bool
{
    return text.find(part) != std::string::npos;
}

auto parse_error(const std::string& text, shader_stage stage) -> std::string
{
    ac::eshader_file file;
    std::string error;
    return ac::parse_eshader_file(text, stage, file, error) ? std::string{} : error;
}

void check_refused(const std::string& text, shader_stage stage, const std::string& expected_in_error, const std::string& what)
{
    const auto error = parse_error(text, stage);
    check(contains_text(error, expected_in_error), what + " (the error reads: " + error + ")");
}

void test_parse_sections()
{
    std::printf("test_parse_sections\n");
    const std::string text = "// A comment before the sections.\n"
                             "\n"
                             "[variants] // A comment after a section's name.\n"
                             "// A comment between the variants.\n"
                             "default\n"
                             "spot.pcf.tent3   SPOT_LIGHT PCF_TENT_SIZE=3 // A comment after the defines.\n"
                             "\n"
                             "[inputs]\n"
                             "vec3 a_position // A comment after a name.\n"
                             "#if SPOT_LIGHT\n"
                             "vec4 a_weight\n"
                             "#endif\n"
                             "\n"
                             "[outputs]\n"
                             "// A comment between the names.\n"
                             "vec2 v_texcoord0\n"
                             "flat vec4 v_material0 = vec4(1.0, 1.0, 1.0, 1.0)\n"
                             "\n"
                             "[code] // And after this one.\n"
                             "void main() {}\n"
                             "[inputs]\n";
    ac::eshader_file file;
    std::string error;
    check(ac::parse_eshader_file(text, shader_stage::vertex, file, error), "a file with every section parses: " + error);
    check(file.variants.size() == 2 && file.variants[0].name == "default" && file.variants[0].defines.empty(),
          "a name alone is a variant without defines");
    check(file.variants.size() == 2 && file.variants[1].name == "spot.pcf.tent3" &&
              file.variants[1].defines == std::vector<std::string>{"SPOT_LIGHT", "PCF_TENT_SIZE=3"},
          "the other words are the variant's defines, up to a comment");
    check(file.declarations.size() == 4, "every name is declared once");
    check(file.interface_lines == "\n\n\n\n\n\n\n\n$input a_position\n#if SPOT_LIGHT\n$input a_weight\n#endif\n\n\n\n"
                                  "$output v_texcoord0\n$output v_material0\n\n\n",
          "the interface lines keep the #if lines and the order, each on the line it has in the file: " + file.interface_lines);
    if(file.declarations.size() == 4)
    {
        const auto& material = file.declarations[3];
        check(material.interpolation == "flat" && material.type == "vec4" && material.name == "v_material0" &&
                  material.default_value == "vec4(1.0, 1.0, 1.0, 1.0)",
              "a declaration keeps its interpolation, type, name and default");
    }
    check(file.code == "void main() {}\n[inputs]\n", "[code] runs to the end of the file, whatever a line looks like");
}

void test_parse_defaults()
{
    std::printf("test_parse_defaults\n");
    ac::eshader_file file;
    std::string error;
    check(ac::parse_eshader_file("[code]\nvoid main() {}\n", shader_stage::compute, file, error), "[code] alone is a file: " + error);
    check(file.variants.size() == 1 && file.variants[0].name == gfx::shader::DEFAULT_VARIANT && file.variants[0].defines.empty(),
          "without [variants] the file has the default variant");
    check(file.declarations.empty() && file.interface_lines == "\n", "without sections it declares nothing");
    check(ac::parse_eshader_file("\xEF\xBB\xBF[code]\nvoid main() {}\n", shader_stage::compute, file, error), "a BOM is no part of the first line: " + error);
    check(ac::parse_eshader_file("[variants]\r\ndefault\r\nspot\tSPOT\r\n\r\n[inputs]\r\nvec2 v_uv\r\n\r\n[code]\r\nvoid main() {}\r\n",
                                 shader_stage::fragment,
                                 file,
                                 error),
          "CRLF parses: " + error);
    check(file.variants.size() == 2 && file.variants[1].name == "spot" && file.variants[1].defines == std::vector<std::string>{"SPOT"},
          "no line ending is left on a define");
    check(file.declarations.size() == 1 && file.declarations[0].name == "v_uv", "no line ending is left on a name");
    check(file.interface_lines == "\n\n\n\n\n$input v_uv\n\n\n", "CRLF lines count once");
}

void test_parse_errors()
{
    std::printf("test_parse_errors\n");
    const auto fragment = shader_stage::fragment;
    check_refused("", fragment, "no [code]", "an empty file has no code");
    check_refused("[variants]\ndefault\n", fragment, "no [code]", "a file needs [code]");
    check_refused("[code]\n\n", fragment, "is empty", "[code] needs code");
    check_refused("void main() {}\n", fragment, "before the first section", "code before the sections is refused");
    check_refused("[varyings]\nvec2 v_uv : TEXCOORD0;\n[code]\nx\n", fragment, "not a section", "an unknown section is refused");
    check_refused("[inputs]\n[inputs]\n[code]\nx\n", fragment, "appears twice", "[inputs] twice is refused");
    check_refused("[variants]\na\n[variants]\nb\n[code]\nx\n", fragment, "appears twice", "[variants] twice is refused");
    check_refused("[variants]\n[code]\nx\n", fragment, "must list default", "an empty [variants] is refused");
    check_refused("[variants]\nspot SPOT\n[code]\nx\n", fragment, "must list default", "a file has a default variant");
    check_refused("[variants]\na\na B\n[code]\nx\n", fragment, "declared twice", "a repeated variant is refused");
    check_refused("[variants]\na-b\n[code]\nx\n", fragment, "variant name", "a name with other characters is refused");
    check_refused("[variants]\na B;C\n[code]\nx\n", fragment, "not NAME", "a ';' would split a define in two");
    check_refused("[variants]\na 1B\n[code]\nx\n", fragment, "not NAME", "a define starts like an identifier");
    check_refused("[variants]\na B=\n[code]\nx\n", fragment, "not NAME", "an '=' needs a value");
    check_refused("\n\nvoid main() {}\n", fragment, "line 3", "an error names its line");
    check_refused("[inputs]\nvec2 v_uv\n[code]\n$input v_uv\nvoid main() {}\n", fragment, "belongs in", "$input in the code is refused");
    check_refused("[code]\nvoid main() {}\n$output v_uv\n", shader_stage::vertex, "line 3", "that error names the code's line");
    check_refused("[outputs]\nvec2 v_uv\n[code]\nx\n", fragment, "no [outputs]", "a fragment shader has no outputs");
    check_refused("[inputs]\nvec2 v_uv\n[code]\nx\n", shader_stage::compute, "compute shader", "a compute shader has no inputs");
    check_refused("[outputs]\nvec2 v_uv\n[code]\nx\n", shader_stage::compute, "compute shader", "or outputs");
    check_refused("[inputs]\nvec2 v_uv : TEXCOORD0\n[code]\nx\n", fragment, "generated", "a tag is refused, with why");
    check_refused("[inputs]\nvec2 v_uv;\n[code]\nx\n", fragment, "no ';'", "a ';' is refused");
    check_refused("[inputs]\nv_uv\n[code]\nx\n", fragment, "type and a name", "a name alone is refused");
    check_refused("[inputs]\nmat4 v_uv\n[code]\nx\n", fragment, "not a type", "an unknown type is refused");
    check_refused("[inputs]\nsoft vec2 v_uv\n[code]\nx\n", fragment, "not an interpolation", "an unknown interpolation is refused");
    check_refused("[inputs]\nvec2 v-uv\n[code]\nx\n", fragment, "not a name", "a bad name is refused");
    check_refused("[inputs]\nvec2 a_position\n[code]\nx\n", fragment, "must start with v_", "a fragment shader reads no mesh data");
    check_refused("[outputs]\nvec2 uv\n[code]\nx\n", shader_stage::vertex, "must start with v_", "a vertex output starts with v_");
    check_refused("[inputs]\nvec2 v_uv = vec2(1.0, 1.0)\n[code]\nx\n", fragment, "default", "a fragment input has no default");
    check_refused("[inputs]\nvec2 v_uv\nvec2 v_uv\n[code]\nx\n", fragment, "declared twice", "a repeated name is refused");
    check_refused("[outputs]\nvec2 v_uv =\n[code]\nx\n", shader_stage::vertex, "without a value", "an '=' needs a value");
    check_refused("[inputs]\nint v_id\n[code]\nx\n", fragment, "must be flat", "an integer varying must be flat");
    check_refused("[inputs]\nsmooth uvec2 v_id\n[code]\nx\n", fragment, "must be flat", "whatever else it says");
    check(parse_error("[inputs]\nflat int v_id\n[code]\nx\n", fragment).empty(), "a flat integer parses");
    check_refused("[inputs]\nvec2 v_uv\nvec2 v_UV\n[code]\nx\n", fragment, "share one", "a tag ignores case");
    check_refused("[inputs]\nvec2 v_a1\nvec2 v_a01\n[code]\nx\n", fragment, "share one", "a tag's index ignores leading zeros");
    check_refused("[inputs]\nvec2 v_a\nvec2 v_a0\n[code]\nx\n", fragment, "share one", "a tag without an index has index zero");
    check(parse_error("[inputs]\nvec2 v_a0\nvec2 v_a1\nvec2 v_a10\nvec2 v_a100\nvec2 v_b\n[code]\nx\n", fragment).empty(), "other indices have their own tags");
    check_refused("[inputs]\n#if A\nvec2 v_uv\n#else\nvec3 v_uv\n#endif\n[code]\nx\n", fragment, "must agree", "branches must agree on a name");
    check_refused("[inputs]\n#if A\nvec2 v_uv\n[code]\nx\n", fragment, "not closed", "an open #if before [code] is refused");
    check_refused("[inputs]\n#if A\n[variants]\nb\n[code]\nx\n", fragment, "not closed", "an open #if before a section is refused");
    check_refused("[inputs]\n#endif\n[code]\nx\n", fragment, "no #if", "an #endif without #if is refused");
    check_refused("[inputs]\n#else\n[code]\nx\n", fragment, "no #if", "an #else without #if is refused");
    check_refused("[inputs]\n#include \"a.sh\"\n[code]\nx\n", fragment, "cannot appear here", "other directives are refused");
    check(parse_error("[inputs]\n#ifdef A\nvec2 v_a\n#elif B\nvec2 v_b\n#else\nvec2 v_c\n#endif\n[code]\nx\n", fragment).empty(),
          "every condition form parses");
    check(parse_error("[inputs]\n#if A\nvec2 v_uv\n#else\nvec2 v_uv\nvec2 v_other\n#endif\n[code]\nx\n", fragment).empty(),
          "branches may repeat a name they agree on");
    ac::eshader_file file;
    std::string error;
    const auto root = get_temp_folder() / "stage";
    write_text(root / "blit.eshader", "[code]\nvoid main() {}\n");
    check(!ac::load_eshader_file(root / "blit.eshader", file, error) && contains_text(error, "vs_, fs_ or cs_"),
          "a file name that gives no stage is refused");
    check(!ac::load_eshader_file(root / "fs_missing.eshader", file, error), "a missing file is refused");
}

void test_vertex_inputs()
{
    std::printf("test_vertex_inputs\n");
    check(ac::get_vertex_input_tag("a_position") == "POSITION" && ac::get_vertex_input_tag("a_normal") == "NORMAL" &&
              ac::get_vertex_input_tag("a_tangent") == "TANGENT" && ac::get_vertex_input_tag("a_bitangent") == "BITANGENT" &&
              ac::get_vertex_input_tag("a_indices") == "BLENDINDICES" && ac::get_vertex_input_tag("a_weight") == "BLENDWEIGHT",
          "the single names map to their tags");
    check(ac::get_vertex_input_tag("a_color3") == "COLOR3" && ac::get_vertex_input_tag("a_texcoord15") == "TEXCOORD15",
          "the numbered names map to their own number");
    for(int slot = 0; slot < INSTANCE_SLOT_COUNT; ++slot)
    {
        check(ac::get_vertex_input_tag("i_data" + std::to_string(slot)) == "TEXCOORD" + std::to_string(FIRST_INSTANCE_TAG_INDEX - slot),
              "instance data counts down from the first slot's tag");
    }
    for(const auto* name : {"a_myname", "a_color4", "a_color", "a_texcoord16", "a_texcoord", "a_color00", "i_data16", "i_data", "v_uv", "position", ""})
    {
        check(ac::get_vertex_input_tag(name).empty(), std::string("no tag for '") + name + "'");
    }
    std::vector<std::string> attributes = {"a_position", "a_normal", "a_tangent", "a_bitangent", "a_indices", "a_weight"};
    for(int index = 0; index < COLOR_ATTRIBUTE_COUNT; ++index)
    {
        attributes.push_back("a_color" + std::to_string(index));
    }
    for(int index = 0; index < TEXCOORD_ATTRIBUTE_COUNT; ++index)
    {
        attributes.push_back("a_texcoord" + std::to_string(index));
    }
    check(int(attributes.size()) == bgfx::Attrib::Count, "these are all of bgfx's attributes");
    for(const auto& attribute : attributes)
    {
        check(!ac::get_vertex_input_tag(attribute).empty(), attribute + " has a tag");
    }
    const auto error = parse_error("[inputs]\nvec2 a_myname\n[code]\nx\n", shader_stage::vertex);
    check(contains_text(error, "line 2") && contains_text(error, "a_myname"), "the error names the line and the name");
    for(const auto* mapping : {"a_position -> POSITION", "a_normal -> NORMAL", "a_tangent -> TANGENT", "a_bitangent -> BITANGENT",
                               "a_color0 to a_color3 -> COLOR0 to COLOR3", "a_indices -> BLENDINDICES", "a_weight -> BLENDWEIGHT",
                               "a_texcoord0 to a_texcoord15 -> TEXCOORD0 to TEXCOORD15", "i_data0 to i_data15 -> TEXCOORD31 to TEXCOORD16"})
    {
        check(contains_text(error, mapping), std::string("the error lists what is supported: ") + mapping);
    }
    check_refused("[inputs]\nvec2 v_uv\n[code]\nx\n", shader_stage::vertex, "a_position -> POSITION", "a varying is no vertex input");
    check_refused("[inputs]\nvec3 a_position = vec3(0.0, 0.0, 0.0)\n[code]\nx\n", shader_stage::vertex, "no interpolation or default", "a vertex input has no default");
    check_refused("[inputs]\nflat vec3 a_position\n[code]\nx\n", shader_stage::vertex, "no interpolation or default", "or interpolation");
    check_refused("[inputs]\nvec3 i_data0\n[code]\nx\n", shader_stage::vertex, "must be vec4", "instance data is vec4");
    check(parse_error("[inputs]\nuvec4 a_indices\nvec2 a_position\nvec4 i_data15\n[code]\nx\n", shader_stage::vertex).empty(),
          "the types the mesh attributes use today parse");
}

void test_generated_varyings()
{
    std::printf("test_generated_varyings\n");
    ac::eshader_file vertex;
    std::string error;
    check(ac::parse_eshader_file("[inputs]\n"
                                 "vec3 a_position\n"
                                 "vec4 i_data2\n"
                                 "[outputs]\n"
                                 "vec2 v_myname\n"
                                 "flat vec4 v_material0 = vec4(1.0, 1.0, 1.0, 1.0)\n"
                                 "float v_fade\n"
                                 "[code]\nx\n",
                                 shader_stage::vertex,
                                 vertex,
                                 error),
          "the file parses: " + error);
    check(ac::make_eshader_varying_text(vertex) == "// Generated from [inputs] and [outputs].\n"
                                                   "vec3 a_position : POSITION;\n"
                                                   "vec4 i_data2 : TEXCOORD29;\n"
                                                   "vec2 v_myname : V_MYNAME = vec2(0.0, 0.0);\n"
                                                   "flat vec4 v_material0 : V_MATERIAL0 = vec4(1.0, 1.0, 1.0, 1.0);\n"
                                                   "float v_fade : V_FADE = 0.0;\n",
          "tags come from the table and from the name, and a default is zero unless given: " + ac::make_eshader_varying_text(vertex));
    const std::map<std::string, std::string> expected_varyings = {{"v_fade", "float"}, {"v_material0", "flat vec4"}, {"v_myname", "vec2"}};
    check(ac::get_eshader_varyings(vertex) == expected_varyings, "a stage's varyings leave out the vertex inputs, and keep the interpolation");
    check(ac::make_eshader_source_text(vertex, "c:/shaders/vs_a.eshader") == "#line 1 \"c:/shaders/vs_a.eshader\"\n" + vertex.interface_lines + "x\n",
          "what shaderc reads names the source and starts on its first line");
    ac::eshader_file fragment;
    check(ac::parse_eshader_file("[inputs]\nvec2 v_myname\n[code]\nx\n", shader_stage::fragment, fragment, error), "the other stage parses: " + error);
    check(contains_text(ac::make_eshader_varying_text(fragment), "v_myname : V_MYNAME"), "and gets the same tag for the same name");
}

void test_format()
{
    std::printf("test_format\n");
    check(ex::is_format<gfx::shader>(".eshader") && !ex::is_format<gfx::shader>(".sc"), ".eshader is the shader format");
    check(!ex::is_format<gfx::shader>(".sh"), "a .sh header is not a shader: is_format matches by substring");
    check(ex::get_format<gfx::shader>() == ".eshader", "a key without an extension gets it");
    check(ex::get_type(".eshader") == ex::get_type<gfx::shader>() && !ex::is_binary(".eshader"), "it is a text shader");
    check(ex::get_source_path_from_compiled("c/x.eshader.asset.dxbc") == fs::path("c/x.eshader"),
          "a compiled file maps back to its source");
    const fs::path root = "root";
    const auto paths = unravel::deployment::get_compiled_paths("app:/data/shaders/fs_a.eshader", root, {".spirv"});
    check(paths == std::vector<fs::path>{root / "compiled/shaders/fs_a.eshader.asset.spirv"}, "deploy ships the renderer's one file");
}

void test_find_variant()
{
    std::printf("test_find_variant\n");
    gfx::shader plain;
    check(plain.find_variant(gfx::shader::DEFAULT_VARIANT) == &plain, "a shader without variants is its own default");
    check(plain.find_variant("skinned") == nullptr, "and has no named variant");
    gfx::shader named;
    auto skinned = std::make_shared<gfx::shader>();
    named.variants.emplace("skinned", skinned);
    check(named.find_variant("skinned") == skinned.get(), "a named variant is found");
    check(named.find_variant(gfx::shader::DEFAULT_VARIANT) == &named, "a shader with variants is its own default too");
    check(named.find_variant("other") == nullptr && named.find_variant("") == nullptr, "a variant it does not list is not found");
}

void test_varying_mismatches()
{
    std::printf("test_varying_mismatches\n");
    gfx::shader vertex;
    vertex.varyings = {{"v_uv", "vec2"}, {"v_color", "vec4"}, {"v_only_written", "vec3"}};
    gfx::shader fragment;
    fragment.varyings = {{"v_uv", "vec2"}, {"v_color", "vec4"}};
    check(unravel::describe_varying_mismatches(vertex, fragment).empty(), "stages that agree have no mismatch, also when a name is only written");
    fragment.varyings["v_uv"] = "vec3";
    const auto type_mismatch = unravel::describe_varying_mismatches(vertex, fragment);
    check(contains_text(type_mismatch, "v_uv is vec2 in the vertex shader and vec3 in the fragment shader"), "a type mismatch names both types: " + type_mismatch);
    check(!contains_text(type_mismatch, "v_color"), "and only the name that differs");
    fragment.varyings["v_color"] = "flat vec4";
    check(contains_text(unravel::describe_varying_mismatches(vertex, fragment), "v_color is vec4 in the vertex shader and flat vec4"), "an interpolation counts");
    const gfx::shader plain;
    check(unravel::describe_varying_mismatches(vertex, plain).empty() && unravel::describe_varying_mismatches(plain, fragment).empty(),
          "a shader that declares nothing has nothing to compare");
}

auto has_dependency(const std::vector<fs::path>& dependencies, const fs::path& file) -> bool
{
    return std::any_of(dependencies.begin(),
                       dependencies.end(),
                       [&](const fs::path& dependency)
                       {
                           return dependency.lexically_normal() == file.lexically_normal();
                       });
}

void test_dependencies(const fs::path& root)
{
    std::printf("test_dependencies\n");
    const auto folder = root / "dependencies";
    write_text(folder / "body.sh", "void main() {}\n");
    write_text(folder / "fs_new.eshader", "[inputs]\nvec2 v_uv\n\n[code]\n#include \"body.sh\"\n");
    std::vector<fs::path> own;
    ac::resolve_dependencies<gfx::shader>(folder / "fs_new.eshader", own);
    check(has_dependency(own, folder / "body.sh"), "an include in [code] is a dependency");
    write_text(folder / "fs_engine.eshader", "[code]\n#include \"common.sh\"\n");
    const auto engine_header = fs::resolve_protocol("engine:/data/shaders/common.sh");
    std::vector<fs::path> engine;
    ac::resolve_dependencies<gfx::shader>(folder / "fs_engine.eshader", engine);
    check(has_dependency(engine, engine_header), "an include among the engine's shaders is a dependency");
    write_text(folder / "common.sh", "\n");
    std::vector<fs::path> shadowed;
    ac::resolve_dependencies<gfx::shader>(folder / "fs_engine.eshader", shadowed);
    check(has_dependency(shadowed, folder / "common.sh") && !has_dependency(shadowed, engine_header), "a file beside the shader comes first, as in shaderc");
}

void test_compiled_file_reads_from_a_pack(const fs::path& root)
{
    std::printf("test_compiled_file_reads_from_a_pack\n");
    constexpr const char* PACK_PROTOCOL = "unravel-tests-eshader";
    constexpr const char* COMPILED_NAME = "compiled/shaders/fs_packed.eshader.asset.spirv";
    const auto folder = root / "pack";
    const auto protocol_root = folder / "root";
    fs::error_code ec;
    fs::create_directories(protocol_root, ec);
    fs::add_path_protocol(PACK_PROTOCOL, protocol_root);
    const compiled_variants written = {{"default", {1, 2, 3, 4}, {{"v_uv", "vec2"}}}, {"skinned", {5, 6}, {{"v_uv", "vec2"}, {"v_id", "flat int"}}}};
    const auto staged = folder / "staged.asset";
    unravel::save_to_file_bin(staged.string(), written);
    fs::pack_write_options options;
    options.compress = true;
    std::string error;
    fs::unmount_packs();
    check(fs::write_pack(folder / "content.pak", {{COMPILED_NAME, staged}}, options, error), "the pack is written: " + error);
    check(fs::mount_pack(PACK_PROTOCOL, folder / "content.pak", error), "the pack mounts: " + error);
    const auto compiled_path = protocol_root / COMPILED_NAME;
    check(!fs::exists(compiled_path, ec) && fs::is_readable_file(compiled_path), "a deployed game finds the file in its pack");
    compiled_variants loaded;
    unravel::load_from_file_bin(compiled_path.string(), loaded);
    check(loaded.size() == 2 && loaded[0].name == "default" && loaded[0].bytes == written[0].bytes && loaded[0].varyings == written[0].varyings &&
              loaded[1].name == "skinned" && loaded[1].bytes == written[1].bytes && loaded[1].varyings == written[1].varyings,
          "every variant reads back from the pack");
    fs::unmount_packs();
}

auto is_shaderc_present() -> bool
{
    fs::error_code ec;
    return fs::exists(fs::resolve_protocol(std::string("binary:/shaderc") + fs::executable_extension()), ec);
}

auto get_key(const std::string& name) -> std::string
{
    return std::string("app:/") + SHADER_FOLDER + "/" + name;
}

auto get_source_folder() -> fs::path
{
    return fs::resolve_protocol("app:/") / SHADER_FOLDER;
}

/// What the compiled file of `key` holds for `renderer_extension`, compiled to `output_folder`.
auto compile_variants(unravel::asset_manager& am, const std::string& key, const fs::path& output_folder, const std::string& renderer_extension)
    -> compiled_variants
{
    fs::error_code ec;
    fs::create_directories(output_folder, ec);
    const auto output = output_folder / (fs::path(key).filename().string() + ".asset" + renderer_extension);
    compiled_variants compiled;
    if(ac::compile<gfx::shader>(am, key, output))
    {
        unravel::load_from_file_bin(output.string(), compiled);
    }
    return compiled;
}

auto as_string(const unravel::compiled_shader_variant& variant) -> std::string
{
    return std::string(variant.bytes.begin(), variant.bytes.end());
}

void test_compile_matches_sc(unravel::asset_manager& am, const fs::path& root)
{
    std::printf("test_compile_matches_sc\n");
    const auto sources = get_source_folder();
    const std::string fragment_body = "#include <bgfx_shader.sh>\n"
                                      "SAMPLER2D(s_tex, 0);\n"
                                      "void main()\n"
                                      "{\n"
                                      "#if TINTED\n"
                                      "    gl_FragColor = texture2D(s_tex, v_texcoord0) * vec4_splat(TINT_AMOUNT);\n"
                                      "#else\n"
                                      "    gl_FragColor = texture2D(s_tex, v_texcoord0);\n"
                                      "#endif\n"
                                      "}\n";
    const std::string vertex_body = "#include <bgfx_shader.sh>\n"
                                    "void main()\n"
                                    "{\n"
                                    "    vec3 position = a_position;\n"
                                    "#if SKINNED\n"
                                    "    position = position * a_weight.x + a_indices.xyz;\n"
                                    "#endif\n"
                                    "    gl_Position = vec4(position, 1.0);\n"
                                    "    v_texcoord0 = a_texcoord0;\n"
                                    "}\n";
    const std::string fragment_inputs = "[inputs]\nvec2 v_texcoord0\n\n[code]\n";
    const std::string vertex_inputs = "[inputs]\nvec3 a_position\nvec2 a_texcoord0\n";
    const std::string vertex_outputs = "[outputs]\nvec2 v_texcoord0\n\n[code]\n";
    write_text(sources / "fs_plain.eshader", fragment_inputs + fragment_body);
    write_text(sources / "fs_tinted.eshader", fragment_inputs + "#define TINTED 1\n#define TINT_AMOUNT 0.5\n" + fragment_body);
    write_text(sources / "fs_merged.eshader", "[variants]\ndefault\ntinted   TINTED TINT_AMOUNT=0.5\n\n" + fragment_inputs + fragment_body);
    write_text(sources / "vs_plain.eshader", vertex_inputs + "\n" + vertex_outputs + vertex_body);
    write_text(sources / "vs_skinned.eshader", vertex_inputs + "vec4 a_weight\nvec4 a_indices\n\n" + vertex_outputs + "#define SKINNED 1\n" + vertex_body);
    write_text(sources / "vs_merged.eshader",
               "[variants]\ndefault\nskinned   SKINNED\n\n" + vertex_inputs + "#if SKINNED\nvec4 a_weight\nvec4 a_indices\n#endif\n\n" + vertex_outputs + vertex_body);
    const std::string compute_body = "#include \"bgfx_compute.sh\"\nBUFFER_WO(b_out, uint, 0);\nNUM_THREADS(1, 1, 1)\nvoid main() { b_out[0] = 0u; }\n";
    write_text(sources / "cs_merged.eshader", "[code]\n" + compute_body);
    const auto output_folder = root / "compile";
    fs::error_code ec;
    fs::create_directories(output_folder, ec);
    const auto compile_single = [&](const std::string& name, const std::string& extension)
    {
        const auto compiled = compile_variants(am, get_key(name + ".eshader"), output_folder, extension);
        return compiled.size() == 1 ? as_string(compiled.front()) : std::string{};
    };
    for(const auto& extension : gfx::get_renderer_platform_supported_filename_extensions())
    {
        const auto fragment = compile_variants(am, get_key("fs_merged.eshader"), output_folder, extension);
        check(fragment.size() == 2 && fragment[0].name == "default" && fragment[1].name == "tinted",
              "the compiled file lists the variants in order on " + extension);
        if(fragment.size() == 2)
        {
            check(!fragment[0].bytes.empty() && as_string(fragment[0]) == compile_single("fs_plain", extension),
                  "the default variant equals a file without variants on " + extension);
            check(!fragment[1].bytes.empty() && as_string(fragment[1]) == compile_single("fs_tinted", extension),
                  "a variant equals a file that defines the same in its code on " + extension);
            check(fragment[0].bytes != fragment[1].bytes, "the variants differ on " + extension);
            const std::map<std::string, std::string> expected_varyings = {{"v_texcoord0", "vec2"}};
            check(fragment[0].varyings == expected_varyings && fragment[1].varyings == expected_varyings, "every variant holds the varyings on " + extension);
        }
        const auto vertex = compile_variants(am, get_key("vs_merged.eshader"), output_folder, extension);
        check(vertex.size() == 2, "a vertex shader compiles with #if inputs on " + extension);
        if(vertex.size() == 2)
        {
            check(as_string(vertex[0]) == compile_single("vs_plain", extension), "its default equals a file without variants on " + extension);
            check(as_string(vertex[1]) == compile_single("vs_skinned", extension), "its skinned variant equals a file that defines the same in its code on " + extension);
        }
        const auto compute = compile_variants(am, get_key("cs_merged.eshader"), output_folder, extension);
        check(compute.size() == 1 && !compute[0].bytes.empty(), "a compute shader compiles on " + extension);
    }
}

void test_every_type_compiles(unravel::asset_manager& am, const fs::path& root)
{
    std::printf("test_every_type_compiles\n");
    const auto sources = get_source_folder();
    write_text(sources / "vs_types.eshader",
               "[inputs]\nvec3 a_position\nuvec4 a_indices\n\n"
               "[outputs]\nfloat v_a\nvec2 v_b\nvec3 v_c\nvec4 v_d\nflat int v_e\nflat ivec2 v_f\nflat ivec3 v_g\nflat ivec4 v_h\n"
               "flat uint v_i\nflat uvec2 v_j\nflat uvec3 v_k\nflat uvec4 v_l\nsmooth vec2 v_m\nnoperspective vec2 v_n\ncentroid vec2 v_o\n\n"
               "[code]\n#include <bgfx_shader.sh>\nvoid main() { gl_Position = vec4(a_position, 1.0); }\n");
    write_text(sources / "fs_types.eshader",
               "[inputs]\nfloat v_a\nvec2 v_b\nvec3 v_c\nvec4 v_d\nflat int v_e\nflat ivec2 v_f\nflat ivec3 v_g\nflat ivec4 v_h\n"
               "flat uint v_i\nflat uvec2 v_j\nflat uvec3 v_k\nflat uvec4 v_l\nsmooth vec2 v_m\nnoperspective vec2 v_n\ncentroid vec2 v_o\n\n"
               "[code]\n#include <bgfx_shader.sh>\nvoid main() { gl_FragColor = vec4(v_a, v_b, v_c.x); }\n");
    for(const auto& extension : gfx::get_renderer_platform_supported_filename_extensions())
    {
        check(compile_variants(am, get_key("vs_types.eshader"), root / "types", extension).size() == 1, "a vertex shader with every type compiles on " + extension);
        check(compile_variants(am, get_key("fs_types.eshader"), root / "types", extension).size() == 1, "a fragment shader with every type compiles on " + extension);
    }
}

auto count_temp_files(const fs::path& folder) -> std::size_t
{
    fs::error_code ec;
    std::size_t count = 0;
    for(const auto& entry : fs::directory_iterator(folder, ec))
    {
        count += entry.path().extension() == ".temp" ? 1 : 0;
    }
    return count;
}

void test_failed_compile_keeps_output(unravel::asset_manager& am, const fs::path& root)
{
    std::printf("test_failed_compile_keeps_output\n");
    const auto sources = get_source_folder();
    const auto key = get_key("fs_failing.eshader");
    const std::string inputs = "[inputs]\nvec2 v_texcoord0\n\n";
    const std::string code = "[code]\n#include <bgfx_shader.sh>\nvoid main()\n{\n    gl_FragColor = vec4_splat(1.0);\n#if BROKEN\n    no_such_function();\n#endif\n}\n";
    const auto output_folder = root / "failing";
    const std::string extension = *gfx::get_renderer_platform_supported_filename_extensions().begin();
    const auto output = output_folder / ("fs_failing.eshader.asset" + extension);
    write_text(sources / "fs_failing.eshader", inputs + code);
    check(compile_variants(am, key, output_folder, extension).size() == 1, "the file compiles before the broken variant");
    const auto compiled_before = read_bytes(output);
    const auto manifest_before = read_bytes(ac::get_manifest_path(output));
    write_text(sources / "fs_failing.eshader", "[variants]\ndefault\nbroken   BROKEN\n\n" + inputs + code);
    std::printf("  (the shader errors logged next are expected)\n");
    check(!ac::compile<gfx::shader>(am, key, output), "one failed variant fails the compile");
    check(read_bytes(output) == compiled_before && !compiled_before.empty(), "the compiled file stays as it was");
    check(read_bytes(ac::get_manifest_path(output)) == manifest_before, "and its manifest, so the next start retries");
    check(count_temp_files(output_folder) == 0, "no temp file stays behind");
    write_text(sources / "fs_failing.eshader", "[code]\n[not a section\n");
    check(!ac::compile<gfx::shader>(am, key, output), "a file of one variant with bad code fails too");
    write_text(sources / "fs_failing.eshader", "no sections\n");
    check(!ac::compile<gfx::shader>(am, key, output) && read_bytes(output) == compiled_before, "a file that does not parse changes nothing");
}

/// The pairs of variants that are the same binary on every renderer: their defines changed nothing.
auto find_identical_variants(const std::vector<compiled_variants>& per_renderer) -> std::vector<std::string>
{
    std::vector<std::string> pairs;
    if(per_renderer.empty() || per_renderer.front().empty())
    {
        return pairs;
    }
    const auto count = per_renderer.front().size();
    for(std::size_t first = 0; first < count; ++first)
    {
        for(std::size_t second = first + 1; second < count; ++second)
        {
            const bool is_identical = std::all_of(per_renderer.begin(),
                                                  per_renderer.end(),
                                                  [&](const compiled_variants& compiled)
                                                  {
                                                      return compiled.size() == count && compiled[first].bytes == compiled[second].bytes;
                                                  });
            if(is_identical)
            {
                pairs.push_back(per_renderer.front()[first].name + " = " + per_renderer.front()[second].name);
            }
        }
    }
    return pairs;
}

void test_mistyped_define_is_found(unravel::asset_manager& am, const fs::path& root)
{
    std::printf("test_mistyped_define_is_found\n");
    write_text(get_source_folder() / "fs_typo.eshader",
               "[variants]\ndefault\nskinned   SKINED\nred   RED\n\n[code]\n#include <bgfx_shader.sh>\nvoid main()\n{\n"
               "#if RED\n    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n#else\n    gl_FragColor = vec4_splat(1.0);\n#endif\n}\n");
    std::vector<compiled_variants> per_renderer;
    for(const auto& extension : gfx::get_renderer_platform_supported_filename_extensions())
    {
        per_renderer.push_back(compile_variants(am, get_key("fs_typo.eshader"), root / "typo", extension));
    }
    const auto pairs = find_identical_variants(per_renderer);
    check(pairs == std::vector<std::string>{"default = skinned"}, "a define nothing reads shows as an identical variant, and a real one does not");
}

/// None of these shaders exist, so nothing reaches a device. A pair with an empty shader is neither built nor refused:
/// the revision counts both, and holding at 0 is what leaves begin() retrying until a reload brings the shader.
void test_failed_loads_are_retried(unravel::asset_manager& am)
{
    std::printf("test_failed_loads_are_retried\n");
    std::printf("  (the errors logged next are expected)\n");
    const auto get_deferred = [&](const std::string& name)
    {
        return am.get_asset<gfx::shader>(get_key(name), unravel::load_flags::standard, unravel::load_mode::deferred);
    };
    unravel::gpu_program plain(get_deferred("vs_no_such_shader.eshader"), get_deferred("fs_no_such_shader.eshader"));
    check(!plain.begin() && !plain.begin() && plain.get_revision() == 0u, "a pair that failed to load tries again each frame");
    unravel::gpu_program named(get_deferred("vs_no_such_shader.eshader"), "skinned", get_deferred("fs_no_such_shader.eshader"));
    check(!named.begin() && named.get_revision() == 0u, "a shader that failed to load is not refused for a variant it does not have yet");
}

/// What of WebGPU's core limits a variant's WGSL exceeds, as a sentence part; empty when it fits.
auto find_webgpu_limit_problems(const unravel::compiled_shader_variant& variant) -> std::string
{
    static const std::regex workgroup_size(R"(@workgroup_size\((\d+)[iu]?,\s*(\d+)[iu]?,\s*(\d+)[iu]?\))");
    static const std::regex storage_texture(R"(var\s+(\w+)\s*:\s*texture_storage_)");
    const std::string wgsl = as_string(variant);
    std::string problems;
    std::smatch size;
    if(std::regex_search(wgsl, size, workgroup_size))
    {
        unsigned long invocations = 1;
        for(std::size_t axis = 0; axis < WEBGPU_MAX_WORKGROUP_SIZE.size(); ++axis)
        {
            const unsigned long axis_size = std::stoul(size[axis + 1]);
            invocations *= axis_size;
            if(axis_size > WEBGPU_MAX_WORKGROUP_SIZE[axis])
            {
                problems += std::to_string(axis_size) + " invocations along axis " + std::to_string(axis) + " ";
            }
        }
        if(invocations > WEBGPU_MAX_WORKGROUP_INVOCATIONS)
        {
            problems += std::to_string(invocations) + " invocations per workgroup ";
        }
    }
    else if(wgsl.find("@compute") != std::string::npos)
    {
        problems += "a workgroup size this check cannot read ";
    }
    std::set<std::string> storage_textures;
    for(std::sregex_iterator it(wgsl.begin(), wgsl.end(), storage_texture), end; it != end; ++it)
    {
        storage_textures.insert((*it)[1].str());
    }
    if(storage_textures.size() > WEBGPU_MAX_STORAGE_TEXTURES_PER_STAGE)
    {
        problems += std::to_string(storage_textures.size()) + " storage textures";
    }
    return problems;
}

void test_engine_shaders_fit_webgpu(unravel::asset_manager& am, const fs::path& root)
{
    std::printf("test_engine_shaders_fit_webgpu\n");
    const fs::path shaders = fs::path(ENGINE_DATA_DIR) / "data/shaders";
    const auto renderer = gfx::get_renderer_filename_extension(bgfx::RendererType::WebGPU);
    std::size_t variant_count = 0;
    fs::error_code ec;
    for(const auto& entry : fs::recursive_directory_iterator(shaders, ec))
    {
        const auto relative = entry.path().lexically_relative(shaders);
        if(entry.path().extension() != ex::get_format<gfx::shader>() || *relative.begin() == WEBGPU_EXEMPT_FOLDER)
        {
            continue;
        }
        const auto name = relative.generic_string();
        const auto compiled = compile_variants(am, entry.path().string(), root / "webgpu", renderer);
        check(!compiled.empty(), name + " compiles to WGSL");
        for(const auto& variant : compiled)
        {
            const auto problems = find_webgpu_limit_problems(variant);
            check(problems.empty(), name + " variant " + variant.name + " exceeds WebGPU's core limits: " + problems);
            ++variant_count;
        }
    }
    std::printf("  %zu WGSL variants\n", variant_count);
    check(variant_count > 0, "the engine's shaders are found");
}

void test_shipped_shaders_compile(unravel::asset_manager& am, const fs::path& root)
{
    std::printf("test_shipped_shaders_compile\n");
    const fs::path engine_data = ENGINE_DATA_DIR;
    const auto output_folder = root / "shipped";
    std::size_t file_count = 0;
    std::size_t variant_count = 0;
    for(const auto& folder : {engine_data / "data/shaders", engine_data / "../editor_data/data/shaders"})
    {
        fs::error_code ec;
        for(const auto& entry : fs::recursive_directory_iterator(folder, ec))
        {
            if(entry.path().extension() != ex::get_format<gfx::shader>())
            {
                continue;
            }
            ++file_count;
            const auto name = entry.path().filename().string();
            ac::eshader_file file;
            std::string error;
            check(ac::load_eshader_file(entry.path(), file, error), name + " parses: " + error);
            std::vector<compiled_variants> per_renderer;
            for(const auto& extension : gfx::get_renderer_platform_supported_filename_extensions())
            {
                auto compiled = compile_variants(am, entry.path().string(), output_folder, extension);
                check(compiled.size() == file.variants.size(), name + " compiles on " + extension);
                variant_count += compiled.size();
                per_renderer.push_back(std::move(compiled));
            }
            for(const auto& pair : find_identical_variants(per_renderer))
            {
                check(false, name + " has variants that are the same binary everywhere, so their defines change nothing: " + pair);
            }
        }
    }
    std::printf("  %zu files, %zu binaries\n", file_count, variant_count);
    check(file_count > 0, "the editor's shaders are found");
}

auto run_eshader_files_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto temp = get_temp_folder();
    fs::error_code ec;
    fs::remove_all(temp, ec);
    fs::create_directories(temp, ec);
    test_parse_sections();
    test_parse_defaults();
    test_parse_errors();
    test_vertex_inputs();
    test_generated_varyings();
    test_format();
    test_find_variant();
    test_varying_mismatches();
    test_dependencies(temp);
    test_compiled_file_reads_from_a_pack(temp);
    if(is_shaderc_present())
    {
        auto& am = ctx.get_cached<unravel::asset_manager>();
        test_compile_matches_sc(am, temp);
        test_every_type_compiles(am, temp);
        test_failed_compile_keeps_output(am, temp);
        test_mistyped_define_is_found(am, temp);
        test_failed_loads_are_retried(am);
        test_shipped_shaders_compile(am, temp);
        test_engine_shaders_fit_webgpu(am, temp);
    }
    else
    {
        std::printf("  SKIP: shaderc is not built beside the tests, so nothing compiles\n");
    }
    fs::remove_all(temp, ec);
    fs::remove_all(get_source_folder(), ec);
    std::printf("eshader files: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}
} // namespace

REGISTER_TEST_SUITE("eshader files", run_eshader_files_suite)
