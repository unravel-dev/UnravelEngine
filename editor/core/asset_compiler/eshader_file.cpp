#include "eshader_file.h"

#include <graphics/shader.h>
#include <logging/logging.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>

namespace unravel
{
namespace asset_compiler
{

namespace
{

constexpr hpp::string_view INPUTS_SECTION = "[inputs]";
constexpr hpp::string_view OUTPUTS_SECTION = "[outputs]";
constexpr hpp::string_view VARIANTS_SECTION = "[variants]";
constexpr hpp::string_view CODE_SECTION = "[code]";
constexpr hpp::string_view COMMENT_PREFIX = "//";
constexpr hpp::string_view WHITESPACE = " \t\r\n";
constexpr hpp::string_view DIGITS = "0123456789";
constexpr hpp::string_view UTF8_BOM = "\xEF\xBB\xBF";
constexpr hpp::string_view VARYING_PREFIX = "v_";
constexpr hpp::string_view INSTANCE_PREFIX = "i_";
constexpr hpp::string_view INSTANCE_TYPE = "vec4";
constexpr hpp::string_view FLAT_INTERPOLATION = "flat";

/// Names a vertex shader can read, as bgfx binds them. A family with a count is numbered: a_color0 to a_color3.
struct vertex_input_family
{
    const char* name;
    const char* tag;
    int count;
    int first_tag_index;
    int tag_index_step;
    const char* meaning;
};

/// bgfx::Attrib in its order (shaderc_hlsl.cpp s_remapInputSemantic), then the instance data slots, which bgfx numbers
/// down from BGFX_CONFIG_INSTANCE_DATA_FIRST_TEXCOORD for BGFX_CONFIG_MAX_INSTANCE_DATA_COUNT slots.
constexpr std::array<vertex_input_family, 9> VERTEX_INPUTS = {{
    {"a_position", "POSITION", 0, 0, 0, "vertex position"},
    {"a_normal", "NORMAL", 0, 0, 0, "vertex normal"},
    {"a_tangent", "TANGENT", 0, 0, 0, "vertex tangent"},
    {"a_bitangent", "BITANGENT", 0, 0, 0, "vertex bitangent"},
    {"a_color", "COLOR", 4, 0, 1, "vertex colors"},
    {"a_indices", "BLENDINDICES", 0, 0, 0, "bone indices"},
    {"a_weight", "BLENDWEIGHT", 0, 0, 0, "bone weights"},
    {"a_texcoord", "TEXCOORD", 16, 0, 1, "texture coordinates"},
    {"i_data", "TEXCOORD", 16, 31, -1, "per instance data, vec4 only"},
}};

/// The types a declaration can have, with the value a vertex output without a default starts from.
struct declaration_type
{
    hpp::string_view name;
    hpp::string_view zero;
    bool is_integer;
};

constexpr std::array<declaration_type, 12> DECLARATION_TYPES = {{
    {"float", "0.0", false},
    {"vec2", "vec2(0.0, 0.0)", false},
    {"vec3", "vec3(0.0, 0.0, 0.0)", false},
    {"vec4", "vec4(0.0, 0.0, 0.0, 0.0)", false},
    {"int", "0", true},
    {"ivec2", "ivec2(0, 0)", true},
    {"ivec3", "ivec3(0, 0, 0)", true},
    {"ivec4", "ivec4(0, 0, 0, 0)", true},
    {"uint", "0u", true},
    {"uvec2", "uvec2(0u, 0u)", true},
    {"uvec3", "uvec3(0u, 0u, 0u)", true},
    {"uvec4", "uvec4(0u, 0u, 0u, 0u)", true},
}};

constexpr std::array<hpp::string_view, 4> INTERPOLATIONS = {"flat", "smooth", "noperspective", "centroid"};
constexpr std::array<hpp::string_view, 3> CONDITION_OPENERS = {"#if", "#ifdef", "#ifndef"};
constexpr std::array<hpp::string_view, 2> CONDITION_BRANCHES = {"#elif", "#else"};
constexpr hpp::string_view CONDITION_CLOSER = "#endif";

enum class section_type
{
    none,
    inputs,
    outputs,
    variants,
};

/// What the parser knows from the lines it has read.
struct parser_state
{
    shader_stage stage = shader_stage::vertex;
    section_type current = section_type::none;
    std::vector<section_type> opened;
    int open_conditions = 0;
    /// The line being parsed, counted from 1.
    std::size_t line_number = 0;
};

template<typename Container>
auto contains(const Container& container, const typename Container::value_type& value) -> bool
{
    return std::find(container.begin(), container.end(), value) != container.end();
}

auto trim(hpp::string_view text) -> hpp::string_view
{
    const auto first = text.find_first_not_of(WHITESPACE);
    if(first == hpp::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(WHITESPACE) - first + 1);
}

auto strip_comment(hpp::string_view line) -> hpp::string_view
{
    return trim(line.substr(0, line.find(COMMENT_PREFIX)));
}

/// The line that starts at @p position, with its end. Moves @p position to the next line.
auto read_line(hpp::string_view text, std::size_t& position) -> hpp::string_view
{
    const auto line_end = text.find('\n', position);
    const auto next = line_end == hpp::string_view::npos ? text.size() : line_end + 1;
    const auto line = text.substr(position, next - position);
    position = next;
    return line;
}

auto is_name_character(char c) -> bool
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

auto is_identifier(hpp::string_view name) -> bool
{
    return !name.empty() && std::isdigit(static_cast<unsigned char>(name.front())) == 0 &&
           std::all_of(name.begin(), name.end(), is_name_character);
}

auto is_section_line(hpp::string_view line) -> bool
{
    return line.size() >= 2 && line.front() == '[' && line.back() == ']' &&
           std::all_of(line.begin() + 1, line.end() - 1, is_name_character);
}

auto is_variant_name(hpp::string_view name) -> bool
{
    return !name.empty() && std::all_of(name.begin(),
                                        name.end(),
                                        [](char c)
                                        {
                                            return is_name_character(c) || c == '.';
                                        });
}

/// NAME or NAME=VALUE. shaderc reads all defines as one ';'-separated list, so a ';' would split one in two.
auto is_define(hpp::string_view define) -> bool
{
    if(define.find(';') != hpp::string_view::npos)
    {
        return false;
    }
    const auto equals = define.find('=');
    return is_identifier(define.substr(0, equals)) && (equals == hpp::string_view::npos || equals + 1 < define.size());
}

auto has_variant(const eshader_file& file, hpp::string_view name) -> bool
{
    return std::any_of(file.variants.begin(),
                       file.variants.end(),
                       [&](const eshader_variant& variant)
                       {
                           return variant.name == name;
                       });
}

auto find_declaration_type(hpp::string_view name) -> const declaration_type*
{
    const auto found = std::find_if(DECLARATION_TYPES.begin(),
                                    DECLARATION_TYPES.end(),
                                    [&](const declaration_type& type)
                                    {
                                        return type.name == name;
                                    });
    return found != DECLARATION_TYPES.end() ? &*found : nullptr;
}

auto list_declaration_types() -> std::string
{
    std::vector<hpp::string_view> names;
    for(const auto& type : DECLARATION_TYPES)
    {
        names.push_back(type.name);
    }
    return fmt::format("{}", fmt::join(names, ", "));
}

auto split_words(hpp::string_view line) -> std::vector<hpp::string_view>
{
    std::vector<hpp::string_view> words;
    auto first = line.find_first_not_of(WHITESPACE);
    while(first != hpp::string_view::npos)
    {
        const auto last = line.find_first_of(WHITESPACE, first);
        words.push_back(line.substr(first, last == hpp::string_view::npos ? last : last - first));
        first = last == hpp::string_view::npos ? last : line.find_first_not_of(WHITESPACE, last);
    }
    return words;
}

auto parse_variant_line(hpp::string_view line, eshader_file& file, std::string& error) -> bool
{
    const auto words = split_words(line);
    const auto name = words.front();
    if(!is_variant_name(name))
    {
        error = fmt::format("the variant name '{}' may only use letters, digits, '_' and '.'", name);
        return false;
    }
    if(has_variant(file, name))
    {
        error = fmt::format("the variant '{}' is declared twice", name);
        return false;
    }
    eshader_variant variant;
    variant.name = std::string(name);
    for(std::size_t i = 1; i < words.size(); ++i)
    {
        if(!is_define(words[i]))
        {
            error = fmt::format("'{}' in variant '{}' is not NAME or NAME=VALUE", words[i], name);
            return false;
        }
        variant.defines.emplace_back(words[i]);
    }
    file.variants.push_back(std::move(variant));
    return true;
}

auto find_section(hpp::string_view line) -> section_type
{
    if(line == INPUTS_SECTION)
    {
        return section_type::inputs;
    }
    if(line == OUTPUTS_SECTION)
    {
        return section_type::outputs;
    }
    if(line == VARIANTS_SECTION)
    {
        return section_type::variants;
    }
    return section_type::none;
}

auto open_section(hpp::string_view line, parser_state& state, std::string& error) -> bool
{
    const auto section = find_section(line);
    if(section == section_type::none)
    {
        error = fmt::format("'{}' is not a section; the sections are {}, {}, {} and {}",
                            line,
                            VARIANTS_SECTION,
                            INPUTS_SECTION,
                            OUTPUTS_SECTION,
                            CODE_SECTION);
        return false;
    }
    if(state.stage == shader_stage::compute && section != section_type::variants)
    {
        error = fmt::format("a compute shader has no {}: it reads and writes through images and buffers", line);
        return false;
    }
    if(state.stage == shader_stage::fragment && section == section_type::outputs)
    {
        error = fmt::format("a fragment shader has no {}: it writes gl_FragColor", line);
        return false;
    }
    if(contains(state.opened, section))
    {
        error = fmt::format("the section {} appears twice", line);
        return false;
    }
    state.opened.push_back(section);
    state.current = section;
    return true;
}

/// An #if line of [inputs] or [outputs], kept as written. shaderc evaluates it where it evaluates the code's.
auto parse_condition_line(hpp::string_view line, parser_state& state, eshader_file& file, std::string& error) -> bool
{
    const auto directive = line.substr(0, line.find_first_of(WHITESPACE));
    if(contains(CONDITION_OPENERS, directive))
    {
        ++state.open_conditions;
    }
    else if(directive == CONDITION_CLOSER)
    {
        --state.open_conditions;
    }
    else if(!contains(CONDITION_BRANCHES, directive))
    {
        error = fmt::format("'{}' cannot appear here; {} and {} take only {}, {} and {}",
                            directive,
                            INPUTS_SECTION,
                            OUTPUTS_SECTION,
                            fmt::join(CONDITION_OPENERS, ", "),
                            fmt::join(CONDITION_BRANCHES, ", "),
                            CONDITION_CLOSER);
        return false;
    }
    if(state.open_conditions < 0 || (state.open_conditions == 0 && directive != CONDITION_CLOSER))
    {
        error = fmt::format("'{}' has no #if to belong to", directive);
        return false;
    }
    file.interface_lines.append(line.data(), line.size());
    return true;
}

auto validate_vertex_input(const eshader_declaration& declaration, std::string& error) -> bool
{
    if(get_vertex_input_tag(declaration.name).empty())
    {
        error = fmt::format("'{}' is not a vertex input bgfx can bind, so no mesh could feed it. A vertex shader can read "
                            "these names, each shown with the tag it maps to:\n{}",
                            declaration.name,
                            describe_vertex_inputs());
        return false;
    }
    if(!declaration.interpolation.empty() || !declaration.default_value.empty())
    {
        error = fmt::format("the vertex input '{}' takes only a type and a name: it has no interpolation or default, "
                            "since the mesh provides it",
                            declaration.name);
        return false;
    }
    if(hpp::string_view(declaration.name).starts_with(INSTANCE_PREFIX) && declaration.type != INSTANCE_TYPE)
    {
        error = fmt::format("the instance data '{}' must be {}: bgfx passes each slot as one", declaration.name, INSTANCE_TYPE);
        return false;
    }
    return true;
}

auto validate_varying(const eshader_declaration& declaration, section_type section, std::string& error) -> bool
{
    if(!hpp::string_view(declaration.name).starts_with(VARYING_PREFIX))
    {
        error = section == section_type::outputs
                    ? fmt::format("the vertex output '{}' must start with {}", declaration.name, VARYING_PREFIX)
                    : fmt::format("the fragment input '{}' must start with {}: a fragment shader reads what its vertex "
                                  "shader writes, not what the mesh provides",
                                  declaration.name,
                                  VARYING_PREFIX);
        return false;
    }
    if(section == section_type::inputs && !declaration.default_value.empty())
    {
        error = fmt::format("'{}' has a default, which only means something on a vertex output", declaration.name);
        return false;
    }
    if(find_declaration_type(declaration.type)->is_integer && declaration.interpolation != FLAT_INTERPOLATION)
    {
        error = fmt::format("the varying '{}' must be {}: a {} cannot be interpolated", declaration.name, FLAT_INTERPOLATION, declaration.type);
        return false;
    }
    return true;
}

/// Splits `[interpolation] type name [= default]`. The same text a varying file holds is refused with what to write.
auto split_declaration(hpp::string_view line, eshader_declaration& declaration, std::string& error) -> bool
{
    if(line.find(':') != hpp::string_view::npos)
    {
        error = fmt::format("'{}': the ': TAG' part is generated, so write only the type and the name", line);
        return false;
    }
    if(line.back() == ';')
    {
        error = fmt::format("'{}' takes no ';'", line);
        return false;
    }
    const auto equals = line.find('=');
    const auto words = split_words(line.substr(0, equals));
    if(words.size() < 2 || words.size() > 3)
    {
        error = fmt::format("'{}' must be a type and a name, such as: vec2 v_texcoord0", line);
        return false;
    }
    declaration.interpolation = words.size() == 3 ? std::string(words[0]) : std::string{};
    declaration.type = std::string(words[words.size() - 2]);
    declaration.name = std::string(words.back());
    if(equals == hpp::string_view::npos)
    {
        return true;
    }
    declaration.default_value = std::string(trim(line.substr(equals + 1)));
    if(declaration.default_value.empty())
    {
        error = fmt::format("'{}' has an '=' without a value", line);
        return false;
    }
    return true;
}

auto validate_declaration(const eshader_declaration& declaration, std::string& error) -> bool
{
    if(!declaration.interpolation.empty() && !contains(INTERPOLATIONS, declaration.interpolation))
    {
        error = fmt::format("'{}' is not an interpolation; those are {}", declaration.interpolation, fmt::join(INTERPOLATIONS, ", "));
        return false;
    }
    if(find_declaration_type(declaration.type) == nullptr)
    {
        error = fmt::format("'{}' is not a type here; those are {}", declaration.type, list_declaration_types());
        return false;
    }
    if(!is_identifier(declaration.name))
    {
        error = fmt::format("'{}' is not a name", declaration.name);
        return false;
    }
    return true;
}

/// A varying's tag only tells D3D which vertex output a fragment input is, so any name works if both stages use it.
/// Making it from the varying's name means they always do, without counting slots or evaluating an #if.
auto make_varying_tag(const std::string& name) -> std::string
{
    return string_utils::to_upper(name);
}

/// What D3D sees of a tag, which ignores case and reads the last digits as an index: v_uv and v_UV are one semantic, and
/// so are v_a and v_a0, and v_a1 and v_a01.
auto get_semantic_identity(const std::string& name) -> std::string
{
    auto tag = make_varying_tag(name);
    const auto index_start = tag.find_last_not_of(DIGITS) + 1;
    const auto first_non_zero = tag.find_first_not_of('0', index_start);
    tag.erase(index_start, first_non_zero == std::string::npos ? first_non_zero : first_non_zero - index_start);
    return tag;
}

/// A name can only come twice from different #if branches, and shaderc's one varying file holds one declaration of it.
auto add_declaration(const eshader_declaration& declaration, const parser_state& state, eshader_file& file, std::string& error) -> bool
{
    const auto identity = get_semantic_identity(declaration.name);
    const auto same_semantic = std::find_if(file.declarations.begin(),
                                            file.declarations.end(),
                                            [&](const eshader_declaration& other)
                                            {
                                                return get_semantic_identity(other.name) == identity;
                                            });
    if(same_semantic == file.declarations.end())
    {
        file.declarations.push_back(declaration);
        return true;
    }
    if(same_semantic->name != declaration.name)
    {
        error = fmt::format("'{}' and '{}' would share one generated semantic, which ignores case and leading zeros, so rename one",
                            same_semantic->name,
                            declaration.name);
        return false;
    }
    const bool is_same = same_semantic->interpolation == declaration.interpolation && same_semantic->type == declaration.type &&
                         same_semantic->default_value == declaration.default_value;
    if(state.open_conditions == 0 || !is_same)
    {
        error = fmt::format("'{}' is declared twice{}",
                            declaration.name,
                            state.open_conditions == 0 ? "" : ", differently, and #if branches must agree on what a name is");
        return false;
    }
    return true;
}

auto parse_interface_line(hpp::string_view line, parser_state& state, eshader_file& file, std::string& error) -> bool
{
    if(line.front() == '#')
    {
        return parse_condition_line(line, state, file, error);
    }
    eshader_declaration declaration;
    if(!split_declaration(line, declaration, error) || !validate_declaration(declaration, error))
    {
        return false;
    }
    const bool is_vertex_input = state.stage == shader_stage::vertex && state.current == section_type::inputs;
    const bool is_valid = is_vertex_input ? validate_vertex_input(declaration, error) : validate_varying(declaration, state.current, error);
    if(!is_valid || !add_declaration(declaration, state, file, error))
    {
        return false;
    }
    file.interface_lines += state.current == section_type::inputs ? "$input " : "$output ";
    file.interface_lines += declaration.name;
    return true;
}

/// One line before [code], without its comment: a section's start, one of its lines, or nothing at all.
auto parse_header_line(hpp::string_view line, parser_state& state, eshader_file& file, std::string& error) -> bool
{
    if(line.empty())
    {
        return true;
    }
    if(is_section_line(line))
    {
        if(state.open_conditions != 0)
        {
            error = "an #if of the section before is not closed";
            return false;
        }
        return open_section(line, state, error);
    }
    if(state.current == section_type::none)
    {
        error = fmt::format("'{}' comes before the first section", line);
        return false;
    }
    if(state.current == section_type::variants)
    {
        return parse_variant_line(line, file, error);
    }
    return parse_interface_line(line, state, file, error);
}

/// The code holds no $input or $output line: the sections declare them, and a second list could only disagree.
auto validate_code(hpp::string_view code, parser_state& state, std::string& error) -> bool
{
    std::size_t position = 0;
    while(position < code.size())
    {
        const auto line = trim(read_line(code, position));
        ++state.line_number;
        if(line.starts_with("$input") || line.starts_with("$output"))
        {
            error = fmt::format("'{}' belongs in the file's {} or {}, as a type and a name per line", line, INPUTS_SECTION, OUTPUTS_SECTION);
            return false;
        }
    }
    return true;
}

/// What follows the [code] line, which ends the parse, and the default variant of a file that lists none.
auto read_code_section(hpp::string_view code, parser_state& state, eshader_file& file, std::string& error) -> bool
{
    if(state.open_conditions != 0)
    {
        error = "an #if of the section before is not closed";
        return false;
    }
    if(contains(state.opened, section_type::variants) && !has_variant(file, gfx::shader::DEFAULT_VARIANT))
    {
        error = fmt::format("the section {} must list {}, the variant a shader is when nothing names one",
                            VARIANTS_SECTION,
                            gfx::shader::DEFAULT_VARIANT);
        return false;
    }
    if(trim(code).empty())
    {
        error = fmt::format("the section {} is empty", CODE_SECTION);
        return false;
    }
    if(!validate_code(code, state, error))
    {
        return false;
    }
    if(file.variants.empty())
    {
        file.variants.push_back({gfx::shader::DEFAULT_VARIANT, {}});
    }
    file.interface_lines += '\n';
    file.code = std::string(code);
    return true;
}

/// The whole text, or the error of the line state stopped on. A text without [code] is no error here.
auto parse_lines(hpp::string_view text, parser_state& state, eshader_file& file, std::string& error) -> bool
{
    std::size_t position = 0;
    while(position < text.size())
    {
        const auto line = strip_comment(read_line(text, position));
        ++state.line_number;
        if(line == CODE_SECTION)
        {
            return read_code_section(text.substr(position), state, file, error);
        }
        if(!parse_header_line(line, state, file, error))
        {
            return false;
        }
        file.interface_lines += '\n';
    }
    return true;
}

auto make_varying_line(const eshader_declaration& declaration) -> std::string
{
    const auto vertex_input_tag = get_vertex_input_tag(declaration.name);
    if(!vertex_input_tag.empty())
    {
        return fmt::format("{} {} : {};\n", declaration.type, declaration.name, vertex_input_tag);
    }
    return fmt::format("{}{}{} {} : {} = {};\n",
                       declaration.interpolation,
                       declaration.interpolation.empty() ? "" : " ",
                       declaration.type,
                       declaration.name,
                       make_varying_tag(declaration.name),
                       declaration.default_value.empty() ? std::string(find_declaration_type(declaration.type)->zero) : declaration.default_value);
}

} // namespace

auto parse_eshader_file(hpp::string_view text, shader_stage stage, eshader_file& file, std::string& error) -> bool
{
    file = {};
    parser_state state;
    state.stage = stage;
    if(text.starts_with(UTF8_BOM))
    {
        text.remove_prefix(UTF8_BOM.size());
    }
    if(!parse_lines(text, state, file, error))
    {
        error = fmt::format("line {}: {}", state.line_number, error);
        return false;
    }
    if(file.code.empty())
    {
        error = fmt::format("the file has no {} section", CODE_SECTION);
        return false;
    }
    return true;
}

auto load_eshader_file(const fs::path& path, eshader_file& file, std::string& error) -> bool
{
    const auto stage = get_shader_stage(path.stem().string());
    if(!stage)
    {
        error = "the file name must start with vs_, fs_ or cs_, which gives the shader's stage";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if(!stream.is_open())
    {
        error = "the file cannot be read";
        return false;
    }
    return parse_eshader_file(fs::read_stream_str(stream), *stage, file, error);
}

auto get_shader_stage(hpp::string_view name) -> hpp::optional<shader_stage>
{
    if(name.starts_with("vs_"))
    {
        return shader_stage::vertex;
    }
    if(name.starts_with("fs_"))
    {
        return shader_stage::fragment;
    }
    if(name.starts_with("cs_"))
    {
        return shader_stage::compute;
    }
    return {};
}

auto get_vertex_input_tag(hpp::string_view name) -> std::string
{
    for(const auto& family : VERTEX_INPUTS)
    {
        const hpp::string_view family_name = family.name;
        if(!name.starts_with(family_name))
        {
            continue;
        }
        if(family.count == 0)
        {
            if(name.size() == family_name.size())
            {
                return family.tag;
            }
            continue;
        }
        for(int index = 0; index < family.count; ++index)
        {
            if(name.substr(family_name.size()) == std::to_string(index))
            {
                return family.tag + std::to_string(family.first_tag_index + index * family.tag_index_step);
            }
        }
    }
    return {};
}

auto describe_vertex_inputs() -> std::string
{
    std::string description;
    for(const auto& family : VERTEX_INPUTS)
    {
        if(family.count == 0)
        {
            description += fmt::format("  {} -> {} ({})\n", family.name, family.tag, family.meaning);
            continue;
        }
        const int last = family.count - 1;
        description += fmt::format("  {0}0 to {0}{1} -> {2}{3} to {2}{4} ({5})\n",
                                   family.name,
                                   last,
                                   family.tag,
                                   family.first_tag_index,
                                   family.first_tag_index + last * family.tag_index_step,
                                   family.meaning);
    }
    return description;
}

auto make_eshader_source_text(const eshader_file& file, const fs::path& source_path) -> std::string
{
    return fmt::format("#line 1 \"{}\"\n{}{}", source_path.generic_string(), file.interface_lines, file.code);
}

auto make_eshader_varying_text(const eshader_file& file) -> std::string
{
    // Never empty: shaderc reports an empty file as a parse error, which would open every compile error of a shader
    // that has no inputs or outputs.
    std::string text = "// Generated from [inputs] and [outputs].\n";
    for(const auto& declaration : file.declarations)
    {
        text += make_varying_line(declaration);
    }
    return text;
}

auto get_eshader_varyings(const eshader_file& file) -> std::map<std::string, std::string>
{
    std::map<std::string, std::string> varyings;
    for(const auto& declaration : file.declarations)
    {
        if(hpp::string_view(declaration.name).starts_with(VARYING_PREFIX))
        {
            varyings[declaration.name] = declaration.interpolation.empty() ? declaration.type : declaration.interpolation + " " + declaration.type;
        }
    }
    return varyings;
}

} // namespace asset_compiler
} // namespace unravel
