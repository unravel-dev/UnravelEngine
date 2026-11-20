#include "material_graph_compiler.h"
#include "named_values.h"

#include <engine/meta/rendering/material_graph.hpp>
#include <engine/rendering/material.h>
#include <engine/rendering/skinning_palette_buffer.h>
#include <string_utils/utils.h>
#include <logging/logging.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <numeric>
#include <set>
#include <utility>

namespace unravel
{

namespace
{
constexpr uint32_t MAX_WIDTH = 4;
constexpr const char* COMPONENTS_XYZW = "xyzw";
constexpr const char* COMPONENTS_RGBA = "rgba";
constexpr const char* VARIABLE_PREFIX = "n_";
constexpr const char* SAMPLER_PREFIX = "s_mg_texture";
constexpr const char* PARAMETERS_UNIFORM = MATERIAL_GRAPH_PARAMETERS_UNIFORM;
/// What a Discard node's test calls (material_graph.sh): it discards in every pass but the GI card capture.
constexpr const char* DISCARD_MACRO = "mg_discard_if";
constexpr const char* NORMAL_CHANNELS_UNIFORM = MATERIAL_GRAPH_NORMAL_CHANNELS_UNIFORM;
/// The names code reads the first parameter registers through: MG_PARAM0..3 (add_instance_register_lines).
constexpr const char* INSTANCE_REGISTER_PREFIX = "MG_PARAM";
constexpr const char* INDENT = "\t";
constexpr const char* PARAMETER_NAME_SETTING = MATERIAL_PARAMETER_NAME_SETTING;
/// The pin a texture node's problems name: its texture.
constexpr const char* TEXTURE_PIN = "texture";
constexpr const char* MASK_SETTING = "mask";
constexpr const char* TWO_SIDED_SETTING = MATERIAL_TWO_SIDED_SETTING;
constexpr const char* OPACITY_MASK_PIN = "opacity_mask";
constexpr const char* EMISSIVE_PIN = "emissive";
constexpr const char* BASE_COLOR_PIN = "base_color";
constexpr const char* NORMAL_PIN = "normal";
constexpr const char* AMBIENT_OCCLUSION_PIN = "ambient_occlusion";
constexpr const char* SUBSURFACE_COLOR_PIN = "subsurface_color";
constexpr const char* SUBSURFACE_OPACITY_PIN = "subsurface_opacity";
constexpr const char* CAMERA_FADE_DISTANCE_PIN = "camera_fade_distance";
/// The node type whose value the emissive estimate multiplies (estimate_emissive).
constexpr const char* MULTIPLY_TYPE = "multiply";
/// The attributes node types the emissive estimate and the mask follow.
constexpr const char* MAKE_ATTRIBUTES_TYPE = "make_material_attributes";
constexpr const char* BLEND_ATTRIBUTES_TYPE = "blend_material_attributes";
/// The expression of a constant node: its value input as it is.
constexpr const char* CONSTANT_EXPRESSION = "{value}";
/// How many nodes deep the emissive estimate follows a link before it gives up.
constexpr int MAX_ESTIMATE_DEPTH = 32;
constexpr const char* WORLD_POSITION_OFFSET_PIN = "world_position_offset";
constexpr const char* MAX_WORLD_POSITION_OFFSET_SETTING = "max_world_position_offset";
/// The node type whose value a vertex does not have: World Position Offset reads the middle of the view for it.
constexpr const char* SCREEN_POSITION_TYPE = "screen_position";
constexpr const char* SWITCH_VALUE_SETTING = MATERIAL_STATIC_SWITCH_VALUE_SETTING;
/// The switch the compile option picks the input of (material_graph_compile_options::quality).
constexpr const char* QUALITY_SWITCH_TYPE = "quality_switch";
constexpr const char* SWITCH_FALSE_VALUE = MATERIAL_STATIC_SWITCH_FALSE;
constexpr const char* REROUTE_NAME_SETTING = "name";
constexpr const char* CUSTOM_OUTPUT_SETTING = "output";
constexpr const char* CUSTOM_CODE_SETTING = "code";
/// What a Custom node's function is named after: its node's variable.
constexpr const char* CUSTOM_FUNCTION_PREFIX = "mg_custom_";
/// Between a call's id and a function node's id in the ids of the nodes a call puts in the graph.
constexpr const char* FUNCTION_NODE_SEPARATOR = "__";
/// Functions calling functions: how deep the calls go before the compiler stops.
constexpr size_t MAX_FUNCTION_DEPTH = 8;
constexpr const char* FUNCTION_INPUT_TYPE_SETTING = "type";
/// A function input's pin taking the caller's link, and its default.
constexpr size_t FUNCTION_INPUT_CALLER_PIN = 0;
constexpr size_t FUNCTION_INPUT_DEFAULT_PIN = 1;

/// The variants every generated shader has.
constexpr const char* SHADER_VARIANTS =
    "[variants]\n"
    "// card_capture: for the surface cache card captures (fs_material_graph_gbuffer.sh).\n"
    "// instance_material: for batches whose instances bring their own values of the first parameter registers\n"
    "// (material instances sharing the draw), from vs_deferred_geom.eshader's *instanced_material variants.\n"
    "default\n"
    "card_capture        DEFERRED_GEOM_CARD_CAPTURE\n"
    "instance_material   MATERIAL_GRAPH_INSTANCE_MATERIAL";

/// The variants of a graph whose Opacity Mask clips: its shadow casters (fs_material_graph_shadow.sh), one per kind of
/// shadow map, as fs_shadowmaps_packdepth.eshader has them.
constexpr const char* SHADER_SHADOW_VARIANTS = "// shadow*: the shadow casters, clipped by the Opacity Mask (fs_material_graph_shadow.sh).\n"
                                               "shadow              SHADOW_CASTER\n"
                                               "shadow_linear       SHADOW_CASTER LINEAR\n"
                                               "shadow_vsm          SHADOW_CASTER PACK_VSM\n"
                                               "shadow_vsm_linear   SHADOW_CASTER PACK_VSM LINEAR";

/// The variants of the vertex shader of a graph with a vertex stage: the G-buffer ones of vs_deferred_geom.eshader
/// (*_material for the batches of instances sharing the draw, which only a graph whose vertex stage computes nothing of
/// its parameters has), the ones of vs_velocity.eshader as velocity_*, and position_only*, which write the moved
/// position alone for the fragment shaders that read nothing (the editor's picking and selection outline): bgfx makes a
/// program only of stages whose varyings match.
constexpr const char* VERTEX_SHADER_VARIANTS = "[variants]\n"
                                               "default\n"
                                               "card_capture                              DEFERRED_GEOM_CARD_CAPTURE\n"
                                               "skinned                                   SKINNED\n"
                                               "instanced                                 INSTANCED\n"
                                               "instanced_material                        INSTANCED INSTANCE_MATERIAL\n"
                                               "skinned_instanced                         SKINNED_INSTANCED\n"
                                               "skinned_instanced_material                SKINNED_INSTANCED INSTANCE_MATERIAL\n"
                                               "skinned_baked_instanced                   SKINNED_BAKED_INSTANCED\n"
                                               "skinned_textures                          SKINNED SHADER_BUFFER_TEXTURES\n"
                                               "skinned_instanced_textures                SKINNED_INSTANCED SHADER_BUFFER_TEXTURES\n"
                                               "skinned_instanced_material_textures       SKINNED_INSTANCED INSTANCE_MATERIAL SHADER_BUFFER_TEXTURES\n"
                                               "skinned_baked_instanced_textures          SKINNED_BAKED_INSTANCED SHADER_BUFFER_TEXTURES\n"
                                               "velocity                                  VELOCITY\n"
                                               "velocity_skinned                          VELOCITY SKINNED\n"
                                               "velocity_instanced                        VELOCITY INSTANCED\n"
                                               "velocity_skinned_instanced                VELOCITY SKINNED_INSTANCED\n"
                                               "velocity_skinned_baked_instanced          VELOCITY SKINNED_BAKED_INSTANCED\n"
                                               "velocity_skinned_textures                 VELOCITY SKINNED SHADER_BUFFER_TEXTURES\n"
                                               "velocity_skinned_instanced_textures       VELOCITY SKINNED_INSTANCED SHADER_BUFFER_TEXTURES\n"
                                               "velocity_skinned_baked_instanced_textures VELOCITY SKINNED_BAKED_INSTANCED SHADER_BUFFER_TEXTURES\n"
                                               "position_only                             POSITION_ONLY\n"
                                               "position_only_skinned                     POSITION_ONLY SKINNED\n"
                                               "position_only_skinned_textures            POSITION_ONLY SKINNED SHADER_BUFFER_TEXTURES";

/// What the vertex shader's variants read and write: vs_deferred_geom.eshader's, vs_velocity.eshader's with the normal,
/// tangent, bitangent and texture coordinates the vertex stage reads, and the position alone (POSITION_ONLY). The
/// G-buffer variants write what the graph's pixels read besides (add_vertex_output_lines), then the code.
constexpr const char* VERTEX_SHADER_INTERFACE = "[inputs]\n"
                                                "vec3 a_position\n"
                                                "vec4 a_normal\n"
                                                "vec4 a_tangent\n"
                                                "vec4 a_bitangent\n"
                                                "vec2 a_texcoord0\n"
                                                "#if SKINNED || SKINNED_INSTANCED || SKINNED_BAKED_INSTANCED\n"
                                                "vec4 a_weight\n"
                                                "vec4 a_indices\n"
                                                "#endif\n"
                                                "#if INSTANCED || SKINNED_INSTANCED || SKINNED_BAKED_INSTANCED\n"
                                                "vec4 i_data0\n"
                                                "#endif\n"
                                                "#if INSTANCED || SKINNED_BAKED_INSTANCED || INSTANCE_MATERIAL\n"
                                                "vec4 i_data1\n"
                                                "vec4 i_data2\n"
                                                "vec4 i_data3\n"
                                                "#endif\n"
                                                "#if (VELOCITY && (INSTANCED || SKINNED_BAKED_INSTANCED)) || INSTANCE_MATERIAL\n"
                                                "vec4 i_data4\n"
                                                "#endif\n"
                                                "#if (VELOCITY && (INSTANCED || SKINNED_BAKED_INSTANCED)) || (INSTANCED && INSTANCE_MATERIAL)\n"
                                                "vec4 i_data5\n"
                                                "vec4 i_data6\n"
                                                "#endif\n"
                                                "#if (VELOCITY && INSTANCED) || (INSTANCED && INSTANCE_MATERIAL)\n"
                                                "vec4 i_data7\n"
                                                "#endif\n"
                                                "\n"
                                                "[outputs]\n"
                                                "#if VELOCITY\n"
                                                "vec4 v_curr_pos\n"
                                                "vec4 v_prev_pos\n"
                                                "vec4 v_prev_static_pos\n"
                                                "#elif !POSITION_ONLY\n"
                                                "vec3 v_wpos\n"
                                                "vec3 v_pos\n"
                                                "vec3 v_wnormal = vec3(0.0, 0.0, 1.0)\n"
                                                "vec3 v_wtangent = vec3(1.0, 0.0, 0.0)\n"
                                                "vec3 v_wbitangent = vec3(0.0, 1.0, 0.0)\n"
                                                "vec2 v_texcoord0\n"
                                                "vec2 v_lod_params\n"
                                                "#if INSTANCE_MATERIAL\n"
                                                "flat vec4 v_material0 = vec4(1.0, 1.0, 1.0, 1.0)\n"
                                                "flat vec4 v_material1\n"
                                                "flat vec4 v_material2\n"
                                                "flat vec4 v_material3\n"
                                                "#endif";

/// The engine shader files the vertex shader includes before its code, from engine:/data/shaders, and the skinning
/// palettes for its skinned variants; then the file defining the offset's call, and the main() of each kind of variant.
constexpr std::array<const char*, 3> VERTEX_HEADER_INCLUDES = {"common.sh", "deferred_geom/normal_basis.sh", "material_graph/material_graph.sh"};
constexpr const char* VERTEX_SKINNING_INCLUDE = "skinning_palette.sh";
constexpr const char* VERTEX_OFFSET_INCLUDE = "material_graph/vs_material_graph.sh";
constexpr const char* VERTEX_MAIN_INCLUDE = "deferred_geom/vs_deferred_geom.sh";
constexpr const char* VERTEX_VELOCITY_MAIN_INCLUDE = "velocity/vs_velocity.sh";

/// What every variant reads: what vs_deferred_geom.eshader writes. A graph with a vertex stage reads what its own
/// vertex shader writes besides (add_vertex_output_lines), then the code.
constexpr const char* SHADER_INPUTS = "[inputs]\n"
                                      "vec3 v_wpos\n"
                                      "vec3 v_pos\n"
                                      "vec3 v_wnormal\n"
                                      "vec3 v_wtangent\n"
                                      "vec3 v_wbitangent\n"
                                      "vec2 v_texcoord0\n"
                                      "vec2 v_lod_params\n"
                                      "#ifdef MATERIAL_GRAPH_INSTANCE_MATERIAL\n"
                                      "flat vec4 v_material0\n"
                                      "flat vec4 v_material1\n"
                                      "flat vec4 v_material2\n"
                                      "flat vec4 v_material3\n"
                                      "#endif";

/// The most Vertex Interpolators a graph reads (MaterialInputs::interpolator0..3).
constexpr uint32_t MAX_INTERPOLATORS = 4;
/// The Vertex Interpolators a graph that also reads the object's transform keeps to: its three varyings and the
/// instance's four leave this many of the 15 that phones and browsers guarantee.
constexpr uint32_t MAX_INTERPOLATORS_WITH_OBJECT_DATA = 2;
/// What code reads of the object's transform (MATERIAL_OBJECT_DATA): its inputs and the functions reading them.
constexpr std::array<const char*, 4> OBJECT_DATA_CODES = {"i.object_", "mg_object_to_world_", "mg_world_to_object_", "mg_object_scale"};

/// What generated code reads that changes without an edit: the time, the camera and the screen (MaterialInputs).
constexpr std::array<const char*, 5> CHANGING_INPUT_CODES = {"i.time", "i.view_direction", "i.camera_position", "i.screen_uv", "i.camera_direction"};

/// The engine shader files every generated shader includes before its code, from engine:/data/shaders.
constexpr std::array<const char*, 3> SHADER_HEADER_INCLUDES = {"common.sh", "lighting.sh", "material_graph/material_graph.sh"};

/// The file holding main(), included after the code.
constexpr const char* SHADER_MAIN_INCLUDE = "material_graph/fs_material_graph_gbuffer.sh";

/// The variants of a surface of the translucency pass (Blend Mode Translucent, Premultiplied, Additive or Modulate):
/// one, the forward main. The G-buffer never draws such a material (deferred::get_g_buffer_graph).
constexpr const char* FORWARD_SHADER_VARIANTS = "[variants]\n"
                                                "// default: the translucency pass (fs_material_graph_forward.sh).\n"
                                                "default";
/// The variant of an Order Independent surface: its weighted sums into the order-independent targets.
constexpr const char* FORWARD_OIT_VARIANT = "// oit: the order-independent targets (fs_material_graph_forward.sh).\n"
                                            "oit                 FORWARD_OIT";
/// The variants lit by the GI's radiance cache (mg_forward.sh FORWARD_GI), drawn where the view runs the GI.
constexpr const char* FORWARD_GI_VARIANT = "// gi: lit by the GI's radiance cache (mg_forward.sh FORWARD_GI).\n"
                                           "gi                  FORWARD_GI";
constexpr const char* FORWARD_OIT_GI_VARIANT = "oit_gi              FORWARD_OIT FORWARD_GI";
/// The forward main and the header of what it reads besides the graph.
constexpr const char* FORWARD_MAIN_INCLUDE = "material_graph/fs_material_graph_forward.sh";
constexpr const char* FORWARD_HEADER_INCLUDE = "material_graph/mg_forward.sh";

/// The file holding main() of the shadow caster variants.
constexpr const char* SHADER_SHADOW_MAIN_INCLUDE = "material_graph/fs_material_graph_shadow.sh";

/// A decal material's shader: the colour path's variant and the decal buffers' (fs_material_graph_decal.sh), what
/// decal/vs_decal.eshader writes, the header it reads the view through, and its main().
constexpr const char* DECAL_SHADER_INTERFACE = "[variants]\n"
                                               "// default: the colour path, into the G-buffer's base colour and emissive.\n"
                                               "// full: the decal buffers' path, for decals writing normals or reading the surface.\n"
                                               "default\n"
                                               "full                DECAL_FULL\n"
                                               "\n"
                                               "[inputs]\n"
                                               "flat vec4 v_decal_local_to_world0\n"
                                               "flat vec4 v_decal_local_to_world1\n"
                                               "flat vec4 v_decal_local_to_world2\n"
                                               "flat vec4 v_decal_world_to_local0\n"
                                               "flat vec4 v_decal_world_to_local1\n"
                                               "flat vec4 v_decal_world_to_local2\n"
                                               "flat vec4 v_decal_fade\n"
                                               "flat vec4 v_decal_angle\n"
                                               "flat vec4 v_material0\n"
                                               "flat vec4 v_material1\n"
                                               "flat vec4 v_material2\n"
                                               "flat vec4 v_material3";
constexpr const char* DECAL_HEADER_INCLUDE = "material_graph/mg_decal.sh";
constexpr const char* DECAL_MAIN_INCLUDE = "material_graph/fs_material_graph_decal.sh";
/// A post-process material's shader: the quad of vs_clip_quad.eshader, the header it reads the view through, its main().
constexpr const char* POST_PROCESS_SHADER_INTERFACE = "[variants]\n"
                                                      "default\n"
                                                      "\n"
                                                      "[inputs]\n"
                                                      "vec2 v_texcoord0";
constexpr const char* POST_PROCESS_HEADER_INCLUDE = "material_graph/mg_post.sh";
constexpr const char* POST_PROCESS_MAIN_INCLUDE = "material_graph/fs_material_graph_post.sh";
/// The made-up view a swatch of a decal or post-process material reads its Scene Textures from.
constexpr const char* PREVIEW_VIEW_INCLUDE = "material_graph/mg_preview_view.sh";

/// The preview shader: one variant, the quad of vs_clip_quad.eshader, and its main().
constexpr const char* PREVIEW_INTERFACE = "[variants]\n"
                                          "default\n"
                                          "\n"
                                          "[inputs]\n"
                                          "vec2 v_texcoord0\n"
                                          "\n"
                                          "[code]";
constexpr const char* PREVIEW_MAIN_INCLUDE = "material_graph/fs_material_graph_preview.sh";
/// How evaluate_preview gives one node's swatch: MG_PREVIEW(index, colour), in mg_preview. fxc (Direct3D 11) cannot
/// return early before code that takes derivatives, so it would flatten every early return, which for a graph of
/// hundreds of nodes takes it minutes; it selects the colour instead and computes every node, as its flattened code
/// would. The other compilers return, skipping the nodes after the one asked for.
constexpr const char* PREVIEW_SELECTION = "#if BGFX_SHADER_LANGUAGE_HLSL && !BGFX_SHADER_LANGUAGE_DXIL\n"
                                          "#define MG_PREVIEW(index, colour) mg_preview = abs(node - (index)) < 0.5 ? (colour) : mg_preview\n"
                                          "#else\n"
                                          "#define MG_PREVIEW(index, colour) if(abs(node - (index)) < 0.5) return (colour)\n"
                                          "#endif";

auto make_include_line(const char* file) -> std::string
{
    return fmt::format("#include \"{}\"", file);
}

/// What a link reads: the node (index), its output and the components picked; node -1 while unlinked.
struct link_source
{
    int node{-1};
    const material_pin_def* output{};
    std::vector<int> components;

    auto is_linked() const -> bool
    {
        return node >= 0;
    }
};

/// What the compiler knows of one node while it works.
struct node_state
{
    const material_graph_node* node{};
    const material_node_def* def{};
    /// For each input of the node type: what its own link reads.
    std::vector<link_source> sources;
    /// For each input of the node type: what the links of its components read ("pin.c" entries).
    std::vector<std::array<link_source, 4>> component_sources;
    /// For each input of the node type: a vector built from its components, at least one of which is linked.
    std::vector<bool> is_composed;
    /// For each input of the node type: the graph's entry for it, or null.
    std::vector<const material_graph_input*> authored;
    /// 0 not resolved, 1 being resolved, 2 resolved: finds loops.
    int resolve_state{};
    std::string variable;
    /// How the node's value is read once emitted: its variable, or its code for an inline node.
    std::string value_code;
    bool is_emitted{};
    /// Its code reads the time, the camera or the screen where the lines it is emitted as do not show it (a Custom
    /// node's function body).
    bool reads_changing_input{};
    /// A function call: the code of each output.
    std::vector<std::string> output_codes;
};

auto is_code_name_char(char c) -> bool
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' || c == '[' || c == ']';
}

/// True when a swizzle can follow @p code as it is: a name, a member, or one call or parenthesis group as a whole.
auto is_postfix_safe(const std::string& code) -> bool
{
    if(code.empty())
    {
        return false;
    }
    if(std::all_of(code.begin(), code.end(), is_code_name_char))
    {
        return true;
    }
    if(code.back() != ')')
    {
        return false;
    }
    // The parenthesis the last one closes: what stands before it must be a function name or nothing.
    int depth = 0;
    for(size_t i = code.size(); i-- > 0;)
    {
        depth += code[i] == ')' ? 1 : (code[i] == '(' ? -1 : 0);
        if(depth == 0)
        {
            return std::all_of(code.begin(), code.begin() + std::ptrdiff_t(i), is_code_name_char);
        }
    }
    return false;
}

/// @p code made safe to put a swizzle after.
auto make_postfix_safe(const std::string& code) -> std::string
{
    return is_postfix_safe(code) ? code : fmt::format("({})", code);
}

/// @p text fit for a // comment of the shader: authored text never ends the comment and starts code.
auto make_comment_text(const std::string& text) -> std::string
{
    std::string safe = text;
    std::replace_if(
        safe.begin(),
        safe.end(),
        [](char c)
        {
            // A backslash ending a line would carry the comment over the next one.
            return static_cast<unsigned char>(c) < ' ' || c == '\\';
        },
        ' ');
    return safe;
}

auto format_float_literal(float value) -> std::string
{
    if(!std::isfinite(value))
    {
        return "0.0";
    }
    std::string text = fmt::format("{}", value);
    if(text.find_first_of(".eEn") == std::string::npos)
    {
        text += ".0";
    }
    return text;
}

/// A float literal that stays one operand anywhere: a negative one is wrapped, so "-{in}" never reads "--1.0".
auto format_scalar(float value) -> std::string
{
    const std::string text = format_float_literal(value);
    return text.front() == '-' ? fmt::format("({})", text) : text;
}

auto get_splat(uint32_t width) -> std::string
{
    return fmt::format("{}_splat", get_material_value_shader_type(get_material_value_type(width)));
}

auto format_literal(const math::vec4& value, uint32_t width, bool broadcast) -> std::string
{
    if(width == 1)
    {
        return format_scalar(value.x);
    }
    const bool all_equal = broadcast || std::all_of(&value[1],
                                                    &value[0] + width,
                                                    [&](float component)
                                                    {
                                                        return component == value.x;
                                                    });
    if(all_equal)
    {
        return fmt::format("{}({})", get_splat(width), format_float_literal(value.x));
    }
    std::vector<std::string> literals;
    for(uint32_t i = 0; i < width; ++i)
    {
        literals.push_back(format_float_literal(value[int(i)]));
    }
    return fmt::format("{}({})", get_material_value_shader_type(get_material_value_type(width)), fmt::join(literals, ", "));
}

/// Whether @p value, stored for an input that takes one number, holds a vector: a component after the first that is
/// set and differs from it. Trailing zeros are how a single number is stored.
auto is_vector_value_given(const math::vec4& value) -> bool
{
    for(int i = 1; i < 4; ++i)
    {
        if(value[i] != 0.0f && value[i] != value.x)
        {
            return true;
        }
    }
    return false;
}

/// @p value as a list, without its trailing zeros: "[0.0, -0.12]".
auto format_vector_value(const math::vec4& value) -> std::string
{
    int count = 4;
    while(count > 1 && value[count - 1] == 0.0f)
    {
        --count;
    }
    std::vector<std::string> parts;
    for(int i = 0; i < count; ++i)
    {
        parts.push_back(format_float_literal(value[i]));
    }
    return fmt::format("[{}]", fmt::join(parts, ", "));
}

/// Index of a component letter (x, y, z, w or r, g, b, a); -1 for any other character.
auto get_component_index(char c) -> int
{
    for(int i = 0; i < int(MAX_WIDTH); ++i)
    {
        if(c == COMPONENTS_XYZW[i] || c == COMPONENTS_RGBA[i])
        {
            return i;
        }
    }
    return -1;
}

/// The components a swizzle (".rgb", "xy") picks; empty when it holds anything else or more than four.
auto parse_swizzle(hpp::string_view swizzle) -> std::vector<int>
{
    if(!swizzle.empty() && swizzle.front() == '.')
    {
        swizzle.remove_prefix(1);
    }
    std::vector<int> components;
    if(swizzle.empty() || swizzle.size() > MAX_WIDTH)
    {
        return components;
    }
    for(const char c : swizzle)
    {
        const int index = get_component_index(c);
        if(index < 0)
        {
            return {};
        }
        components.push_back(index);
    }
    return components;
}

/// True when @p components are every component of a value of @p width, in order.
auto is_whole_value(const std::vector<int>& components, uint32_t width) -> bool
{
    if(components.size() != width)
    {
        return false;
    }
    for(size_t i = 0; i < components.size(); ++i)
    {
        if(components[i] != int(i))
        {
            return false;
        }
    }
    return true;
}

/// @p components read from @p code, a value of @p width components; a component the value lacks reads 0.
auto apply_components(const std::string& code, uint32_t width, const std::vector<int>& components) -> std::string
{
    if(components.empty())
    {
        return code;
    }
    if(is_whole_value(components, width))
    {
        return code;
    }
    const auto read = [&](int component) -> std::string
    {
        if(component >= int(width))
        {
            return "0.0";
        }
        return width == 1 ? code : fmt::format("{}.{}", make_postfix_safe(code), COMPONENTS_XYZW[component]);
    };
    const bool in_range = std::all_of(components.begin(),
                                      components.end(),
                                      [&](int component)
                                      {
                                          return component < int(width);
                                      });
    if(components.size() == 1)
    {
        return read(components.front());
    }
    if(in_range && width > 1)
    {
        std::string swizzle;
        for(const int component : components)
        {
            swizzle += COMPONENTS_XYZW[component];
        }
        return fmt::format("{}.{}", make_postfix_safe(code), swizzle);
    }
    if(in_range)
    {
        return fmt::format("{}({})", get_splat(uint32_t(components.size())), code);
    }
    std::vector<std::string> reads;
    for(const int component : components)
    {
        reads.push_back(read(component));
    }
    return fmt::format("{}({})", get_material_value_shader_type(get_material_value_type(uint32_t(components.size()))), fmt::join(reads, ", "));
}

auto make_first_components(uint32_t count) -> std::vector<int>
{
    std::vector<int> components(count);
    for(uint32_t i = 0; i < count; ++i)
    {
        components[i] = int(i);
    }
    return components;
}

/**
 * @brief Compiles one graph into one result.
 */
class graph_compiler
{
public:
    graph_compiler(const material_graph& graph, const material_graph_compile_options& options, material_graph_compile_result& result)
        : flat_(graph)
        , graph_(flat_)
        , options_(options)
        , result_(result)
    {
    }

    void run()
    {
        expand_function_calls();
        run_flat();
        // The nodes the calls added were for the code: the result describes the graph's own.
        map_function_diagnostics();
        result_.nodes.resize(std::min(result_.nodes.size(), authored_count_));
    }

private:
    void run_flat()
    {
        index_nodes();
        resolve_links();
        resolve_types();
        if(options_.is_preview)
        {
            mark_all_used();
        }
        else
        {
            mark_used();
        }
        check_domain_nodes();
        collect_node_aspects();
        allocate_parameters();
        allocate_textures();
        collect_static_switches();
        if(options_.is_preview)
        {
            emit_previews();
            assemble_preview_shader();
            return;
        }
        emit_surface();
        if(is_surface_domain())
        {
            collect_vertex_outputs();
            estimate_emissive();
            emit_vertex();
        }
        assemble_shader();
        assemble_vertex_shader();
    }

private:
    /// Reports a problem once: the vertex stage emits again nodes the fragment stage did.
    void report(material_graph_severity severity, const std::string& node_id, const std::string& pin, std::string message)
    {
        const bool is_known = std::any_of(result_.diagnostics.begin(),
                                          result_.diagnostics.end(),
                                          [&](const material_graph_diagnostic& diagnostic)
                                          {
                                              return diagnostic.severity == severity && diagnostic.node_id == node_id &&
                                                     diagnostic.pin == pin && diagnostic.message == message;
                                          });
        if(!is_known)
        {
            result_.diagnostics.push_back({severity, node_id, pin, std::move(message)});
        }
    }

    auto get_info(size_t index) -> material_graph_node_info&
    {
        return result_.nodes[index];
    }

    void index_nodes()
    {
        states_.resize(graph_.nodes.size());
        result_.nodes.resize(graph_.nodes.size());
        std::set<std::string> ids;
        std::set<std::string> variables;
        for(size_t i = 0; i < graph_.nodes.size(); ++i)
        {
            const auto& node = graph_.nodes[i];
            auto& state = states_[i];
            state.node = &node;
            state.def = find_material_node_def(node.type);
            if(!ids.insert(node.id).second)
            {
                report(material_graph_severity::error,
                       node.id,
                       {},
                       fmt::format("Another node has the id \"{}\": links reach only the first.", node.id));
            }
            state.variable = make_variable_name(node.id, variables);
            if(!state.def)
            {
                report(material_graph_severity::error, node.id, {}, fmt::format("Unknown node type \"{}\".", node.type));
                continue;
            }
            auto& info = get_info(i);
            info.input_types.assign(state.def->inputs.size(), material_value_type::float1);
            info.output_types.assign(state.def->outputs.size(), material_value_type::float1);
            const auto domain = find_material_domain_of_output_type(node.type);
            if(!domain)
            {
                continue;
            }
            if(output_ < 0)
            {
                output_ = int(i);
                result_.domain = *domain;
            }
            else
            {
                report(material_graph_severity::warning, node.id, {}, "Only the graph's first output node is used.");
            }
        }
        if(output_ < 0 && !is_material_function_graph(graph_))
        {
            report(material_graph_severity::error, {}, {}, "The graph has no output node.");
        }
    }

    auto is_surface_domain() const -> bool
    {
        return result_.domain == material_domain::surface;
    }

    /// Whether node @p index can run in the graph's domain: a node of another domain's reads what this one's main does
    /// not give, so its code is its default value.
    auto is_in_domain(size_t index) const -> bool
    {
        const auto* def = states_[index].def;
        return def == nullptr || is_material_node_in_domain(*def, result_.domain);
    }

    /// Reports every used node that does not run in the graph's domain, and what the Scene Texture nodes read: the view
    /// textures the draw binds, and whether one reads another pixel than its own.
    void check_domain_nodes()
    {
        for(size_t i = 0; i < states_.size(); ++i)
        {
            const auto& state = states_[i];
            if(!state.def || !get_info(i).is_used || int(i) == output_)
            {
                continue;
            }
            if(!is_in_domain(i))
            {
                report(material_graph_severity::error,
                       state.node->id,
                       {},
                       fmt::format("{} works in {} materials, not in a {} material: it reads its default value.",
                                   state.def->label,
                                   get_material_node_domains_text(*state.def),
                                   get_material_domain_label(result_.domain)));
                continue;
            }
            if(state.node->type == MATERIAL_DEPTH_FADE_TYPE && can_surface_read_scene(state.node->id, "Scene Depth"))
            {
                result_.scene_textures |= material_scene_texture_depth;
            }
            if(state.node->type == MATERIAL_SCENE_TEXTURE_TYPE)
            {
                collect_scene_texture(i);
            }
        }
    }

    void collect_scene_texture(size_t index)
    {
        const auto& state = states_[index];
        const auto* setting = find_material_setting_def(*state.def, MATERIAL_SCENE_TEXTURE_SOURCE_SETTING);
        const std::string key = setting != nullptr ? get_checked_setting(index, *setting) : std::string{};
        const auto* source = find_material_scene_texture_source(key);
        if(source == nullptr)
        {
            return;
        }
        if((source->domains & get_material_domain_bit(result_.domain)) == 0)
        {
            report(material_graph_severity::error,
                   state.node->id,
                   MATERIAL_SCENE_TEXTURE_SOURCE_SETTING,
                   fmt::format("A {} material cannot read {}: it reads black.", get_material_domain_label(result_.domain), source->label));
            return;
        }
        if(!can_surface_read_scene(state.node->id, source->label))
        {
            return;
        }
        result_.scene_textures |= source->texture_bit;
        result_.samples_scene_neighbours = result_.samples_scene_neighbours || is_input_linked(state, 0);
        if(result_.domain == material_domain::decal)
        {
            result_.decal_reads_surface = true;
        }
    }

    /// Whether the surface graph may read the scene's colour or depth, which only the translucency pass has; reports
    /// why not on node @p node_id otherwise. Decal and post-process graphs check their sources by domain.
    auto can_surface_read_scene(const std::string& node_id, const std::string& what) -> bool
    {
        if(!is_surface_domain() || is_output_forward())
        {
            return true;
        }
        report(material_graph_severity::error,
               node_id,
               {},
               fmt::format("Only a surface of the translucency pass (Blend Mode Translucent, Premultiplied, Additive or "
                           "Modulate) reads {}: this one reads black.",
                           what));
        return false;
    }

    /// A surface's read of the scene its shader does not have (reported by check_domain_nodes): a Scene Texture or a
    /// Depth Fade outside the translucency pass, or a Scene Texture of a source only decals and post-process materials
    /// read. It reads its default value.
    auto is_refused_scene_read(size_t index) const -> bool
    {
        const auto& state = states_[index];
        if(!is_surface_domain() || state.node == nullptr)
        {
            return false;
        }
        const bool is_scene_texture = state.node->type == MATERIAL_SCENE_TEXTURE_TYPE;
        if(!is_scene_texture && state.node->type != MATERIAL_DEPTH_FADE_TYPE)
        {
            return false;
        }
        if(!is_output_forward())
        {
            return true;
        }
        if(!is_scene_texture || state.def == nullptr)
        {
            return false;
        }
        const auto* setting = find_material_setting_def(*state.def, MATERIAL_SCENE_TEXTURE_SOURCE_SETTING);
        const auto* source = setting != nullptr ? find_material_scene_texture_source(get_setting(state, setting->name)) : nullptr;
        return source == nullptr || (source->domains & get_material_domain_bit(material_domain::surface)) == 0;
    }

    /// The Surface Output's Blend Mode draws in the translucency pass (read from its setting: the compile result
    /// learns it later, when the output is emitted).
    auto is_output_forward() const -> bool
    {
        return output_ >= 0 && states_[size_t(output_)].node != nullptr &&
               is_material_blend_mode_forward(get_material_blend_mode(*states_[size_t(output_)].node));
    }

    /// A lit surface whose textures and scene reads leave the radiance cache's stages free (the "gi" variant).
    auto fits_gi_variant() const -> bool
    {
        const uint32_t stages = uint32_t(result_.textures.size()) + count_forward_scene_reads(result_.scene_textures);
        return result_.shading_model == material_shading_model::default_lit && stages <= MATERIAL_GRAPH_MAX_FORWARD_GI_TEXTURES;
    }

    /// The Surface Output bends the view (Refraction linked or set off 1) and its Blend Mode refracts.
    auto is_output_refracting() const -> bool
    {
        if(output_ < 0 || states_[size_t(output_)].node == nullptr || states_[size_t(output_)].def == nullptr)
        {
            return false;
        }
        const auto& state = states_[size_t(output_)];
        if(!is_material_output_pin_used(*state.node, MATERIAL_REFRACTION_PIN))
        {
            return false;
        }
        const size_t pin = find_input_pin(*state.def, MATERIAL_REFRACTION_PIN);
        return pin < state.def->inputs.size() && (is_input_linked(state, pin) || state.is_composed[pin] || get_value(state, pin).x != 1.0f);
    }

    /// A refracting lit surface that may be rough (its Roughness, or Glossiness in the specular workflow, linked or
    /// rougher than MATERIAL_ROUGH_REFRACTION_MIN): what it shows through itself blurs by its roughness.
    auto is_output_rough_refracting() const -> bool
    {
        if(!is_output_refracting())
        {
            return false;
        }
        const auto& state = states_[size_t(output_)];
        if(get_material_shading_model(*state.node) == material_shading_model::unlit)
        {
            return false;
        }
        const bool is_specular = is_material_specular_workflow(*state.node);
        const size_t pin = find_input_pin(*state.def, is_specular ? MATERIAL_GLOSSINESS_PIN : MATERIAL_ROUGHNESS_PIN);
        if(pin >= state.def->inputs.size())
        {
            return false;
        }
        if(is_input_linked(state, pin) || state.is_composed[pin])
        {
            return true;
        }
        const float value = get_value(state, pin).x;
        return (is_specular ? 1.0f - value : value) >= MATERIAL_ROUGH_REFRACTION_MIN;
    }

    /// What the Surface Output's Refraction reads of the scene behind (material_scene_texture_bits).
    auto get_output_refraction_reads() const -> uint32_t
    {
        if(!is_output_refracting())
        {
            return 0;
        }
        const uint32_t reads = material_scene_texture_color | material_scene_texture_depth;
        return is_output_rough_refracting() ? reads | material_scene_texture_blurred_color : reads;
    }

    static auto make_variable_name(const std::string& id, std::set<std::string>& taken) -> std::string
    {
        const std::string base = VARIABLE_PREFIX + sanitize_material_graph_identifier(id);
        std::string name = base;
        for(int number = 1; !taken.insert(name).second; ++number)
        {
            name = fmt::format("{}_{}", base, number);
        }
        return name;
    }

    void resolve_links()
    {
        for(size_t i = 0; i < states_.size(); ++i)
        {
            auto& state = states_[i];
            if(!state.def)
            {
                continue;
            }
            const auto& inputs = state.def->inputs;
            state.sources.assign(inputs.size(), {});
            state.component_sources.assign(inputs.size(), {});
            state.is_composed.assign(inputs.size(), false);
            state.authored.assign(inputs.size(), nullptr);
            for(size_t pin = 0; pin < inputs.size(); ++pin)
            {
                state.authored[pin] = find_material_graph_input(*state.node, inputs[pin].name);
                if(state.authored[pin])
                {
                    state.sources[pin] = resolve_link(state, inputs[pin], inputs[pin].name, state.authored[pin]->link);
                }
            }
            resolve_component_links(state);
        }
        resolve_named_reroutes();
    }

    /// The name a Named Reroute declaration is read by: its name setting, or its id.
    auto get_reroute_name(const node_state& state) const -> std::string
    {
        const std::string name = get_setting(state, REROUTE_NAME_SETTING);
        return name.empty() ? state.node->id : name;
    }

    /// Links each Named Reroute Usage's hidden input to the declaration its name names.
    void resolve_named_reroutes()
    {
        std::map<std::string, size_t> declarations;
        for(size_t i = 0; i < states_.size(); ++i)
        {
            if(!states_[i].def || states_[i].def->code != material_node_code::named_reroute_declaration)
            {
                continue;
            }
            const std::string name = get_reroute_name(states_[i]);
            if(!declarations.emplace(name, i).second)
            {
                report(material_graph_severity::error,
                       states_[i].node->id,
                       REROUTE_NAME_SETTING,
                       fmt::format("Another Named Reroute is named \"{}\"; usages read the first.", name));
            }
        }
        for(auto& state : states_)
        {
            if(!state.def || state.def->code != material_node_code::named_reroute)
            {
                continue;
            }
            const std::string name = get_setting(state, REROUTE_NAME_SETTING);
            const auto found = declarations.find(name);
            if(found == declarations.end())
            {
                report(material_graph_severity::error,
                       state.node->id,
                       REROUTE_NAME_SETTING,
                       name.empty() ? std::string("Names no Named Reroute: it reads 0.")
                                    : fmt::format("No Named Reroute is named \"{}\": it reads 0.", name));
                continue;
            }
            const auto& declaration_def = *states_[found->second].def;
            state.sources[0] = {int(found->second), &declaration_def.outputs.front(), {}};
        }
    }

    /// What @p link, written on the entry @p entry of input @p pin_def, reads; unlinked when it reads nothing valid.
    auto resolve_link(const node_state& state, const material_pin_def& pin_def, const std::string& entry, const std::string& link)
        -> link_source
    {
        if(link.empty())
        {
            return {};
        }
        const std::string& id = state.node->id;
        if(pin_def.is_value_only)
        {
            report(material_graph_severity::error, id, entry, fmt::format("{} takes no link; its value is used.", pin_def.label));
            return {};
        }
        const auto end = parse_material_graph_link(link);
        const int source = find_material_graph_node(graph_, end.node);
        if(source < 0)
        {
            report(material_graph_severity::error, id, entry, fmt::format("Links \"{}\", which is not a node of the graph.", end.node));
            return {};
        }
        const auto* source_def = states_[size_t(source)].def;
        if(!source_def || source_def->outputs.empty())
        {
            return {};
        }
        const auto* output = end.pin.empty() ? &source_def->outputs.front() : find_material_pin_def(source_def->outputs, end.pin);
        if(!output)
        {
            report(material_graph_severity::error,
                   id,
                   entry,
                   fmt::format("Links output \"{}\" of \"{}\", a {}, which has none of that name.", end.pin, end.node, source_def->label));
            return {};
        }
        link_source resolved{source, output, parse_swizzle(end.components)};
        if(!end.components.empty() && resolved.components.empty())
        {
            report(material_graph_severity::error,
                   id,
                   entry,
                   fmt::format("\"{}\" is not one to four of x, y, z, w (or r, g, b, a).", end.components));
            return {};
        }
        return resolved;
    }

    /// The "pin.c" entries: links into one component of a vector input, which then is built from its components.
    void resolve_component_links(node_state& state)
    {
        const auto& inputs = state.def->inputs;
        for(const auto& entry : state.node->inputs)
        {
            const auto parsed = parse_material_graph_pin(entry.pin);
            const auto* pin_def = find_material_pin_def(inputs, parsed.pin);
            if(!pin_def)
            {
                report(material_graph_severity::warning,
                       state.node->id,
                       entry.pin,
                       fmt::format("A {} has no input \"{}\"; the entry is ignored.", state.def->label, parsed.pin));
                continue;
            }
            if(parsed.pin == entry.pin)
            {
                continue;
            }
            const size_t pin = size_t(pin_def - inputs.data());
            if(!can_split_material_pin(*pin_def) || parsed.component < 0 ||
               parsed.component >= int(get_material_value_width(pin_def->type)))
            {
                report(material_graph_severity::warning,
                       state.node->id,
                       entry.pin,
                       fmt::format("{} has no component \"{}\"; the entry is ignored.", pin_def->label, entry.pin.substr(parsed.pin.size() + 1)));
                continue;
            }
            auto& source = state.component_sources[pin][size_t(parsed.component)];
            source = resolve_link(state, *pin_def, entry.pin, entry.link);
            state.is_composed[pin] = state.is_composed[pin] || source.is_linked();
        }
        for(size_t pin = 0; pin < inputs.size(); ++pin)
        {
            if(state.is_composed[pin] && state.sources[pin].is_linked())
            {
                report(material_graph_severity::warning,
                       state.node->id,
                       inputs[pin].name,
                       fmt::format("{} is built from its components; its own link is ignored.", inputs[pin].label));
                state.sources[pin] = {};
            }
        }
    }

    void resolve_types()
    {
        for(size_t i = 0; i < states_.size(); ++i)
        {
            resolve_type(i);
        }
    }

    /// The width of what @p source reads, resolving its node first; 0 while unlinked. A link that closes a loop or
    /// reads components its output lacks is cut.
    auto resolve_source_width(const node_state& state, link_source& source, const std::string& entry) -> uint32_t
    {
        if(!source.is_linked())
        {
            return 0;
        }
        if(states_[size_t(source.node)].resolve_state == 1)
        {
            report(material_graph_severity::error, state.node->id, entry, "This link closes a loop; it is ignored.");
            source = {};
            return 0;
        }
        resolve_type(size_t(source.node));
        const uint32_t output_width = get_material_value_width(get_output_type(size_t(source.node), *source.output));
        for(const int component : source.components)
        {
            if(component >= int(output_width))
            {
                report(material_graph_severity::error,
                       state.node->id,
                       entry,
                       fmt::format("Reads component {} of a {}; the link is ignored.",
                                   COMPONENTS_XYZW[component],
                                   get_material_value_type_name(get_material_value_type(output_width))));
                source = {};
                return 0;
            }
        }
        return source.components.empty() ? output_width : uint32_t(source.components.size());
    }

    /// The width a resolved link source gives; 0 while unlinked.
    auto get_source_width(const link_source& source) -> uint32_t
    {
        if(!source.is_linked())
        {
            return 0;
        }
        return source.components.empty() ? get_material_value_width(get_output_type(size_t(source.node), *source.output))
                                         : uint32_t(source.components.size());
    }

    auto get_output_type(size_t node, const material_pin_def& output) -> material_value_type
    {
        // A call's outputs are as wide as the function's results.
        if(const int result = get_call_result_pin(node, output); result >= 0)
        {
            return get_info(node).input_types[size_t(result)];
        }
        return output.type == material_value_type::dynamic ? get_info(node).type : output.type;
    }

    /// The hidden input of a call holding the result its output @p output gives; -1 for other nodes.
    auto get_call_result_pin(size_t node, const material_pin_def& output) const -> int
    {
        const auto* def = states_[node].def;
        if(def == nullptr || def->code != material_node_code::function_call)
        {
            return -1;
        }
        const auto offset = size_t(&output - def->outputs.data());
        return offset < def->outputs.size() ? int(MATERIAL_FUNCTION_MAX_INPUTS + offset) : -1;
    }

    void resolve_type(size_t index)
    {
        auto& state = states_[index];
        if(state.resolve_state != 0 || !state.def)
        {
            return;
        }
        state.resolve_state = 1;
        const auto& def = *state.def;
        std::vector<uint32_t> source_widths(def.inputs.size());
        for(size_t pin = 0; pin < def.inputs.size(); ++pin)
        {
            source_widths[pin] = resolve_source_width(state, state.sources[pin], def.inputs[pin].name);
            for(size_t component = 0; component < state.component_sources[pin].size(); ++component)
            {
                const std::string entry =
                    make_material_graph_component_pin(def.inputs[pin].name, int(component), get_material_component_letters(def.inputs[pin]));
                resolve_source_width(state, state.component_sources[pin][component], entry);
            }
        }
        auto& info = get_info(index);
        // Textures and attributes are no numbers: they widen nothing, and pass on through the nodes that keep a type.
        std::vector<material_value_type> source_types(def.inputs.size(), material_value_type::dynamic);
        for(size_t pin = 0; pin < def.inputs.size(); ++pin)
        {
            if(state.sources[pin].is_linked() && !is_material_value_numeric(get_source_type(state.sources[pin])))
            {
                source_types[pin] = get_source_type(state.sources[pin]);
                source_widths[pin] = 0;
            }
        }
        const uint32_t dynamic_width = find_dynamic_width(def, source_widths);
        info.type = find_node_type(state, source_widths, dynamic_width);
        if(const auto kept = find_kept_type(state, source_types); kept != material_value_type::dynamic)
        {
            info.type = kept;
        }
        for(size_t pin = 0; pin < def.inputs.size(); ++pin)
        {
            info.input_types[pin] = find_input_type(def, def.inputs[pin], dynamic_width, source_widths[pin]);
            const bool keeps_type = def.inputs[pin].type == material_value_type::dynamic && source_types[pin] != material_value_type::dynamic &&
                                    (keeps_own_input_width(def) || is_pass_through(def.code));
            if(keeps_type)
            {
                info.input_types[pin] = source_types[pin];
            }
        }
        // A function input gives the caller's value as the type it declares.
        if(def.type_rule == material_type_rule::declared)
        {
            info.input_types[FUNCTION_INPUT_CALLER_PIN] = info.type;
        }
        for(size_t pin = 0; pin < def.outputs.size(); ++pin)
        {
            info.output_types[pin] = get_output_type(index, def.outputs[pin]);
        }
        state.resolve_state = 2;
    }

    /// The widest value linked into the node's dynamic inputs, 1 when none is linked: what they all read.
    static auto find_dynamic_width(const material_node_def& def, const std::vector<uint32_t>& source_widths) -> uint32_t
    {
        uint32_t width = 1;
        for(size_t pin = 0; pin < def.inputs.size(); ++pin)
        {
            if(def.inputs[pin].type == material_value_type::dynamic)
            {
                width = std::max(width, source_widths[pin]);
            }
        }
        return width;
    }

    static auto find_input_type(const material_node_def& def, const material_pin_def& pin, uint32_t dynamic_width, uint32_t source_width)
        -> material_value_type
    {
        if(pin.type != material_value_type::dynamic)
        {
            return pin.type;
        }
        return get_material_value_type(keeps_own_input_width(def) ? std::max(source_width, 1u) : dynamic_width);
    }

    /// Append, Swizzle, Static Switch, Custom and calls read each input at its own width (and type).
    static auto keeps_own_input_width(const material_node_def& def) -> bool
    {
        return def.type_rule == material_type_rule::append || def.type_rule == material_type_rule::swizzle ||
               def.type_rule == material_type_rule::chosen_input || def.type_rule == material_type_rule::custom ||
               def.type_rule == material_type_rule::function_call;
    }

    /// The texture or attributes type a node passes on from what is linked into it (a reroute, a Static Switch, a
    /// function's output, a call); dynamic for every other node.
    auto find_kept_type(const node_state& state, const std::vector<material_value_type>& source_types) -> material_value_type
    {
        const auto& def = *state.def;
        const auto code = def.code;
        const bool passes_first = code == material_node_code::reroute || code == material_node_code::named_reroute_declaration ||
                                  code == material_node_code::named_reroute || code == material_node_code::function_output;
        if(passes_first && !source_types.empty())
        {
            return source_types[0];
        }
        if(def.type_rule == material_type_rule::chosen_input)
        {
            return source_types[get_chosen_input(state)];
        }
        if(def.type_rule == material_type_rule::function_call && source_types.size() > MATERIAL_FUNCTION_MAX_INPUTS)
        {
            return source_types[MATERIAL_FUNCTION_MAX_INPUTS];
        }
        return material_value_type::dynamic;
    }

    /// The type @p source reads: its output's, or as many numbers as the components it picks.
    auto get_source_type(const link_source& source) -> material_value_type
    {
        if(!source.components.empty())
        {
            return get_material_value_type(uint32_t(source.components.size()));
        }
        return get_output_type(size_t(source.node), *source.output);
    }

    auto find_node_type(const node_state& state, const std::vector<uint32_t>& source_widths, uint32_t dynamic_width)
        -> material_value_type
    {
        const auto& def = *state.def;
        switch(def.type_rule)
        {
            case material_type_rule::fixed:
                return def.value_type;
            case material_type_rule::dynamic:
                return get_material_value_type(dynamic_width);
            case material_type_rule::append:
                return find_append_type(state, source_widths);
            case material_type_rule::swizzle:
                return get_material_value_type(uint32_t(std::max<size_t>(get_swizzle_mask(state).size(), 1)));
            case material_type_rule::chosen_input:
                return get_material_value_type(std::max(source_widths[get_chosen_input(state)], 1u));
            case material_type_rule::custom:
                return get_custom_output_type(state);
            case material_type_rule::declared:
                return get_declared_type(state);
            case material_type_rule::function_call:
                return get_material_value_type(std::max(source_widths[MATERIAL_FUNCTION_MAX_INPUTS], 1u));
            default:
                break;
        }
        return material_value_type::float1;
    }

    /// The input a Static Switch passes on.
    /// The input a Static Switch passes on: the one its Value names, or for a Quality Switch the one of the quality
    /// the material compiles for.
    auto get_chosen_input(const node_state& state) const -> size_t
    {
        if(state.def != nullptr && state.def->type == QUALITY_SWITCH_TYPE)
        {
            return options_.quality == material_graph_quality::low ? 1 : 0;
        }
        return get_setting(state, SWITCH_VALUE_SETTING) == SWITCH_FALSE_VALUE ? 1 : 0;
    }

    static auto get_declared_type(const node_state& state) -> material_value_type
    {
        return get_named_type(get_setting(state, FUNCTION_INPUT_TYPE_SETTING));
    }

    static auto get_custom_output_type(const node_state& state) -> material_value_type
    {
        return get_named_type(get_setting(state, CUSTOM_OUTPUT_SETTING));
    }

    /// The type a shader type name names ("vec2", "sampler2D", "MaterialSurface"); a Vector 3 for any other name.
    static auto get_named_type(const std::string& output) -> material_value_type
    {
        for(uint32_t width = 1; width <= MAX_WIDTH; ++width)
        {
            if(output == get_material_value_shader_type(get_material_value_type(width)))
            {
                return get_material_value_type(width);
            }
        }
        for(const auto type : {material_value_type::texture2d, material_value_type::texture_cube, material_value_type::attributes})
        {
            if(output == get_material_value_shader_type(type))
            {
                return type;
            }
        }
        return material_value_type::float3;
    }

    auto find_append_type(const node_state& state, const std::vector<uint32_t>& source_widths) -> material_value_type
    {
        uint32_t width = 0;
        for(const uint32_t source_width : source_widths)
        {
            width += std::max(source_width, 1u);
        }
        if(width > MAX_WIDTH)
        {
            report(material_graph_severity::error,
                   state.node->id,
                   {},
                   fmt::format("Joins {} components; a vector holds at most 4.", width));
            return material_value_type::float4;
        }
        return get_material_value_type(width);
    }

    /// The components the node's mask setting picks; empty when it is not a valid mask.
    static auto get_swizzle_mask(const node_state& state) -> std::vector<int>
    {
        return parse_swizzle(get_setting(state, MASK_SETTING));
    }

    static auto get_setting(const node_state& state, hpp::string_view name) -> std::string
    {
        if(const auto* value = find_material_graph_setting(*state.node, name))
        {
            return *value;
        }
        const auto* setting = state.def ? find_material_setting_def(*state.def, name) : nullptr;
        return setting ? setting->default_value : std::string{};
    }

    void mark_used()
    {
        mark_discards_used();
        if(output_ >= 0)
        {
            mark_used(size_t(output_));
            return;
        }
        // A function compiled on its own: what its outputs give.
        for(size_t i = 0; i < authored_count_; ++i)
        {
            if(is_used_kind_def(i, material_node_code::function_output))
            {
                mark_used(i);
            }
        }
    }

    /// Discard nodes act with no link: each is a root of the pixel stage, as the Surface Output is.
    void mark_discards_used()
    {
        for(size_t i = 0; i < states_.size(); ++i)
        {
            if(is_used_kind_def(i, material_node_code::discard))
            {
                mark_used(i);
            }
        }
    }

    auto is_used_kind_def(size_t index, material_node_code code) const -> bool
    {
        return states_[index].def && states_[index].def->code == code;
    }

    /// The preview computes every node with a value of its own.
    void mark_all_used()
    {
        for(size_t i = 0; i < states_.size(); ++i)
        {
            if(states_[i].def && int(i) != output_)
            {
                mark_used(i);
            }
        }
    }

    void mark_used(size_t index)
    {
        auto& info = get_info(index);
        if(info.is_used)
        {
            return;
        }
        info.is_used = true;
        const auto& state = states_[index];
        const bool is_switch = state.def && state.def->code == material_node_code::static_switch;
        for(size_t pin = 0; pin < state.sources.size(); ++pin)
        {
            // A Static Switch's other branch is not in the code, nor the other workflow's channels.
            if((is_switch && pin != get_chosen_input(state)) || !is_output_pin_used(index, pin))
            {
                continue;
            }
            if(state.sources[pin].is_linked())
            {
                mark_used(size_t(state.sources[pin].node));
            }
            for(const auto& source : state.component_sources[pin])
            {
                if(source.is_linked())
                {
                    mark_used(size_t(source.node));
                }
            }
        }
    }

    /// Whether input @p pin of node @p index is read: every one but the Surface Output's of the other workflow.
    auto is_output_pin_used(size_t index, size_t pin) const -> bool
    {
        const auto& state = states_[index];
        if(int(index) != output_ || state.def == nullptr || pin >= state.def->inputs.size())
        {
            return true;
        }
        return is_material_output_pin_used(*state.node, state.def->inputs[pin].name);
    }

    auto is_used_kind(size_t index, material_node_code code) -> bool
    {
        return states_[index].def && states_[index].def->code == code && get_info(index).is_used;
    }

    /// Lays out the parameters' registers: those material instances override per instance first, vectors before
    /// scalars, so they take the registers an instanced draw reads per instance; then the others. The result lists
    /// them in the graph's order.
    void allocate_parameters()
    {
        std::vector<size_t> vectors;
        std::vector<size_t> scalars;
        std::vector<size_t> shared_vectors;
        std::vector<size_t> shared_scalars;
        std::map<std::string, material_value_type> names;
        for(size_t i = 0; i < states_.size(); ++i)
        {
            if(!is_used_kind(i, material_node_code::parameter))
            {
                continue;
            }
            std::string name = get_setting(states_[i], PARAMETER_NAME_SETTING);
            name = name.empty() ? states_[i].node->id : name;
            parameter_names_[i] = name;
            // Parameters of one name and type are one value (each call of a function holding one shares it).
            const auto named = names.emplace(name, get_info(i).type);
            if(!named.second)
            {
                if(named.first->second != get_info(i).type)
                {
                    report(material_graph_severity::error,
                           states_[i].node->id,
                           PARAMETER_NAME_SETTING,
                           fmt::format("Another parameter is named \"{}\" with another type; material instances could not tell them apart.", name));
                }
                continue;
            }
            const bool is_per_instance =
                get_setting(states_[i], MATERIAL_PARAMETER_PER_INSTANCE_SETTING) != MATERIAL_PARAMETER_PER_INSTANCE_OFF;
            const bool is_scalar = get_material_value_width(get_info(i).type) == 1;
            auto& group = is_per_instance ? (is_scalar ? scalars : vectors) : (is_scalar ? shared_scalars : shared_vectors);
            group.push_back(i);
        }
        std::vector<size_t> allocated;
        allocate_registers(vectors, scalars, allocated);
        allocate_registers(shared_vectors, shared_scalars, allocated);
        result_.parameter_registers = std::min(result_.parameter_registers, MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS);
        sort_by_node(result_.parameters, allocated);
        merge_parameter_aspects();
    }

    /// Gives each parameter the aspects of every node of its name: one name may sit in several calls of a function.
    void merge_parameter_aspects()
    {
        for(auto& parameter : result_.parameters)
        {
            parameter.changes = render_change::none;
        }
        for(size_t i = 0; i < states_.size(); ++i)
        {
            const auto name = parameter_names_.find(i);
            if(name == parameter_names_.end() || !is_used_kind(i, material_node_code::parameter))
            {
                continue;
            }
            if(auto* parameter = find_named(result_.parameters, name->second))
            {
                parameter->changes |= node_aspects_[i];
            }
        }
    }

    /// The render_change aspects a Surface Output channel feeds.
    static auto get_channel_aspects(hpp::string_view pin, bool is_specular_workflow) -> render_change::mask
    {
        if(pin == BASE_COLOR_PIN)
        {
            // The diffuse color of the specular workflow takes part in its metallic.
            return is_specular_workflow ? render_change::albedo | render_change::specular : render_change::albedo;
        }
        if(pin == MATERIAL_METALLIC_PIN || pin == MATERIAL_SPECULAR_PIN || pin == MATERIAL_GLOSSINESS_PIN)
        {
            return render_change::albedo | render_change::specular;
        }
        if(pin == MATERIAL_ROUGHNESS_PIN)
        {
            return render_change::specular;
        }
        if(pin == NORMAL_PIN)
        {
            return render_change::normal;
        }
        if(pin == EMISSIVE_PIN)
        {
            return render_change::emission;
        }
        if(pin == AMBIENT_OCCLUSION_PIN)
        {
            return render_change::occlusion;
        }
        if(pin == SUBSURFACE_COLOR_PIN || pin == SUBSURFACE_OPACITY_PIN)
        {
            return render_change::albedo;
        }
        if(pin == OPACITY_MASK_PIN || pin == MATERIAL_OPACITY_MASK_CLIP_PIN || pin == CAMERA_FADE_DISTANCE_PIN)
        {
            return render_change::coverage;
        }
        // World Position Offset moves the whole surface.
        return render_change::material_aspects;
    }

    /// The aspects each node feeds: those of the Surface Output channels it reaches, and coverage for what a Discard
    /// node reads. A preview feeds no draw: every node takes every aspect.
    void collect_node_aspects()
    {
        node_aspects_.assign(states_.size(), options_.is_preview ? render_change::material_aspects : render_change::none);
        if(options_.is_preview)
        {
            return;
        }
        for(size_t i = 0; i < states_.size(); ++i)
        {
            if(is_used_kind_def(i, material_node_code::discard) && get_info(i).is_used)
            {
                spread_aspects(i, render_change::coverage);
            }
        }
        if(output_ < 0)
        {
            return;
        }
        const size_t index = size_t(output_);
        const auto& state = states_[index];
        const bool is_specular_workflow = is_material_specular_workflow(*state.node);
        for(size_t pin = 0; pin < state.sources.size() && pin < state.def->inputs.size(); ++pin)
        {
            if(is_output_pin_used(index, pin))
            {
                spread_input_aspects(index, pin, get_channel_aspects(state.def->inputs[pin].name, is_specular_workflow));
            }
        }
    }

    /// Gives what input @p pin of node @p index reads @p aspects.
    void spread_input_aspects(size_t index, size_t pin, render_change::mask aspects)
    {
        const auto& state = states_[index];
        if(state.sources[pin].is_linked())
        {
            spread_aspects(size_t(state.sources[pin].node), aspects);
        }
        for(const auto& source : state.component_sources[pin])
        {
            if(source.is_linked())
            {
                spread_aspects(size_t(source.node), aspects);
            }
        }
    }

    /// Gives node @p index, and every node it reads, @p aspects.
    void spread_aspects(size_t index, render_change::mask aspects)
    {
        if((node_aspects_[index] & aspects) == aspects)
        {
            return;
        }
        node_aspects_[index] |= aspects;
        const auto& state = states_[index];
        const bool is_switch = state.def && state.def->code == material_node_code::static_switch;
        for(size_t pin = 0; pin < state.sources.size(); ++pin)
        {
            if(!is_switch || pin == get_chosen_input(state))
            {
                spread_input_aspects(index, pin, aspects);
            }
        }
    }

    /// Vectors take a register each, scalars share the registers after them four to one. @p allocated gets the node of
    /// each parameter added.
    void allocate_registers(const std::vector<size_t>& vectors, const std::vector<size_t>& scalars, std::vector<size_t>& allocated)
    {
        for(const size_t index : vectors)
        {
            if(add_parameter(index, result_.parameter_registers++, 0))
            {
                allocated.push_back(index);
            }
        }
        for(size_t i = 0; i < scalars.size(); ++i)
        {
            if(add_parameter(scalars[i], result_.parameter_registers + uint32_t(i / MAX_WIDTH), uint32_t(i % MAX_WIDTH)))
            {
                allocated.push_back(scalars[i]);
            }
        }
        result_.parameter_registers += uint32_t((scalars.size() + MAX_WIDTH - 1) / MAX_WIDTH);
    }

    /// Puts @p entries, each made from the node at the same place of @p nodes, in the graph's order.
    template<typename Entry>
    static void sort_by_node(std::vector<Entry>& entries, const std::vector<size_t>& nodes)
    {
        std::vector<size_t> order(entries.size());
        std::iota(order.begin(), order.end(), size_t{0});
        std::stable_sort(order.begin(),
                         order.end(),
                         [&nodes](size_t a, size_t b)
                         {
                             return nodes[a] < nodes[b];
                         });
        std::vector<Entry> sorted;
        sorted.reserve(entries.size());
        for(const size_t index : order)
        {
            sorted.push_back(std::move(entries[index]));
        }
        entries = std::move(sorted);
    }

    /// Adds the parameter of node @p index at @p register_index; false, reported, past the last register.
    auto add_parameter(size_t index, uint32_t register_index, uint32_t component) -> bool
    {
        const auto& state = states_[index];
        if(register_index >= MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS)
        {
            report(material_graph_severity::error,
                   state.node->id,
                   {},
                   fmt::format("A material's parameters take at most {} vec4 registers; this one is its default value.",
                               MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS));
            return false;
        }
        material_graph_parameter parameter;
        parameter.node_id = state.node->id;
        parameter.name = parameter_names_[index];
        parameter.description = get_setting(state, MATERIAL_PARAMETER_DESCRIPTION_SETTING);
        parameter.type = get_info(index).type;
        parameter.default_value = get_value(state, 0);
        parameter.register_index = register_index;
        parameter.component = component;
        result_.parameters.push_back(parameter);
        return true;
    }

    /// Whether node @p index holds a texture of its own the shader reads: a used texture node with nothing linked into
    /// its Texture input, a Texture or Cubemap Object, or a texture function input the caller links nothing into.
    auto holds_read_texture(size_t index) -> bool
    {
        const auto& state = states_[index];
        if(state.def == nullptr || !get_info(index).is_used)
        {
            return false;
        }
        if(state.def->code == material_node_code::function_input)
        {
            return is_material_texture_type(get_info(index).type) && !state.sources[FUNCTION_INPUT_CALLER_PIN].is_linked();
        }
        return is_material_texture_node(*state.def) && find_texture_provider(index) == -1;
    }

    /// The kind of texture node @p index holds.
    auto get_held_texture_kind(size_t index) -> material_value_type
    {
        const auto& def = *states_[index].def;
        return def.code == material_node_code::function_input ? get_info(index).type : get_material_texture_node_kind(def);
    }

    /// Marks the textures Normal Map nodes read through their Texture input as normal maps: the draw binds the flat
    /// normal for an empty one and tells the shader its channel layout.
    void mark_linked_normal_maps()
    {
        for(size_t i = 0; i < states_.size(); ++i)
        {
            if(!is_used_kind(i, material_node_code::normal_map))
            {
                continue;
            }
            const int provider = find_texture_provider(i);
            const auto it = provider >= 0 ? texture_indices_.find(size_t(provider)) : texture_indices_.end();
            if(it != texture_indices_.end())
            {
                result_.textures[it->second].is_normal_map = true;
            }
        }
    }

    void allocate_textures()
    {
        for(size_t i = 0; i < states_.size(); ++i)
        {
            if(!holds_read_texture(i))
            {
                continue;
            }
            const bool is_normal = is_used_kind(i, material_node_code::normal_map);
            const auto& node = *states_[i].node;
            if(result_.textures.size() >= get_texture_limit())
            {
                report(material_graph_severity::error,
                       node.id,
                       {},
                       fmt::format("A {} material reads at most {} textures; this one reads its default value.",
                                   get_texture_limit_label(),
                                   get_texture_limit()));
                continue;
            }
            const auto texture_index = uint32_t(result_.textures.size());
            material_graph_texture texture;
            texture.node_id = node.id;
            const std::string name = get_setting(states_[i], PARAMETER_NAME_SETTING);
            texture.name = name.empty() ? node.id : name;
            texture.description = get_setting(states_[i], MATERIAL_PARAMETER_DESCRIPTION_SETTING);
            texture.changes = node_aspects_[i];
            texture.texture = node.texture;
            texture.sampler = get_material_graph_sampler_name(texture_index);
            texture.stage = get_material_graph_texture_stage(texture_index);
            texture.is_normal_map = is_normal;
            texture.kind = get_held_texture_kind(i);
            texture.address = get_texture_address(states_[i]);
            texture.filter = get_texture_filter(states_[i]);
            if(!texture.texture)
            {
                // As a PBR material's empty map: the default texture the draw binds.
                report(material_graph_severity::info,
                       node.id,
                       TEXTURE_PIN,
                       is_normal ? "No texture: reads a flat normal." : "No texture: reads white.");
            }
            texture_indices_[i] = result_.textures.size();
            result_.textures.push_back(texture);
        }
        mark_linked_normal_maps();
    }

    static auto get_texture_address(const node_state& state) -> material_texture_address
    {
        const std::string value = get_setting(state, MATERIAL_TEXTURE_ADDRESS_SETTING);
        if(value == "wrap")
        {
            return material_texture_address::wrap;
        }
        if(value == "clamp")
        {
            return material_texture_address::clamp;
        }
        return value == "mirror" ? material_texture_address::mirror : material_texture_address::texture;
    }

    static auto get_texture_filter(const node_state& state) -> material_texture_filter
    {
        const std::string value = get_setting(state, MATERIAL_TEXTURE_FILTER_SETTING);
        if(value == "trilinear")
        {
            return material_texture_filter::trilinear;
        }
        return value == "point" ? material_texture_filter::point : material_texture_filter::anisotropic;
    }

    /// The named Static Switches of the graph's own nodes in the shader: what the material's inspector shows.
    void collect_static_switches()
    {
        for(size_t i = 0; i < authored_count_ && i < states_.size(); ++i)
        {
            if(!is_used_kind(i, material_node_code::static_switch))
            {
                continue;
            }
            std::string name = get_setting(states_[i], PARAMETER_NAME_SETTING);
            if(name.empty())
            {
                continue;
            }
            material_graph_static_switch entry;
            entry.node_id = states_[i].node->id;
            entry.name = std::move(name);
            entry.description = get_setting(states_[i], MATERIAL_PARAMETER_DESCRIPTION_SETTING);
            entry.value = get_chosen_input(states_[i]) == 0;
            result_.static_switches.push_back(std::move(entry));
        }
    }

    /// The authored value of input @p pin, or its default.
    auto get_value(const node_state& state, size_t pin) const -> math::vec4
    {
        const auto* authored = state.authored.empty() ? nullptr : state.authored[pin];
        return authored ? authored->value : state.def->inputs[pin].default_value;
    }

    // -----------------------------------------------------------------------------------------
    // Code
    // -----------------------------------------------------------------------------------------

    /// The code of an output of an emitted node.
    auto get_output_code(size_t index, const material_pin_def& output) -> std::string
    {
        const auto& state = states_[index];
        if(const int result = get_call_result_pin(index, output); result >= 0)
        {
            return state.output_codes[size_t(result) - MATERIAL_FUNCTION_MAX_INPUTS];
        }
        if(!output.member.empty())
        {
            return fmt::format("{}.{}", make_postfix_safe(state.value_code), output.member);
        }
        const uint32_t width = get_material_value_width(get_info(index).type);
        return apply_components(state.value_code, width, parse_swizzle(output.swizzle));
    }

    /// @p code of @p from_type made a value of @p to_type.
    auto convert(const std::string& code, uint32_t from, uint32_t to, const node_state& reader, const std::string& pin) -> std::string
    {
        if(from == to)
        {
            return code;
        }
        if(from == 1)
        {
            return fmt::format("{}({})", get_splat(to), code);
        }
        if(to == 1)
        {
            report(material_graph_severity::error,
                   reader.node->id,
                   pin,
                   fmt::format("A {} cannot go into a Float: link one of its components (an R, G, B or A output, a split pin or a "
                               "Swizzle).",
                               get_material_value_type_name(get_material_value_type(from))));
            return apply_components(code, from, make_first_components(1));
        }
        if(from > to)
        {
            return apply_components(code, from, make_first_components(to));
        }
        report(material_graph_severity::warning,
               reader.node->id,
               pin,
               fmt::format("A {} is linked into a {}: the missing components read 0{}",
                           get_material_value_type_name(get_material_value_type(from)),
                           get_material_value_type_name(get_material_value_type(to)),
                           to == MAX_WIDTH ? " (alpha 1)." : "."));
        const std::string type = get_material_value_shader_type(get_material_value_type(to));
        if(to == MAX_WIDTH)
        {
            return fmt::format("{}({}, {})", type, code, from == 2 ? "0.0, 1.0" : "1.0");
        }
        return fmt::format("{}({}, 0.0)", type, code);
    }

    /// The code of what @p source reads, emitting its node first.
    auto read_source(const link_source& source) -> std::string
    {
        emit_node(size_t(source.node));
        const auto& output = *source.output;
        const std::string code = get_output_code(size_t(source.node), output);
        if(source.components.empty())
        {
            return code;
        }
        return apply_components(code, get_material_value_width(get_output_type(size_t(source.node), output)), source.components);
    }

    /// The code of a vector input built from its components: each linked one reads its link, the others the value.
    auto make_composed_code(size_t index, size_t pin) -> std::string
    {
        const auto& state = states_[index];
        const auto& pin_def = state.def->inputs[pin];
        const uint32_t width = get_material_value_width(pin_def.type);
        const math::vec4 value = get_value(state, pin);
        std::vector<std::string> parts;
        for(uint32_t component = 0; component < width; ++component)
        {
            const auto& source = state.component_sources[pin][component];
            std::string part = format_float_literal(value[int(component)]);
            if(source.is_linked())
            {
                const std::string entry =
                    make_material_graph_component_pin(pin_def.name, int(component), get_material_component_letters(pin_def));
                part = convert(read_source(source), get_source_width(source), 1, state, entry);
            }
            parts.push_back(std::move(part));
        }
        return fmt::format("{}({})", get_material_value_shader_type(pin_def.type), fmt::join(parts, ", "));
    }

    /// The code input @p pin of the node reads, as a value of its resolved type. Emits what it is linked to first.
    auto get_input_code(size_t index, size_t pin) -> std::string
    {
        auto& state = states_[index];
        const auto& pin_def = state.def->inputs[pin];
        const uint32_t width = get_material_value_width(get_info(index).input_types[pin]);
        if(state.is_composed[pin])
        {
            return make_composed_code(index, pin);
        }
        const auto& source = state.sources[pin];
        const auto input_type = get_info(index).input_types[pin];
        if(source.is_linked())
        {
            const auto source_type = get_source_type(source);
            if(is_material_value_numeric(source_type) && is_material_value_numeric(input_type))
            {
                return convert(read_source(source), get_source_width(source), width, state, pin_def.name);
            }
            if(source_type == input_type)
            {
                return read_source(source);
            }
            report(material_graph_severity::error,
                   state.node->id,
                   pin_def.name,
                   fmt::format("A {} cannot go into a {}: the link is ignored.",
                               get_material_value_type_name(source_type),
                               get_material_value_type_name(input_type)));
            return get_unlinked_code(input_type);
        }
        if(!is_material_value_numeric(input_type))
        {
            return get_unlinked_code(input_type);
        }
        if(!pin_def.default_code.empty())
        {
            return pin_def.default_code;
        }
        const math::vec4 value = get_value(state, pin);
        const bool is_dynamic = pin_def.type == material_value_type::dynamic;
        if(is_dynamic && is_vector_value_given(value))
        {
            report(material_graph_severity::warning,
                   state.node->id,
                   pin_def.name,
                   fmt::format("{} takes one number for every component: {} is read as {}. Link a Vector 2, Vector 3 or "
                               "Color node to give each component its own value.",
                               pin_def.label.empty() ? pin_def.name : pin_def.label,
                               format_vector_value(value),
                               format_float_literal(value.x)));
        }
        return format_literal(value, width, is_dynamic);
    }

    /// What an unlinked input of a type that is no number reads: the default surface for attributes, nothing for a
    /// texture (a texture node reads its own then, others report it).
    static auto get_unlinked_code(material_value_type type) -> std::string
    {
        return type == material_value_type::attributes ? std::string("mg_default_surface()") : std::string{};
    }

    /// @p expression with each {pin} replaced by the input's code and each {setting} by its checked value.
    auto expand_expression(size_t index, const std::string& expression) -> std::string
    {
        std::string code;
        size_t position = 0;
        while(position < expression.size())
        {
            const size_t open = expression.find('{', position);
            const size_t close = open == std::string::npos ? std::string::npos : expression.find('}', open);
            if(close == std::string::npos)
            {
                code += expression.substr(position);
                break;
            }
            code += expression.substr(position, open - position);
            code += expand_token(index, expression.substr(open + 1, close - open - 1));
            position = close + 1;
        }
        return code;
    }

    auto expand_token(size_t index, const std::string& name) -> std::string
    {
        const auto& def = *states_[index].def;
        for(size_t pin = 0; pin < def.inputs.size(); ++pin)
        {
            if(def.inputs[pin].name == name)
            {
                return get_input_code(index, pin);
            }
        }
        if(const auto* setting = find_material_setting_def(def, name))
        {
            return get_checked_setting(index, *setting);
        }
        report(material_graph_severity::error,
               states_[index].node->id,
               {},
               fmt::format("The node type's code names \"{}\", which it does not have.", name));
        return "0.0";
    }

    /// A setting's value checked for its kind, so that it is safe to put into code; its default when it is not.
    auto get_checked_setting(size_t index, const material_setting_def& setting) -> std::string
    {
        const auto& state = states_[index];
        const std::string value = get_setting(state, setting.name);
        switch(setting.kind)
        {
            case material_setting_kind::choice:
                if(std::find(setting.choices.begin(), setting.choices.end(), value) != setting.choices.end())
                {
                    return value;
                }
                report(material_graph_severity::error,
                       state.node->id,
                       setting.name,
                       fmt::format("{} \"{}\" is not one of its choices.", setting.label, value));
                return setting.default_value;
            case material_setting_kind::number:
                return format_scalar(parse_number(index, setting, value));
            case material_setting_kind::swizzle:
                if(!parse_swizzle(value).empty())
                {
                    return value;
                }
                report(material_graph_severity::error,
                       state.node->id,
                       setting.name,
                       fmt::format("{} \"{}\" is not a swizzle mask.", setting.label, value));
                return setting.default_value;
            case material_setting_kind::text:
            case material_setting_kind::code:
                break;
        }
        report(material_graph_severity::error, state.node->id, setting.name, fmt::format("{} cannot appear in code.", setting.label));
        return "0.0";
    }

    auto parse_number(size_t index, const material_setting_def& setting, const std::string& value) -> float
    {
        char* end = nullptr;
        const float number = std::strtof(value.c_str(), &end);
        if(!value.empty() && end == value.c_str() + value.size() && std::isfinite(number))
        {
            return number;
        }
        report(material_graph_severity::error,
               states_[index].node->id,
               setting.name,
               fmt::format("{} \"{}\" is not a number.", setting.label, value));
        return std::strtof(setting.default_value.c_str(), nullptr);
    }

    /// Emits the node and everything it reads, once.
    void emit_node(size_t index)
    {
        auto& state = states_[index];
        if(state.is_emitted || !state.def)
        {
            return;
        }
        state.is_emitted = true;
        const std::string code = make_node_code(index);
        // A texture is never a variable: GLSL has no sampler variables, so its name is written where it is read.
        if(state.def->is_inline || is_pass_through(state.def->code) || is_material_texture_type(get_info(index).type))
        {
            state.value_code = code;
            return;
        }
        state.value_code = state.variable;
        const size_t first_line = body_.size();
        body_.push_back(fmt::format("{}// {}: {}", INDENT, make_comment_text(state.node->id), state.def->label));
        body_.push_back(fmt::format("{}{} {} = {};", INDENT, get_material_value_shader_type(get_info(index).type), state.variable, code));
        node_lines_.push_back({index, first_line, body_.size() - first_line});
    }

    auto make_node_code(size_t index) -> std::string
    {
        const auto& def = *states_[index].def;
        // Reported by check_domain_nodes: what it would read does not exist in this domain's shader.
        if((!is_in_domain(index) || is_refused_scene_read(index)) && def.code != material_node_code::surface_output)
        {
            const auto type = get_info(index).type;
            return is_material_value_numeric(type) ? format_literal(math::vec4(0.0f), get_material_value_width(type), true) : "0.0";
        }
        switch(def.code)
        {
            case material_node_code::expression:
                return expand_expression(index, get_expression(index));
            case material_node_code::parameter:
                return make_parameter_code(index);
            case material_node_code::texture_sample:
                return make_texture_sample_code(index);
            case material_node_code::normal_map:
                return make_normal_map_code(index);
            case material_node_code::append:
                return make_append_code(index);
            case material_node_code::swizzle:
                return make_swizzle_code(index);
            case material_node_code::reroute:
            case material_node_code::named_reroute_declaration:
            case material_node_code::named_reroute:
                return get_input_code(index, 0);
            case material_node_code::static_switch:
                return get_input_code(index, get_chosen_input(states_[index]));
            case material_node_code::custom:
                return make_custom_code(index);
            case material_node_code::triplanar_sample:
                return make_triplanar_code(index);
            case material_node_code::function_input:
                return make_function_input_code(index);
            case material_node_code::function_output:
                return get_input_code(index, 0);
            case material_node_code::function_call:
                return make_function_call_code(index);
            case material_node_code::color_ramp:
                return make_color_ramp_code(index);
            case material_node_code::vertex_interpolator:
                return make_interpolator_code(index);
            case material_node_code::texture_object:
                return get_own_sampler(index);
            case material_node_code::parallax_occlusion:
                return make_parallax_code(index);
            case material_node_code::normal_from_texture:
                return make_normal_from_texture_code(index);
            case material_node_code::surface_output:
            case material_node_code::discard:
                break;
        }
        return "0.0";
    }

    /// A node whose value is one of its inputs as it is: written where it is read, never into a variable.
    static auto is_pass_through(material_node_code code) -> bool
    {
        return code == material_node_code::reroute || code == material_node_code::named_reroute_declaration ||
               code == material_node_code::named_reroute || code == material_node_code::static_switch ||
               code == material_node_code::function_input || code == material_node_code::function_output ||
               code == material_node_code::function_call || code == material_node_code::vertex_interpolator;
    }

    /// A Vertex Interpolator: in the vertex stage (and a preview, which has none) its input itself; in the pixel stage
    /// the varying the vertex shader writes it into, past the last one a graph has, its input computed per pixel.
    auto make_interpolator_code(size_t index) -> std::string
    {
        if(options_.is_preview || is_emitting_vertex_)
        {
            return get_input_code(index, 0);
        }
        const auto found = interpolator_slots_.find(index);
        uint32_t slot = 0;
        if(found != interpolator_slots_.end())
        {
            slot = found->second;
        }
        else
        {
            if(interpolator_slots_.size() >= MAX_INTERPOLATORS)
            {
                report(material_graph_severity::error,
                       states_[index].node->id,
                       {},
                       fmt::format("A graph reads at most {} Vertex Interpolators: this one is computed per pixel.", MAX_INTERPOLATORS));
                return get_input_code(index, 0);
            }
            slot = uint32_t(interpolator_slots_.size());
            interpolator_slots_[index] = slot;
        }
        states_[index].reads_changing_input = reads_changing_code(index, 0);
        const uint32_t width = get_material_value_width(get_info(index).type);
        const std::string varying = fmt::format("i.interpolator{}", slot);
        return width == MAX_WIDTH ? varying : apply_components(varying, MAX_WIDTH, make_first_components(width));
    }

    /// Whether the code of what input @p pin of node @p index reads names the time, the camera or the screen, by the
    /// node types' code: for a value the pixel stage reads from the vertex stage, whose lines it does not have.
    auto reads_changing_code(size_t index, size_t pin) const -> bool
    {
        const auto names_changing = [](const std::string& code)
        {
            return std::any_of(CHANGING_INPUT_CODES.begin(),
                               CHANGING_INPUT_CODES.end(),
                               [&code](const char* changing)
                               {
                                   return code.find(changing) != std::string::npos;
                               });
        };
        std::set<size_t> nodes;
        collect_input_nodes(index, pin, nodes);
        return std::any_of(nodes.begin(),
                           nodes.end(),
                           [&](size_t node)
                           {
                               const auto& state = states_[node];
                               if(state.def == nullptr)
                               {
                                   return false;
                               }
                               const bool reads_default = std::any_of(state.def->inputs.begin(),
                                                                      state.def->inputs.end(),
                                                                      [&](const material_pin_def& input)
                                                                      {
                                                                          return names_changing(input.default_code);
                                                                      });
                               const bool reads_custom = state.def->code == material_node_code::custom &&
                                                         names_changing(get_setting(state, CUSTOM_CODE_SETTING));
                               return names_changing(state.def->expression) || reads_default || reads_custom;
                           });
    }

    /// What the caller links into the input, as its declared type; its default while nothing is.
    auto make_function_input_code(size_t index) -> std::string
    {
        const auto& state = states_[index];
        if(state.sources[FUNCTION_INPUT_CALLER_PIN].is_linked())
        {
            return get_input_code(index, FUNCTION_INPUT_CALLER_PIN);
        }
        // An unlinked texture input reads its own texture, an attributes input the default surface.
        if(is_material_texture_type(get_info(index).type))
        {
            return get_own_sampler(index);
        }
        if(get_info(index).type == material_value_type::attributes)
        {
            return get_unlinked_code(material_value_type::attributes);
        }
        return format_literal(get_value(state, FUNCTION_INPUT_DEFAULT_PIN), get_material_value_width(get_info(index).type), false);
    }

    /// A call: the code of each of its results, each one its output's; its value is the first.
    auto make_function_call_code(size_t index) -> std::string
    {
        auto& state = states_[index];
        state.output_codes.clear();
        for(uint32_t k = 0; k < MATERIAL_FUNCTION_MAX_OUTPUTS; ++k)
        {
            const size_t pin = MATERIAL_FUNCTION_MAX_INPUTS + k;
            state.output_codes.push_back(state.sources[pin].is_linked() ? get_input_code(index, pin) : std::string("0.0"));
        }
        return state.output_codes.front();
    }

    /// A Custom node: its code as a function of its inputs, emitted before the graph's, and the call of it.
    auto make_custom_code(size_t index) -> std::string
    {
        auto& state = states_[index];
        const auto& info = get_info(index);
        const std::string function = CUSTOM_FUNCTION_PREFIX + state.variable;
        std::vector<std::string> parameters{"MaterialInputs i"};
        std::vector<std::string> arguments{"i"};
        for(size_t pin = 0; pin < state.def->inputs.size(); ++pin)
        {
            parameters.push_back(fmt::format("{} {}", get_material_value_shader_type(info.input_types[pin]), state.def->inputs[pin].name));
            arguments.push_back(get_input_code(index, pin));
        }
        const std::string code = get_setting(state, CUSTOM_CODE_SETTING);
        state.reads_changing_input = std::any_of(CHANGING_INPUT_CODES.begin(),
                                                 CHANGING_INPUT_CODES.end(),
                                                 [&code](const char* changing)
                                                 {
                                                     return code.find(changing) != std::string::npos;
                                                 });
        functions_.push_back(fmt::format("// {}: Custom", make_comment_text(state.node->id)));
        functions_.push_back(fmt::format("{} {}({})", get_material_value_shader_type(info.type), function, fmt::join(parameters, ", ")));
        functions_.emplace_back("{");
        size_t position = 0;
        while(position <= code.size())
        {
            const size_t end = std::min(code.find('\n', position), code.size());
            std::string line = code.substr(position, end - position);
            if(!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            functions_.push_back(fmt::format("{}{}", INDENT, line));
            position = end + 1;
        }
        functions_.emplace_back("}");
        functions_.emplace_back();
        return fmt::format("{}({})", function, fmt::join(arguments, ", "));
    }

    auto make_triplanar_code(size_t index) -> std::string
    {
        const std::string position = get_input_code(index, 0);
        const std::string normal = get_input_code(index, 1);
        const std::string scale = get_input_code(index, 2);
        const std::string sharpness = get_input_code(index, 3);
        const std::string sampler = get_texture_sampler(index);
        if(sampler.empty())
        {
            return format_literal(math::vec4(1.0f), MAX_WIDTH, true);
        }
        return fmt::format("mg_triplanar({}, {} * {}, {}, {}, i.mip_bias)", sampler, position, scale, normal, sharpness);
    }

    /// The sampler texture node @p index reads: what is linked into its Texture input, else its own. Empty when it has
    /// neither (no sampler left, or the link reads no texture): it reads its default value.
    auto get_texture_sampler(size_t index) -> std::string
    {
        const auto* pin = find_material_pin_def(states_[index].def->inputs, MATERIAL_TEXTURE_PIN);
        if(pin != nullptr)
        {
            const auto pin_index = size_t(pin - states_[index].def->inputs.data());
            if(states_[index].sources[pin_index].is_linked())
            {
                return get_input_code(index, pin_index);
            }
        }
        return get_own_sampler(index);
    }

    /// The sampler of the texture node @p index holds itself; empty when it got none.
    auto get_own_sampler(size_t index) const -> std::string
    {
        const auto it = texture_indices_.find(index);
        return it == texture_indices_.end() ? std::string{} : result_.textures[it->second].sampler;
    }

    /// The index among the textures of what texture node @p index reads (its own, or the provider linked into its Texture
    /// input); -1 when there is none.
    auto find_read_texture(size_t index) const -> int
    {
        const int provider = find_texture_provider(index);
        const auto it = texture_indices_.find(size_t(provider >= 0 ? provider : int(index)));
        return provider == -2 || it == texture_indices_.end() ? -1 : int(it->second);
    }

    /// The node holding the texture node @p index reads through its Texture input, following reroutes, Static Switches,
    /// function inputs and calls; -1 while the input is unlinked (it reads its own), -2 when the link reaches no texture.
    auto find_texture_provider(size_t index) const -> int
    {
        const auto* pin = find_material_pin_def(states_[index].def->inputs, MATERIAL_TEXTURE_PIN);
        if(pin == nullptr)
        {
            return -1;
        }
        const auto& source = states_[index].sources[size_t(pin - states_[index].def->inputs.data())];
        return source.is_linked() ? trace_texture(source, 0) : -1;
    }

    auto trace_texture(const link_source& source, int depth) const -> int
    {
        if(!source.is_linked() || depth > MAX_ESTIMATE_DEPTH)
        {
            return -2;
        }
        const auto node = size_t(source.node);
        const auto& state = states_[node];
        if(state.def == nullptr)
        {
            return -2;
        }
        switch(state.def->code)
        {
            case material_node_code::texture_object:
                return int(node);
            case material_node_code::function_input:
                return state.sources[FUNCTION_INPUT_CALLER_PIN].is_linked() ? trace_texture(state.sources[FUNCTION_INPUT_CALLER_PIN], depth + 1)
                                                                             : int(node);
            case material_node_code::reroute:
            case material_node_code::named_reroute_declaration:
            case material_node_code::named_reroute:
            case material_node_code::function_output:
                return trace_texture(state.sources[0], depth + 1);
            case material_node_code::static_switch:
                return trace_texture(state.sources[get_chosen_input(state)], depth + 1);
            case material_node_code::function_call:
            {
                const int result = get_call_result_pin(node, *source.output);
                return result < 0 ? -2 : trace_texture(state.sources[size_t(result)], depth + 1);
            }
            default:
                break;
        }
        return -2;
    }

    /// The Mip setting's read of @p sampler at @p coordinates: auto, biased or at a level by the Mip input. @p is_cube
    /// reads a cube texture along a direction.
    auto make_sample_read(size_t index, const std::string& sampler, const std::string& coordinates, bool is_cube) -> std::string
    {
        const auto& state = states_[index];
        const auto* mode_setting = find_material_setting_def(*state.def, MATERIAL_TEXTURE_MIP_MODE_SETTING);
        const std::string mode = mode_setting != nullptr ? get_checked_setting(index, *mode_setting) : std::string("auto");
        const char* read = is_cube ? "mg_textureCube" : "mg_texture2D";
        if(mode == "auto")
        {
            return fmt::format("{}({}, {}, i.mip_bias)", read, sampler, coordinates);
        }
        const auto mip_pin = find_input_pin(*state.def, "mip");
        const std::string mip = get_input_code(index, mip_pin);
        if(mode == "bias")
        {
            return fmt::format("{}({}, {}, i.mip_bias + {})", read, sampler, coordinates, mip);
        }
        return fmt::format("{}_level({}, {}, {})", read, sampler, coordinates, mip);
    }

    /// Parallax Occlusion: the UV its height map shows along the view.
    auto make_parallax_code(size_t index) -> std::string
    {
        const auto& def = *states_[index].def;
        const std::string uv = get_input_code(index, 0);
        const std::string scale = get_input_code(index, find_input_pin(def, "height_scale"));
        const std::string min_steps = get_input_code(index, find_input_pin(def, "min_steps"));
        const std::string max_steps = get_input_code(index, find_input_pin(def, "max_steps"));
        const std::string reference = get_input_code(index, find_input_pin(def, "reference_plane"));
        const std::string sampler = get_texture_sampler(index);
        if(sampler.empty())
        {
            return uv;
        }
        return fmt::format("mg_parallax_occlusion({}, {}, mg_tangent_view_direction(i.view_direction, i.tangent, i.bitangent, i.vertex_normal), "
                           "{}, {}, {}, {}, {})",
                           sampler,
                           uv,
                           scale,
                           min_steps,
                           max_steps,
                           reference,
                           get_channel_code(index));
    }

    /// Normal From Texture: the normal of its height map's slopes.
    auto make_normal_from_texture_code(size_t index) -> std::string
    {
        const auto& def = *states_[index].def;
        const std::string uv = get_input_code(index, 0);
        const std::string offset = get_input_code(index, find_input_pin(def, "offset"));
        const std::string strength = get_input_code(index, find_input_pin(def, "strength"));
        const std::string sampler = get_texture_sampler(index);
        if(sampler.empty())
        {
            return "vec3(0.0, 0.0, 1.0)";
        }
        return fmt::format("mg_normal_from_texture({}, {}, {}, {}, {}, i.mip_bias)", sampler, uv, offset, strength, get_channel_code(index));
    }

    /// The mask of the channel node @p index reads its height from (mg_channel_r..a).
    auto get_channel_code(size_t index) -> std::string
    {
        const auto* setting = find_material_setting_def(*states_[index].def, "channel");
        return fmt::format("mg_channel_{}", setting != nullptr ? get_checked_setting(index, *setting) : std::string("r"));
    }

    /// The expression of an expression node: the one for the width of its first dynamic input where the node has one,
    /// its float form while that input reads a float, else its expression.
    auto get_expression(size_t index) -> const std::string&
    {
        const auto& def = *states_[index].def;
        if(!def.width_expressions.empty())
        {
            const size_t width = get_material_value_width(get_first_dynamic_input_type(index));
            if(width <= def.width_expressions.size() && !def.width_expressions[width - 1].empty())
            {
                return def.width_expressions[width - 1];
            }
        }
        return is_scalar_form(index) ? def.scalar_expression : def.expression;
    }

    /// The type input @p index's first dynamic input reads; a float for a node without one.
    auto get_first_dynamic_input_type(size_t index) -> material_value_type
    {
        const auto& def = *states_[index].def;
        for(size_t pin = 0; pin < def.inputs.size(); ++pin)
        {
            if(def.inputs[pin].type == material_value_type::dynamic)
            {
                return get_info(index).input_types[pin];
            }
        }
        return material_value_type::float1;
    }

    /// A Color Ramp: the first stop's colour, blended stop by stop toward each next one as Value passes between their
    /// positions. The stops are sorted by position here, so the editor may list them in any order.
    auto make_color_ramp_code(size_t index) -> std::string
    {
        const auto& state = states_[index];
        const std::string value = get_input_code(index, 0);
        const auto& count_setting = *find_material_setting_def(*state.def, MATERIAL_COLOR_RAMP_COUNT_SETTING);
        const float count_value = parse_number(index, count_setting, get_setting(state, count_setting.name));
        const auto count = uint32_t(std::clamp(count_value, 2.0f, float(MATERIAL_COLOR_RAMP_MAX_STOPS)));
        const std::string interpolation =
            get_checked_setting(index, *find_material_setting_def(*state.def, MATERIAL_COLOR_RAMP_INTERPOLATION_SETTING));
        struct stop
        {
            float position{};
            math::vec4 color{};
        };
        std::vector<stop> stops;
        for(uint32_t k = 0; k < count; ++k)
        {
            const auto color = find_input_pin(*state.def, get_material_color_ramp_color_pin(k));
            const auto position = find_input_pin(*state.def, get_material_color_ramp_position_pin(k));
            stops.push_back({get_value(state, position).x, get_value(state, color)});
        }
        std::stable_sort(stops.begin(),
                         stops.end(),
                         [](const stop& a, const stop& b)
                         {
                             return a.position < b.position;
                         });
        std::string code = format_literal(stops.front().color, MAX_WIDTH, false);
        for(size_t k = 1; k < stops.size(); ++k)
        {
            const std::string from = format_scalar(stops[k - 1].position);
            const std::string to = format_scalar(stops[k].position);
            std::string weight;
            if(interpolation == "constant")
            {
                weight = fmt::format("step({}, {})", to, value);
            }
            else
            {
                const std::string span = format_scalar(std::max(stops[k].position - stops[k - 1].position, 0.00001f));
                weight = fmt::format("saturate(({} - {}) / {})", value, from, span);
                if(interpolation == "smooth")
                {
                    weight = fmt::format("mg_smooth_weight({})", weight);
                }
            }
            code = fmt::format("mix({}, {}, {})", code, format_literal(stops[k].color, MAX_WIDTH, false), weight);
        }
        return code;
    }

    /// The index of input @p name of @p def; its first input when it has none of that name.
    static auto find_input_pin(const material_node_def& def, const std::string& name) -> size_t
    {
        const auto* pin = find_material_pin_def(def.inputs, name);
        return pin == nullptr ? 0 : size_t(pin - def.inputs.data());
    }

    /// True when the node has a float form and its dynamic inputs read floats.
    auto is_scalar_form(size_t index) -> bool
    {
        const auto& def = *states_[index].def;
        if(def.scalar_expression.empty())
        {
            return false;
        }
        const auto& input_types = get_info(index).input_types;
        for(size_t pin = 0; pin < def.inputs.size(); ++pin)
        {
            if(def.inputs[pin].type == material_value_type::dynamic)
            {
                return input_types[pin] == material_value_type::float1;
            }
        }
        return false;
    }

    /// The register of the parameter of the node's name and type; the node's default when there is none (a name
    /// another type took, reported, or no register left).
    auto make_parameter_code(size_t index) -> std::string
    {
        const auto& name = parameter_names_[index];
        const auto type = get_info(index).type;
        const auto it = std::find_if(result_.parameters.begin(),
                                     result_.parameters.end(),
                                     [&](const material_graph_parameter& parameter)
                                     {
                                         return parameter.name == name && parameter.type == type;
                                     });
        if(it == result_.parameters.end())
        {
            return format_literal(get_value(states_[index], 0), get_material_value_width(get_info(index).type), false);
        }
        const std::string reg = make_register_code(it->register_index);
        if(get_material_value_width(it->type) == MAX_WIDTH)
        {
            return reg;
        }
        return fmt::format("{}.{}", reg, COMPONENTS_XYZW[it->component]);
    }

    /// Decals: a texture read takes its gradients from the surface the decal lies on (mg_decal.sh), not from the
    /// screen's 2x2 quads, which straddle depth edges and pick a mip far too small there.
    auto is_decal_gradient_read(size_t index) -> bool
    {
        if(result_.domain != material_domain::decal || options_.is_preview || is_emitting_decal_uv_)
        {
            return false;
        }
        const auto* mode = find_material_setting_def(*states_[index].def, MATERIAL_TEXTURE_MIP_MODE_SETTING);
        return mode == nullptr || get_checked_setting(index, *mode) == "auto";
    }

    /// The UV a decal's texture read reads at, and at the two neighbours on its surface. The decal's own UV takes the
    /// main's gradients (is_own); a UV the graph computes from what a neighbour has (positions, UVs, time, parameters)
    /// is computed again there by a function of its own (emit_decal_uv_function); any other reads at the screen's
    /// gradients.
    struct decal_uv_codes
    {
        std::string uv;
        std::string uv_x;
        std::string uv_y;
        bool is_own{};
    };

    auto make_decal_uv_codes(size_t index, size_t pin) -> decal_uv_codes
    {
        decal_uv_codes codes;
        if(!is_input_linked(states_[index], pin))
        {
            codes.uv = get_input_code(index, pin);
            codes.is_own = true;
            return codes;
        }
        const std::string function = emit_decal_uv_function(index, pin);
        if(function.empty())
        {
            codes.uv = get_input_code(index, pin);
            return codes;
        }
        codes.uv = fmt::format("{}(i)", function);
        codes.uv_x = fmt::format("{}(mg_decal_neighbour_x(i))", function);
        codes.uv_y = fmt::format("{}(mg_decal_neighbour_y(i))", function);
        return codes;
    }

    /// Whether node @p index can be computed again from the inputs of another pixel: no texture read, no user code.
    auto can_evaluate_at_neighbour(size_t index) const -> bool
    {
        const auto* def = states_[index].def;
        if(def == nullptr)
        {
            return false;
        }
        switch(def->code)
        {
            case material_node_code::expression:
            case material_node_code::parameter:
            case material_node_code::append:
            case material_node_code::swizzle:
            case material_node_code::reroute:
            case material_node_code::named_reroute_declaration:
            case material_node_code::named_reroute:
            case material_node_code::static_switch:
            case material_node_code::function_input:
            case material_node_code::function_output:
            case material_node_code::function_call:
            case material_node_code::color_ramp:
                return true;
            default:
                return false;
        }
    }

    /// Emits `vec2 mg_decal_uv_<node>(MaterialInputs i)`, the UV input @p pin of node @p index computed from the
    /// inputs it is given, among the functions; empty when what it reads cannot run there. Its nodes are emitted again
    /// into it: the variables of evaluate_material are not in its scope.
    auto emit_decal_uv_function(size_t index, size_t pin) -> std::string
    {
        std::set<size_t> nodes;
        collect_input_nodes(index, pin, nodes);
        for(const size_t node : nodes)
        {
            if(!can_evaluate_at_neighbour(node))
            {
                return {};
            }
        }
        const std::string name = fmt::format("mg_decal_uv_{}", states_[index].variable);
        auto main_body = std::exchange(body_, {});
        auto main_lines = std::exchange(node_lines_, {});
        std::vector<std::pair<bool, std::string>> saved;
        saved.reserve(states_.size());
        for(auto& state : states_)
        {
            saved.emplace_back(state.is_emitted, state.value_code);
            state.is_emitted = false;
        }
        is_emitting_decal_uv_ = true;
        const std::string code = get_input_code(index, pin);
        is_emitting_decal_uv_ = false;
        auto function_body = std::exchange(body_, std::move(main_body));
        node_lines_ = std::move(main_lines);
        for(size_t i = 0; i < states_.size(); ++i)
        {
            states_[i].is_emitted = saved[i].first;
            states_[i].value_code = std::move(saved[i].second);
        }
        functions_.push_back(fmt::format("vec2 {}(MaterialInputs i)", name));
        functions_.emplace_back("{");
        functions_.insert(functions_.end(), function_body.begin(), function_body.end());
        functions_.push_back(fmt::format("{}return {};", INDENT, code));
        functions_.emplace_back("}");
        functions_.emplace_back();
        return name;
    }

    auto make_decal_texture_sample_code(size_t index, const std::string& sampler) -> std::string
    {
        const auto codes = make_decal_uv_codes(index, 0);
        if(codes.is_own)
        {
            return fmt::format("mg_decal_texture2D({}, {}, i)", sampler, codes.uv);
        }
        if(!codes.uv_x.empty())
        {
            return fmt::format("mg_decal_texture2D_at({}, {}, {}, {}, i)", sampler, codes.uv, codes.uv_x, codes.uv_y);
        }
        return make_sample_read(index, sampler, codes.uv, false);
    }

    auto make_texture_sample_code(size_t index) -> std::string
    {
        const bool is_cube_node = get_material_texture_node_kind(*states_[index].def) == material_value_type::texture_cube;
        if(!is_cube_node && is_decal_gradient_read(index))
        {
            const std::string sampler = get_texture_sampler(index);
            if(!sampler.empty())
            {
                return make_decal_texture_sample_code(index, sampler);
            }
        }
        const std::string coordinates = get_input_code(index, 0);
        const std::string sampler = get_texture_sampler(index);
        if(sampler.empty())
        {
            // No texture: what an empty cube reads (black), or an empty 2D slot (white).
            const bool is_cube = get_material_texture_node_kind(*states_[index].def) == material_value_type::texture_cube;
            return format_literal(is_cube ? math::vec4(0.0f, 0.0f, 0.0f, 1.0f) : math::vec4(1.0f), MAX_WIDTH, !is_cube);
        }
        const bool is_cube = get_material_texture_node_kind(*states_[index].def) == material_value_type::texture_cube;
        return make_sample_read(index, sampler, coordinates, is_cube);
    }

    auto make_normal_map_code(size_t index) -> std::string
    {
        const std::string sampler = get_texture_sampler(index);
        const int texture = find_read_texture(index);
        if(sampler.empty() || texture < 0)
        {
            return "vec3(0.0, 0.0, 1.0)";
        }
        // getTangentSpaceNormal (shaderlib.sh) reads the layout of the texture the draw binds.
        const auto slot = uint32_t(texture);
        const std::string layout_code = fmt::format("{}[{}].{}", NORMAL_CHANNELS_UNIFORM, slot / MAX_WIDTH, COMPONENTS_XYZW[slot % MAX_WIDTH]);
        if(is_decal_gradient_read(index))
        {
            const auto codes = make_decal_uv_codes(index, 0);
            const std::string strength = get_input_code(index, 1);
            if(codes.is_own)
            {
                return fmt::format("mg_decal_tangent_space_normal({}, {}, mg_decal_uv_ddx(i) * exp2(i.mip_bias), mg_decal_uv_ddy(i) * "
                                   "exp2(i.mip_bias), {}, {})",
                                   sampler,
                                   codes.uv,
                                   strength,
                                   layout_code);
            }
            if(!codes.uv_x.empty())
            {
                return fmt::format("mg_decal_tangent_space_normal({}, {}, ({} - {}) * exp2(i.mip_bias), ({} - {}) * exp2(i.mip_bias), {}, {})",
                                   sampler,
                                   codes.uv,
                                   codes.uv_x,
                                   codes.uv,
                                   codes.uv_y,
                                   codes.uv,
                                   strength,
                                   layout_code);
            }
            return fmt::format("getTangentSpaceNormal({}, {}, {}, {}, i.mip_bias)", sampler, codes.uv, strength, layout_code);
        }
        const std::string uv = get_input_code(index, 0);
        const std::string strength = get_input_code(index, 1);
        return fmt::format("getTangentSpaceNormal({}, {}, {}, {}, i.mip_bias)", sampler, uv, strength, layout_code);
    }

    auto make_append_code(size_t index) -> std::string
    {
        const auto& info = get_info(index);
        const uint32_t width = get_material_value_width(info.input_types[0]) + get_material_value_width(info.input_types[1]);
        const std::string a = get_input_code(index, 0);
        const std::string b = get_input_code(index, 1);
        if(width > MAX_WIDTH)
        {
            return format_literal({}, MAX_WIDTH, true);
        }
        return fmt::format("{}({}, {})", get_material_value_shader_type(info.type), a, b);
    }

    auto make_swizzle_code(size_t index) -> std::string
    {
        const auto& state = states_[index];
        const auto components = get_swizzle_mask(state);
        const std::string input = get_input_code(index, 0);
        const uint32_t width = get_material_value_width(get_info(index).input_types[0]);
        if(components.empty())
        {
            report(material_graph_severity::error,
                   state.node->id,
                   MASK_SETTING,
                   fmt::format("The mask \"{}\" is not one to four of x, y, z, w (or r, g, b, a).", get_setting(state, MASK_SETTING)));
            return input;
        }
        const bool reads_missing = std::any_of(components.begin(),
                                               components.end(),
                                               [&](int component)
                                               {
                                                   return component >= int(width);
                                               });
        if(reads_missing)
        {
            report(material_graph_severity::warning,
                   state.node->id,
                   MASK_SETTING,
                   fmt::format("The mask reads components a {} does not have: they read 0.",
                               get_material_value_type_name(get_material_value_type(width))));
        }
        return apply_components(input, width, components);
    }

    void emit_surface()
    {
        if(output_ < 0)
        {
            emit_function_surface();
            return;
        }
        if(!is_surface_domain())
        {
            emit_domain_output();
            return;
        }
        emit_discards();
        const size_t index = size_t(output_);
        const auto& state = states_[index];
        const auto& def = *state.def;
        std::vector<std::string> assignments;
        // Linked Material Attributes give every channel; then only the channels linked besides, and the Surface Output's
        // own (the clip and the fade), replace theirs.
        const size_t attributes_pin = find_input_pin(def, MATERIAL_ATTRIBUTES_PIN);
        const bool uses_attributes = is_input_linked(state, attributes_pin);
        if(uses_attributes)
        {
            assignments.push_back(fmt::format("{}s = {};", INDENT, get_input_code(index, attributes_pin)));
        }
        for(size_t pin = 0; pin < def.inputs.size(); ++pin)
        {
            const auto& name = def.inputs[pin].name;
            // The vertex stage's (emit_vertex); the other workflow's channels are not written.
            if(pin == attributes_pin || name == WORLD_POSITION_OFFSET_PIN || !is_output_pin_used(index, pin))
            {
                continue;
            }
            const bool is_own_channel = name == MATERIAL_OPACITY_MASK_CLIP_PIN || name == CAMERA_FADE_DISTANCE_PIN;
            if(uses_attributes && !is_own_channel && !is_input_linked(state, pin))
            {
                continue;
            }
            assignments.push_back(fmt::format("{}s.{} = {};", INDENT, name, get_input_code(index, pin)));
        }
        const size_t first_line = body_.size();
        body_.push_back(fmt::format("{}// {}: {}", INDENT, make_comment_text(state.node->id), def.label));
        body_.insert(body_.end(), assignments.begin(), assignments.end());
        node_lines_.push_back({index, first_line, body_.size() - first_line});
        collect_surface_defines(index);
    }

    /// The Decal or Post Process Output's channels. Post: a Color the graph does not set (is_input_set) keeps the
    /// scene's (main() fills it in). Decal: an unlinked Normal Opacity or Surface Opacity is Opacity times its value.
    void emit_domain_output()
    {
        emit_discards();
        const size_t index = size_t(output_);
        const auto& state = states_[index];
        const auto& def = *state.def;
        std::vector<std::string> assignments;
        for(size_t pin = 0; pin < def.inputs.size(); ++pin)
        {
            const auto& name = def.inputs[pin].name;
            const bool is_linked = is_input_linked(state, pin);
            if(name == MATERIAL_COLOR_PIN && !is_input_set(index, pin))
            {
                continue;
            }
            const bool is_relative_opacity = name == MATERIAL_NORMAL_OPACITY_PIN || name == MATERIAL_SURFACE_OPACITY_PIN;
            if(is_relative_opacity && !is_linked)
            {
                assignments.push_back(fmt::format("{}s.{} = s.{} * {};", INDENT, name, MATERIAL_OPACITY_PIN, get_input_code(index, pin)));
                continue;
            }
            assignments.push_back(fmt::format("{}s.{} = {};", INDENT, name, get_input_code(index, pin)));
        }
        const size_t first_line = body_.size();
        body_.push_back(fmt::format("{}// {}: {}", INDENT, make_comment_text(state.node->id), def.label));
        body_.insert(body_.end(), assignments.begin(), assignments.end());
        node_lines_.push_back({index, first_line, body_.size() - first_line});
        if(result_.domain == material_domain::decal)
        {
            collect_decal_defines(index);
        }
        else
        {
            collect_post_process_defines(index);
        }
    }

    /// A decal's defines: the channels it writes (those its Decal Output sets, is_input_set), how its normal blends, how
    /// far its ends fade.
    void collect_decal_defines(size_t index)
    {
        const auto& state = states_[index];
        const auto& def = *state.def;
        const auto is_pin_set = [&](const char* name)
        {
            return is_input_set(index, name);
        };
        uint32_t writes = 0;
        if(is_pin_set(BASE_COLOR_PIN))
        {
            writes |= material_decal_writes_base_color;
            defines_.push_back("#define DECAL_WRITES_BASE_COLOR 1");
        }
        if(is_pin_set(NORMAL_PIN))
        {
            writes |= material_decal_writes_normal;
            defines_.push_back("#define DECAL_WRITES_NORMAL 1");
        }
        if(is_pin_set(MATERIAL_METALLIC_PIN) || is_pin_set(MATERIAL_ROUGHNESS_PIN))
        {
            writes |= material_decal_writes_surface;
            defines_.push_back("#define DECAL_WRITES_SURFACE 1");
            defines_.push_back(fmt::format("#define DECAL_WRITES_METALLIC {}", is_pin_set(MATERIAL_METALLIC_PIN) ? 1 : 0));
            defines_.push_back(fmt::format("#define DECAL_WRITES_ROUGHNESS {}", is_pin_set(MATERIAL_ROUGHNESS_PIN) ? 1 : 0));
        }
        if(is_pin_set(EMISSIVE_PIN))
        {
            writes |= material_decal_writes_emissive;
            defines_.push_back("#define DECAL_WRITES_EMISSIVE 1");
        }
        if(writes == 0)
        {
            report(material_graph_severity::warning,
                   state.node->id,
                   {},
                   "The Decal Output sets no channel: link one, or give it a value, or the decal leaves every surface "
                   "as it is.");
        }
        result_.decal_writes = writes;
        const auto* blend = find_material_setting_def(def, MATERIAL_DECAL_NORMAL_BLEND_SETTING);
        const bool is_replace = blend != nullptr && get_checked_setting(index, *blend) == MATERIAL_DECAL_NORMAL_BLEND_REPLACE;
        if(is_replace)
        {
            defines_.push_back("#define DECAL_NORMAL_REPLACE 1");
        }
        // A surface read, or metallic and roughness written apart (the other kept), reads the G-buffer the decal lies on.
        const bool writes_one_of_metallic_roughness = (writes & material_decal_writes_surface) != 0 &&
                                                      is_pin_set(MATERIAL_METALLIC_PIN) != is_pin_set(MATERIAL_ROUGHNESS_PIN);
        const bool blends_detail_normal = (writes & material_decal_writes_normal) != 0 && !is_replace;
        result_.decal_reads_surface = result_.decal_reads_surface || blends_detail_normal || writes_one_of_metallic_roughness;
        if(result_.decal_reads_surface)
        {
            defines_.push_back("#define DECAL_READS_SURFACE 1");
        }
        const auto* edge = find_material_setting_def(def, MATERIAL_DECAL_EDGE_FADE_SETTING);
        const float edge_fade = edge != nullptr ? std::clamp(parse_number(index, *edge, get_setting(state, edge->name)), 0.0f, 0.5f) : 0.0f;
        defines_.push_back(fmt::format("#define DECAL_EDGE_FADE {}", format_float_literal(edge_fade)));
        add_scene_texture_defines();
    }

    /// A post-process material's defines: its Location's kind of colour, the view textures it reads.
    void collect_post_process_defines(size_t index)
    {
        const auto& state = states_[index];
        const auto& def = *state.def;
        const auto* location = find_material_setting_def(def, MATERIAL_POST_PROCESS_LOCATION_SETTING);
        if(location != nullptr)
        {
            result_.post_location =
                find_material_post_process_location(get_checked_setting(index, *location)).value_or(material_post_process_location::after_tonemapping);
        }
        const auto* priority = find_material_setting_def(def, MATERIAL_POST_PROCESS_PRIORITY_SETTING);
        if(priority != nullptr)
        {
            result_.post_priority = int(std::lround(parse_number(index, *priority, get_setting(state, priority->name))));
        }
        const auto* mask = find_material_setting_def(def, MATERIAL_POST_PROCESS_MASK_SETTING);
        if(mask != nullptr)
        {
            const auto value = std::clamp(std::lround(parse_number(index, *mask, get_setting(state, mask->name))), 0L, 255L);
            if(value > 0)
            {
                // The mask decides where it runs: the pass draws the masked renderers and binds their mask.
                result_.scene_textures |= material_scene_texture_custom_mask;
                defines_.push_back(fmt::format("#define POST_PROCESS_RUN_WHERE_MASK {}", value));
            }
        }
        add_scene_texture_defines();
    }

    /// MG_READS_<texture> for each view texture a Scene Texture node reads: the mains declare only those samplers.
    void add_scene_texture_defines()
    {
        const std::array<std::pair<uint32_t, const char*>, 8> names = {{{material_scene_texture_color, "MG_READS_SCENE_COLOR"},
                                                                        {material_scene_texture_depth, "MG_READS_SCENE_DEPTH"},
                                                                        {material_scene_texture_gbuffer0, "MG_READS_GBUFFER0"},
                                                                        {material_scene_texture_gbuffer1, "MG_READS_GBUFFER1"},
                                                                        {material_scene_texture_gbuffer2, "MG_READS_GBUFFER2"},
                                                                        {material_scene_texture_velocity, "MG_READS_VELOCITY"},
                                                                        {material_scene_texture_custom_mask, "MG_READS_CUSTOM_MASK"},
                                                                        {material_scene_texture_custom_depth, "MG_READS_CUSTOM_DEPTH"}}};
        for(const auto& [bit, name] : names)
        {
            if((result_.scene_textures & bit) != 0)
            {
                defines_.push_back(fmt::format("#define {} 1", name));
            }
        }
    }

    /// Estimates what the Surface Output's Emissive gives off, for the GI (material_graph_emissive_estimate).
    void estimate_emissive()
    {
        // A function compiled on its own has no Surface Output: it gives off nothing.
        if(output_ < 0)
        {
            return;
        }
        const size_t index = size_t(output_);
        const auto& state = states_[index];
        const auto& def = *state.def;
        const auto* pin = find_material_pin_def(def.inputs, EMISSIVE_PIN);
        if(pin == nullptr)
        {
            return;
        }
        const auto emissive_pin = size_t(pin - def.inputs.data());
        const size_t attributes_pin = find_input_pin(def, MATERIAL_ATTRIBUTES_PIN);
        if(is_input_linked(state, attributes_pin) && !is_input_linked(state, emissive_pin))
        {
            result_.emissive = estimate_attributes_emissive(state.sources[attributes_pin], 0);
            return;
        }
        result_.emissive = estimate_input(index, emissive_pin, 0);
    }

    /// The Emissive of the attributes @p source reads, as the GI estimates it: a Make Material Attributes' input,
    /// through the nodes passing it on; unknown for a blend of two and anything else.
    auto estimate_attributes_emissive(const link_source& source, int depth) const -> material_graph_emissive_estimate
    {
        if(!source.is_linked() || depth > MAX_ESTIMATE_DEPTH)
        {
            return {};
        }
        const auto node = size_t(source.node);
        const auto& state = states_[node];
        if(state.def == nullptr)
        {
            return {};
        }
        if(state.def->type == MAKE_ATTRIBUTES_TYPE)
        {
            return estimate_input(node, find_input_pin(*state.def, EMISSIVE_PIN), depth + 1);
        }
        const auto code = state.def->code;
        if(code == material_node_code::reroute || code == material_node_code::named_reroute_declaration ||
           code == material_node_code::named_reroute)
        {
            return estimate_attributes_emissive(state.sources[0], depth + 1);
        }
        if(code == material_node_code::static_switch)
        {
            return estimate_attributes_emissive(state.sources[get_chosen_input(state)], depth + 1);
        }
        return {};
    }

    /// Whether the attributes @p source reads may be cut out: a Make Material Attributes whose Opacity Mask is linked or
    /// below 1, a blend of such, through the nodes passing them on; a function's attributes may be.
    auto attributes_use_mask(const link_source& source, int depth) const -> bool
    {
        if(!source.is_linked() || depth > MAX_ESTIMATE_DEPTH)
        {
            return false;
        }
        const auto node = size_t(source.node);
        const auto& state = states_[node];
        if(state.def == nullptr)
        {
            return false;
        }
        if(state.def->type == MAKE_ATTRIBUTES_TYPE)
        {
            const size_t mask = find_input_pin(*state.def, OPACITY_MASK_PIN);
            return is_input_linked(state, mask) || get_value(state, mask).x < 1.0f;
        }
        if(state.def->type == BLEND_ATTRIBUTES_TYPE)
        {
            return attributes_use_mask(state.sources[0], depth + 1) || attributes_use_mask(state.sources[1], depth + 1);
        }
        const auto code = state.def->code;
        if(code == material_node_code::reroute || code == material_node_code::named_reroute_declaration ||
           code == material_node_code::named_reroute)
        {
            return attributes_use_mask(state.sources[0], depth + 1);
        }
        if(code == material_node_code::static_switch)
        {
            return attributes_use_mask(state.sources[get_chosen_input(state)], depth + 1);
        }
        return true;
    }

    /// Whether input @p pin of @p state is linked, as a whole or by a component.
    static auto is_input_linked(const node_state& state, size_t pin) -> bool
    {
        return pin < state.sources.size() && (state.sources[pin].is_linked() || state.is_composed[pin]);
    }

    /// Whether the graph sets input @p pin of node @p index: a link, or a value of its own other than the pin's default
    /// (a link taken away leaves its input behind at the default). An output node writes only the channels it sets.
    auto is_input_set(size_t index, size_t pin) const -> bool
    {
        const auto& state = states_[index];
        if(is_input_linked(state, pin))
        {
            return true;
        }
        const auto& pin_def = state.def->inputs[pin];
        const math::vec4 value = get_value(state, pin);
        const uint32_t width = std::max(get_material_value_width(pin_def.type), 1u);
        for(uint32_t component = 0; component < std::min(width, 4u); ++component)
        {
            if(value[int(component)] != pin_def.default_value[int(component)])
            {
                return true;
            }
        }
        return false;
    }

    /// is_input_set for the pin named @p name of node @p index; false for a node without one.
    auto is_input_set(size_t index, hpp::string_view name) const -> bool
    {
        const auto& def = *states_[index].def;
        const auto* pin = find_material_pin_def(def.inputs, name);
        return pin != nullptr && is_input_set(index, size_t(pin - def.inputs.data()));
    }

    /// The estimate of input @p pin of node @p index: its own value while unlinked.
    auto estimate_input(size_t index, size_t pin, int depth) const -> material_graph_emissive_estimate
    {
        const auto& state = states_[index];
        if(pin >= state.sources.size() || state.is_composed[pin])
        {
            return {};
        }
        if(!state.sources[pin].is_linked())
        {
            material_graph_emissive_estimate estimate;
            estimate.is_known = true;
            estimate.constant = math::vec3(get_value(state, pin));
            return estimate;
        }
        return estimate_node(size_t(state.sources[pin].node), depth + 1);
    }

    /// The estimate of node @p index's value: a parameter, a texture (white), a constant, a pass-through or a Multiply.
    auto estimate_node(size_t index, int depth) const -> material_graph_emissive_estimate
    {
        const auto& state = states_[index];
        material_graph_emissive_estimate estimate;
        if(state.def == nullptr || depth > MAX_ESTIMATE_DEPTH)
        {
            return estimate;
        }
        switch(state.def->code)
        {
            case material_node_code::parameter:
            {
                const auto name = parameter_names_.find(index);
                if(name == parameter_names_.end())
                {
                    return estimate;
                }
                estimate.is_known = true;
                estimate.constant = math::vec3(1.0f);
                estimate.parameters.push_back(name->second);
                return estimate;
            }
            case material_node_code::texture_sample:
            case material_node_code::triplanar_sample:
                estimate.is_known = true;
                estimate.constant = math::vec3(1.0f);
                return estimate;
            case material_node_code::reroute:
            case material_node_code::named_reroute_declaration:
            case material_node_code::named_reroute:
                return estimate_input(index, 0, depth);
            case material_node_code::static_switch:
                return estimate_input(index, get_chosen_input(state), depth);
            case material_node_code::expression:
                break;
            default:
                return estimate;
        }
        if(state.def->expression == CONSTANT_EXPRESSION)
        {
            return estimate_input(index, 0, depth);
        }
        if(state.def->type != MULTIPLY_TYPE)
        {
            return estimate;
        }
        auto product = estimate_input(index, 0, depth);
        const auto other = estimate_input(index, 1, depth);
        if(!product.is_known || !other.is_known)
        {
            return {};
        }
        product.constant *= other.constant;
        product.parameters.insert(product.parameters.end(), other.parameters.begin(), other.parameters.end());
        return product;
    }

    /// Every Discard node's test, before the surface is written: the pixel goes as soon as one fails.
    void emit_discards()
    {
        for(size_t i = 0; i < states_.size(); ++i)
        {
            if(is_used_kind_def(i, material_node_code::discard) && is_in_domain(i))
            {
                emit_discard(i);
            }
        }
    }

    void emit_discard(size_t index)
    {
        const std::string value = get_input_code(index, 0);
        const std::string threshold = get_input_code(index, 1);
        const size_t first_line = body_.size();
        body_.push_back(fmt::format("{}// {}: {}", INDENT, make_comment_text(states_[index].node->id), states_[index].def->label));
        body_.push_back(fmt::format("{}{}({} < {});", INDENT, DISCARD_MACRO, value, threshold));
        node_lines_.push_back({index, first_line, body_.size() - first_line});
    }

    /// Whether a Discard node is in the shader, and whether one of them reads the time, the camera or the screen.
    auto find_discards() const -> std::pair<bool, bool>
    {
        bool has_discard = false;
        bool is_animated = false;
        for(size_t i = 0; i < states_.size(); ++i)
        {
            if(is_used_kind_def(i, material_node_code::discard) && result_.nodes[i].is_used)
            {
                has_discard = true;
                is_animated = is_animated || is_input_changing(i, 0) || is_input_changing(i, 1);
            }
        }
        return {has_discard, is_animated};
    }

    /// A function compiled on its own draws its first output as the base color.
    void emit_function_surface()
    {
        emit_discards();
        for(size_t i = 0; i < authored_count_; ++i)
        {
            if(!is_used_kind_def(i, material_node_code::function_output))
            {
                continue;
            }
            const std::string value = convert(get_input_code(i, 0), get_material_value_width(get_info(i).input_types[0]), 3, states_[i], "in");
            body_.push_back(fmt::format("{}s.base_color = {};", INDENT, value));
            return;
        }
    }

    void collect_surface_defines(size_t index)
    {
        const auto& state = states_[index];
        const auto& def = *state.def;
        if(get_checked_setting(index, *find_material_setting_def(def, TWO_SIDED_SETTING)) == MATERIAL_TWO_SIDED_ON)
        {
            defines_.push_back("#define MATERIAL_TWO_SIDED 1");
        }
        if(get_checked_setting(index, *find_material_setting_def(def, MATERIAL_WORKFLOW_SETTING)) == MATERIAL_WORKFLOW_SPECULAR)
        {
            defines_.push_back("#define MATERIAL_SPECULAR_WORKFLOW 1");
        }
        const auto* mask_pin = find_material_pin_def(def.inputs, OPACITY_MASK_PIN);
        const size_t mask_index = size_t(mask_pin - def.inputs.data());
        const size_t clip_index = size_t(find_material_pin_def(def.inputs, MATERIAL_OPACITY_MASK_CLIP_PIN) - def.inputs.data());
        const size_t attributes_index = find_input_pin(def, MATERIAL_ATTRIBUTES_PIN);
        const bool attributes_mask = is_input_linked(state, attributes_index) && !is_input_linked(state, mask_index) &&
                                     attributes_use_mask(state.sources[attributes_index], 0);
        const bool uses_mask = state.sources[mask_index].is_linked() || state.is_composed[mask_index] ||
                               get_value(state, mask_index).x < mask_pin->default_value.x || attributes_mask;
        collect_blend_defines(index);
        // A see-through surface reads its mask as its opacity; only an opaque one clips by it.
        const bool clips_by_mask = uses_mask && !result_.is_translucent();
        // A Discard node clips like the mask: the shader gets the shadow casters, which run it too.
        const auto [has_discard, is_discard_animated] = find_discards();
        if(clips_by_mask || has_discard)
        {
            defines_.push_back("#define MATERIAL_OPACITY_MASK 1");
            result_.uses_opacity_mask = true;
            // The forward shadow's dither may have marked it animated already (collect_blend_defines).
            result_.is_opacity_mask_animated =
                result_.is_opacity_mask_animated ||
                (clips_by_mask && (is_input_changing(index, mask_index) || is_input_changing(index, clip_index) ||
                                   (attributes_mask && is_input_changing(index, attributes_index)))) ||
                is_discard_animated;
        }
        const size_t offset_index = get_offset_pin(def);
        const math::vec4 offset_value = get_value(state, offset_index);
        result_.uses_world_position_offset = state.sources[offset_index].is_linked() || state.is_composed[offset_index] ||
                                             offset_value.x != 0.0f || offset_value.y != 0.0f || offset_value.z != 0.0f;
        const auto& max_offset = *find_material_setting_def(def, MAX_WORLD_POSITION_OFFSET_SETTING);
        result_.max_world_position_offset = std::max(0.0f, parse_number(index, max_offset, get_setting(state, max_offset.name)));
    }

    /// The most textures the graph reads: its domain's, and for a surface its Blend Mode's and Shading Model's.
    auto get_texture_limit() const -> uint32_t
    {
        if(!is_surface_domain() || output_ < 0 || states_[size_t(output_)].node == nullptr)
        {
            return get_material_graph_max_textures(result_.domain);
        }
        const auto& output = *states_[size_t(output_)].node;
        const uint32_t scene_textures = result_.scene_textures | get_output_refraction_reads();
        return get_material_graph_max_textures(get_material_blend_mode(output), get_material_shading_model(output), scene_textures);
    }

    /// What the texture limit's message names the material: its Blend Mode in the translucency pass, else its domain.
    auto get_texture_limit_label() const -> const char*
    {
        if(is_surface_domain() && output_ >= 0 && states_[size_t(output_)].node != nullptr)
        {
            const auto mode = get_material_blend_mode(*states_[size_t(output_)].node);
            if(is_material_blend_mode_forward(mode))
            {
                return get_material_blend_mode_label(mode);
            }
        }
        return get_material_domain_label(result_.domain);
    }

    /// The Blend Mode, Shading Model and their settings: the result's traits and the defines the mains read.
    void collect_blend_defines(size_t index)
    {
        const auto& state = states_[index];
        const auto& def = *state.def;
        result_.blend_mode = find_material_blend_mode(get_checked_setting(index, *find_material_setting_def(def, MATERIAL_BLEND_MODE_SETTING)))
                                 .value_or(material_blend_mode::opaque);
        result_.shading_model = get_material_shading_model(*state.node);
        result_.preserve_specular =
            get_checked_setting(index, *find_material_setting_def(def, MATERIAL_PRESERVE_SPECULAR_SETTING)) == MATERIAL_PRESERVE_SPECULAR_ON;
        const bool can_be_order_independent = is_material_blend_mode_order_dependent(result_.blend_mode);
        result_.order_independent =
            can_be_order_independent &&
            get_checked_setting(index, *find_material_setting_def(def, MATERIAL_ORDER_INDEPENDENT_SETTING)) == MATERIAL_ORDER_INDEPENDENT_ON;
        result_.depth_prepass = can_be_order_independent && !result_.order_independent &&
                                get_checked_setting(index, *find_material_setting_def(def, MATERIAL_DEPTH_PREPASS_SETTING)) ==
                                    MATERIAL_SETTING_ON;
        result_.writes_velocity = is_material_blend_mode_forward(result_.blend_mode) &&
                                  get_checked_setting(index, *find_material_setting_def(def, MATERIAL_WRITE_VELOCITY_SETTING)) ==
                                      MATERIAL_SETTING_ON;
        const auto& taa_response = *find_material_setting_def(def, MATERIAL_TAA_RESPONSE_SETTING);
        result_.taa_response = std::clamp(parse_number(index, taa_response, get_setting(state, taa_response.name)), 0.0f, 1.0f);
        if(result_.blend_mode == material_blend_mode::dithered)
        {
            defines_.push_back("#define MATERIAL_DITHERED 1");
        }
        if(result_.is_forward())
        {
            defines_.push_back("#define MATERIAL_FORWARD 1");
            defines_.push_back(fmt::format("#define MATERIAL_BLEND_{} 1", string_utils::to_upper(get_material_blend_mode_key(result_.blend_mode))));
            if(result_.preserve_specular && result_.blend_mode == material_blend_mode::translucent)
            {
                defines_.push_back("#define MATERIAL_PRESERVE_SPECULAR 1");
            }
            defines_.push_back(fmt::format("#define MATERIAL_TAA_RESPONSE {}", format_float_literal(result_.taa_response)));
            result_.casts_forward_shadow =
                get_checked_setting(index, *find_material_setting_def(def, MATERIAL_CAST_SHADOW_SETTING)) == MATERIAL_CAST_SHADOW_DITHERED;
            if(result_.casts_forward_shadow)
            {
                defines_.push_back("#define MATERIAL_FORWARD_SHADOW 1");
                // A dithered shadow changes with its Opacity: animated where the Opacity is.
                const size_t opacity_index = size_t(find_material_pin_def(def.inputs, OPACITY_MASK_PIN) - def.inputs.data());
                result_.is_opacity_mask_animated = result_.is_opacity_mask_animated || is_input_changing(index, opacity_index);
            }
        }
        if(result_.shading_model == material_shading_model::unlit)
        {
            defines_.push_back("#define MATERIAL_UNLIT 1");
        }
        if(result_.is_forward())
        {
            if(is_output_refracting())
            {
                defines_.push_back("#define MATERIAL_REFRACTION 1");
                if(is_output_rough_refracting())
                {
                    defines_.push_back("#define MATERIAL_ROUGH_REFRACTION 1");
                }
                result_.scene_textures |= get_output_refraction_reads();
            }
            add_scene_texture_defines();
        }
    }

    static auto get_offset_pin(const material_node_def& def) -> size_t
    {
        return size_t(find_material_pin_def(def.inputs, WORLD_POSITION_OFFSET_PIN) - def.inputs.data());
    }

    /// What the pixel stage read of the vertex stage, once the surface is emitted: the object's transform and the
    /// Vertex Interpolators, which the vertex shader writes; the shader's defines for them.
    void collect_vertex_outputs()
    {
        const auto reads_object = [](const std::string& line)
        {
            return std::any_of(OBJECT_DATA_CODES.begin(),
                               OBJECT_DATA_CODES.end(),
                               [&line](const char* code)
                               {
                                   return line.find(code) != std::string::npos;
                               });
        };
        result_.reads_object_data = std::any_of(body_.begin(), body_.end(), reads_object) ||
                                    std::any_of(functions_.begin(), functions_.end(), reads_object);
        result_.interpolator_count = uint32_t(interpolator_slots_.size());
        if(result_.reads_object_data)
        {
            defines_.push_back("#define MATERIAL_OBJECT_DATA 1");
        }
        if(result_.interpolator_count > 0)
        {
            defines_.push_back(fmt::format("#define MATERIAL_INTERPOLATORS {}", result_.interpolator_count));
        }
        if(result_.reads_object_data && result_.interpolator_count > MAX_INTERPOLATORS_WITH_OBJECT_DATA)
        {
            for(const auto& [node, slot] : interpolator_slots_)
            {
                if(slot >= MAX_INTERPOLATORS_WITH_OBJECT_DATA)
                {
                    report(material_graph_severity::warning,
                           states_[node].node->id,
                           {},
                           fmt::format("With the object's transform the graph reads, more than {} Vertex Interpolators may not "
                                       "fit the 15 varyings phones and browsers guarantee.",
                                       MAX_INTERPOLATORS_WITH_OBJECT_DATA));
                }
            }
        }
    }

    /// Emits the vertex shader's code: World Position Offset and each Vertex Interpolator the pixel stage reads. The
    /// nodes are emitted again, as the two stages share no variables, and the Custom functions they call alone, as code
    /// a vertex cannot run must stay out of its shader. A graph whose pixels read the object's transform alone gets a
    /// vertex shader that computes nothing.
    void emit_vertex()
    {
        const bool has_vertex_stage = result_.uses_world_position_offset || result_.reads_vertex_outputs();
        if(!has_vertex_stage)
        {
            return;
        }
        auto fragment_body = std::exchange(body_, {});
        auto fragment_lines = std::exchange(node_lines_, {});
        auto fragment_functions = std::exchange(functions_, {});
        for(auto& state : states_)
        {
            state.is_emitted = false;
        }
        is_emitting_vertex_ = true;
        if(result_.uses_world_position_offset)
        {
            const size_t index = size_t(output_);
            const size_t pin = get_offset_pin(*states_[index].def);
            if(emit_vertex_root(WORLD_POSITION_OFFSET_PIN, index, pin))
            {
                result_.is_world_position_offset_animated = is_input_changing(index, pin);
            }
            else
            {
                result_.uses_world_position_offset = false;
                result_.is_world_position_offset_animated = false;
            }
        }
        for(const auto& [node, slot] : interpolator_slots_)
        {
            emit_vertex_root(fmt::format("interpolator{}", slot), node, 0);
        }
        is_emitting_vertex_ = false;
        vertex_body_ = std::move(body_);
        vertex_functions_ = std::move(functions_);
        body_ = std::move(fragment_body);
        node_lines_ = std::move(fragment_lines);
        functions_ = std::move(fragment_functions);
    }

    /// Emits `s.<target> = <input pin of node index>;` into the vertex stage, what it reads before it. False, with
    /// everything it emitted taken back, when it reads a node a vertex cannot run: the target keeps its zero.
    auto emit_vertex_root(const std::string& target, size_t index, size_t pin) -> bool
    {
        const size_t body_size = body_.size();
        const size_t lines_size = node_lines_.size();
        const size_t functions_size = functions_.size();
        std::vector<bool> was_emitted;
        was_emitted.reserve(states_.size());
        for(const auto& state : states_)
        {
            was_emitted.push_back(state.is_emitted);
        }
        std::string code = get_input_code(index, pin);
        if(check_vertex_nodes(was_emitted))
        {
            if(target != WORLD_POSITION_OFFSET_PIN)
            {
                code = pad_to_vec4(code, get_material_value_width(get_info(index).input_types[pin]));
            }
            body_.push_back(fmt::format("{}s.{} = {};", INDENT, target, code));
            return true;
        }
        body_.resize(body_size);
        node_lines_.resize(lines_size);
        functions_.resize(functions_size);
        for(size_t i = 0; i < states_.size(); ++i)
        {
            states_[i].is_emitted = was_emitted[i];
        }
        return false;
    }

    /// @p code of @p width components as a vec4, the missing ones 0.
    static auto pad_to_vec4(const std::string& code, uint32_t width) -> std::string
    {
        switch(width)
        {
            case 1:
                return fmt::format("vec4({}, 0.0, 0.0, 0.0)", code);
            case 2:
                return fmt::format("vec4({}, 0.0, 0.0)", code);
            case 3:
                return fmt::format("vec4({}, 0.0)", code);
            default:
                return code;
        }
    }

    /// Reports the nodes the vertex stage emitted since @p was_emitted that a vertex cannot run. False when one cannot:
    /// what reads it is dropped, as a graph with errors still compiles.
    auto check_vertex_nodes(const std::vector<bool>& was_emitted) -> bool
    {
        bool can_run = true;
        for(size_t i = 0; i < states_.size(); ++i)
        {
            const auto& state = states_[i];
            if(!state.is_emitted || was_emitted[i] || !state.def)
            {
                continue;
            }
            if(state.def->code == material_node_code::normal_map)
            {
                report(material_graph_severity::error,
                       state.node->id,
                       {},
                       "A Normal Map gives a pixel's normal: the vertex stage (World Position Offset, Vertex Interpolators) "
                       "cannot read one, so what reads it there stays 0.");
                can_run = false;
            }
            else if(state.def->is_pixel_only)
            {
                report(material_graph_severity::error,
                       state.node->id,
                       {},
                       fmt::format("A {} reads how values change between pixels, which a vertex does not have: the vertex "
                                   "stage (World Position Offset, Vertex Interpolators) cannot read one, so what reads it "
                                   "there stays 0.",
                                   state.def->label));
                can_run = false;
            }
            else if(state.node->type == SCREEN_POSITION_TYPE)
            {
                report(material_graph_severity::warning,
                       state.node->id,
                       {},
                       "A vertex has no pixel: the vertex stage reads the middle of the view as the Screen Position.");
            }
        }
        return can_run;
    }

    /// Adds the nodes input @p pin of node @p index reads, directly or through other nodes, to @p nodes.
    void collect_input_nodes(size_t index, size_t pin, std::set<size_t>& nodes) const
    {
        const auto& state = states_[index];
        const auto add = [&](const link_source& source)
        {
            if(!source.is_linked() || !nodes.insert(size_t(source.node)).second)
            {
                return;
            }
            for(size_t source_pin = 0; source_pin < states_[size_t(source.node)].sources.size(); ++source_pin)
            {
                collect_input_nodes(size_t(source.node), source_pin, nodes);
            }
        };
        add(state.sources[pin]);
        for(const auto& source : state.component_sources[pin])
        {
            add(source);
        }
    }

    /// Whether what feeds input @p pin of node @p index reads a value that changes without an edit: the code of the
    /// nodes it reads, and its own assignment, name the time, the camera or the screen.
    auto is_input_changing(size_t index, size_t pin) const -> bool
    {
        const auto reads_changing_input = [](const std::string& line)
        {
            return std::any_of(CHANGING_INPUT_CODES.begin(),
                               CHANGING_INPUT_CODES.end(),
                               [&line](const char* code)
                               {
                                   return line.find(code) != std::string::npos;
                               });
        };
        std::set<size_t> nodes;
        collect_input_nodes(index, pin, nodes);
        const bool has_changing_function = std::any_of(nodes.begin(),
                                                       nodes.end(),
                                                       [this](size_t node)
                                                       {
                                                           return states_[node].reads_changing_input;
                                                       });
        if(has_changing_function)
        {
            return true;
        }
        const std::string assignment = fmt::format("{}s.{} = ", INDENT, states_[index].def->inputs[pin].name);
        // A Discard node's test is its own line, where inline inputs (the time) are written.
        const bool is_discard = states_[index].def->code == material_node_code::discard;
        for(const auto& range : node_lines_)
        {
            const bool is_read = nodes.count(range.node) != 0;
            for(size_t line = range.first; line < range.first + range.count; ++line)
            {
                const bool is_assignment = range.node == index && (is_discard || body_[line].rfind(assignment, 0) == 0);
                if((is_read || is_assignment) && reads_changing_input(body_[line]))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void add_lines(std::vector<std::string>& lines, const std::string& text)
    {
        size_t position = 0;
        while(position <= text.size())
        {
            const size_t end = text.find('\n', position);
            lines.push_back(text.substr(position, end == std::string::npos ? std::string::npos : end - position));
            if(end == std::string::npos)
            {
                break;
            }
            position = end + 1;
        }
    }

    /// The Surface Output's defines (MATERIAL_TWO_SIDED, MATERIAL_OPACITY_MASK), before the includes: material_graph.sh
    /// and the mains test them with #if, which reads a define only once it is written.
    void add_defines(std::vector<std::string>& lines)
    {
        lines.insert(lines.end(), defines_.begin(), defines_.end());
        if(!defines_.empty())
        {
            lines.emplace_back();
        }
    }

    void add_declarations(std::vector<std::string>& lines)
    {
        // Named by node, never by texture: the texture is bound at draw time, so picking another one leaves the code,
        // and the shader compiled from it, as they are.
        for(const auto& texture : result_.textures)
        {
            const char* declaration = texture.kind == material_value_type::texture_cube ? "SAMPLERCUBE" : "SAMPLER2D";
            lines.push_back(fmt::format("{}({}, {}); // {}", declaration, texture.sampler, texture.stage, make_comment_text(texture.node_id)));
        }
        const bool reads_normal_map = std::any_of(result_.textures.begin(),
                                                  result_.textures.end(),
                                                  [](const material_graph_texture& texture)
                                                  {
                                                      return texture.is_normal_map;
                                                  });
        if(reads_normal_map)
        {
            lines.push_back("// Where each normal map keeps its vector, by texture: the texture's own, set at draw time.");
            lines.push_back(fmt::format("uniform vec4 {}[{}];", NORMAL_CHANNELS_UNIFORM, MATERIAL_GRAPH_NORMAL_CHANNEL_REGISTERS));
        }
        if(!result_.textures.empty())
        {
            lines.emplace_back();
        }
        if(result_.parameter_registers > 0)
        {
            lines.push_back("// Parameters, set by name through the material:");
            for(const auto& parameter : result_.parameters)
            {
                const bool is_vector = get_material_value_width(parameter.type) == MAX_WIDTH;
                const std::string component = is_vector ? std::string{} : fmt::format(".{}", COMPONENTS_XYZW[parameter.component]);
                lines.push_back(
                    fmt::format("//   {} = {}[{}]{}", make_comment_text(parameter.name), PARAMETERS_UNIFORM, parameter.register_index, component));
            }
            lines.push_back(fmt::format("uniform vec4 {}[{}];", PARAMETERS_UNIFORM, result_.parameter_registers));
            add_instance_register_lines(lines);
            lines.emplace_back();
        }
    }

    /// What code reads register @p index through: the first ones by name, which the instance_material variant reads
    /// per instance.
    static auto make_register_code(uint32_t index) -> std::string
    {
        if(index < material::MAX_INSTANCE_CONSTANTS)
        {
            return fmt::format("{}{}", INSTANCE_REGISTER_PREFIX, index);
        }
        return fmt::format("{}[{}]", PARAMETERS_UNIFORM, index);
    }

    /// The names of the first registers: the instance's values in the instance_material variant (MaterialInputs,
    /// from v_material0..3), the uniform's otherwise.
    void add_instance_register_lines(std::vector<std::string>& lines) const
    {
        const uint32_t count = std::min<uint32_t>(result_.parameter_registers, material::MAX_INSTANCE_CONSTANTS);
        lines.emplace_back("#ifdef MATERIAL_GRAPH_INSTANCE_MATERIAL");
        for(uint32_t i = 0; i < count; ++i)
        {
            lines.push_back(fmt::format("#define {}{} i.instance_register{}", INSTANCE_REGISTER_PREFIX, i, i));
        }
        lines.emplace_back("#else");
        for(uint32_t i = 0; i < count; ++i)
        {
            lines.push_back(fmt::format("#define {}{} {}[{}]", INSTANCE_REGISTER_PREFIX, i, PARAMETERS_UNIFORM, i));
        }
        lines.emplace_back("#endif");
    }

    /// Emits every node with a swatch, each followed by MG_PREVIEW, which gives its value as a colour when it is the
    /// one asked for (PREVIEW_SELECTION): where it returns, a swatch computes what its node reads and what was emitted
    /// before it, not the whole graph.
    void emit_previews()
    {
        // The graph's own nodes have swatches; the nodes calls put in it are computed where they are read.
        for(size_t i = 0; i < authored_count_; ++i)
        {
            if(!has_material_graph_preview(graph_, result_, i))
            {
                continue;
            }
            emit_node(i);
            body_.push_back(fmt::format("{}MG_PREVIEW({}, {});", INDENT, format_float_literal(float(i)), make_preview_color(i)));
        }
    }

    /// The value of node @p index as an opaque colour: a float grey, a vector's first components.
    auto make_preview_color(size_t index) const -> std::string
    {
        switch(result_.nodes[index].type)
        {
            case material_value_type::texture2d:
                return states_[index].value_code.empty() ? std::string("vec4_splat(1.0)")
                                                         : fmt::format("mg_texture2D({}, i.uv0, 0.0)", states_[index].value_code);
            case material_value_type::texture_cube:
                return states_[index].value_code.empty()
                           ? std::string("vec4(0.0, 0.0, 0.0, 1.0)")
                           : fmt::format("textureCubeLod({}, mg_preview_direction(i.uv0), 0.0)", states_[index].value_code);
            case material_value_type::attributes:
                return fmt::format("vec4(({}).base_color, 1.0)", states_[index].value_code);
            default:
                break;
        }
        const std::string value = fmt::format("({})", states_[index].value_code);
        switch(get_material_value_width(result_.nodes[index].type))
        {
            case 1:
                return fmt::format("vec4(vec3_splat({}), 1.0)", value);
            case 2:
                return fmt::format("vec4({}, 0.0, 1.0)", value);
            case 3:
                return fmt::format("vec4({}, 1.0)", value);
            default:
                return fmt::format("vec4({}.xyz, 1.0)", value);
        }
    }

    void assemble_preview_shader()
    {
        std::vector<std::string> lines;
        const std::string source = options_.source_name.empty() ? std::string{} : fmt::format(" {}", make_comment_text(options_.source_name));
        lines.push_back(fmt::format("// Preview of the material graph{}. Generated for the editor's node swatches.", source));
        add_lines(lines, PREVIEW_INTERFACE);
        add_defines(lines);
        for(const auto* include : SHADER_HEADER_INCLUDES)
        {
            lines.push_back(make_include_line(include));
        }
        if(!is_surface_domain() || result_.scene_textures != 0)
        {
            lines.push_back(make_include_line(PREVIEW_VIEW_INCLUDE));
        }
        lines.emplace_back();
        add_declarations(lines);
        lines.insert(lines.end(), functions_.begin(), functions_.end());
        add_lines(lines, PREVIEW_SELECTION);
        lines.emplace_back();
        lines.push_back("vec4 evaluate_preview(MaterialInputs i, float node)");
        lines.push_back("{");
        lines.push_back(fmt::format("{}vec4 mg_preview = vec4(0.0, 0.0, 0.0, 1.0);", INDENT));
        lines.insert(lines.end(), body_.begin(), body_.end());
        lines.push_back(fmt::format("{}return mg_preview;", INDENT));
        lines.push_back("}");
        lines.emplace_back();
        lines.push_back(make_include_line(PREVIEW_MAIN_INCLUDE));
        for(const auto& line : lines)
        {
            result_.shader += line;
            result_.shader += '\n';
        }
    }

    /// The shader of a decal or post-process material: its domain's interface, header and main around the code.
    void assemble_domain_shader()
    {
        const bool is_decal = result_.domain == material_domain::decal;
        std::vector<std::string> lines;
        const std::string source = options_.source_name.empty() ? std::string{} : fmt::format(" from {}", make_comment_text(options_.source_name));
        lines.push_back(fmt::format("// Generated by the material graph compiler{}. Edit the graph, not this file.", source));
        add_lines(lines, is_decal ? DECAL_SHADER_INTERFACE : POST_PROCESS_SHADER_INTERFACE);
        lines.emplace_back();
        lines.emplace_back("[code]");
        if(is_decal)
        {
            // A decal's instances carry the values of the first parameter registers (vs_decal.eshader).
            lines.emplace_back("#define MATERIAL_GRAPH_INSTANCE_MATERIAL 1");
        }
        else if(result_.post_location == material_post_process_location::after_tonemapping)
        {
            lines.emplace_back("#define POST_PROCESS_DISPLAY 1");
        }
        add_defines(lines);
        for(const auto* include : SHADER_HEADER_INCLUDES)
        {
            lines.push_back(make_include_line(include));
        }
        lines.push_back(make_include_line(is_decal ? DECAL_HEADER_INCLUDE : POST_PROCESS_HEADER_INCLUDE));
        lines.emplace_back();
        add_declarations(lines);
        lines.insert(lines.end(), functions_.begin(), functions_.end());
        lines.push_back("void evaluate_material(MaterialInputs i, inout MaterialSurface s)");
        lines.push_back("{");
        const size_t body_start = lines.size();
        lines.insert(lines.end(), body_.begin(), body_.end());
        lines.push_back("}");
        lines.emplace_back();
        lines.push_back(make_include_line(is_decal ? DECAL_MAIN_INCLUDE : POST_PROCESS_MAIN_INCLUDE));
        for(const auto& range : node_lines_)
        {
            auto& info = get_info(range.node);
            info.first_line = uint32_t(body_start + range.first + 1);
            info.line_count = uint32_t(range.count);
        }
        for(const auto& line : lines)
        {
            result_.shader += line;
            result_.shader += '\n';
        }
    }

    void assemble_shader()
    {
        if(!is_surface_domain())
        {
            assemble_domain_shader();
            return;
        }
        std::vector<std::string> lines;
        const std::string source = options_.source_name.empty() ? std::string{} : fmt::format(" from {}", make_comment_text(options_.source_name));
        lines.push_back(fmt::format("// Generated by the material graph compiler{}. Edit the graph, not this file.", source));
        const bool is_forward = result_.is_forward();
        add_lines(lines, is_forward ? FORWARD_SHADER_VARIANTS : SHADER_VARIANTS);
        if(is_forward && result_.order_independent)
        {
            add_lines(lines, FORWARD_OIT_VARIANT);
        }
        result_.has_gi_variant = is_forward && fits_gi_variant();
        if(result_.has_gi_variant)
        {
            add_lines(lines, FORWARD_GI_VARIANT);
            if(result_.order_independent)
            {
                add_lines(lines, FORWARD_OIT_GI_VARIANT);
            }
        }
        const bool has_shadow_casters =
            is_forward ? result_.casts_forward_shadow : (result_.uses_opacity_mask || result_.uses_world_position_offset);
        if(has_shadow_casters)
        {
            add_lines(lines, SHADER_SHADOW_VARIANTS);
        }
        lines.emplace_back();
        add_lines(lines, SHADER_INPUTS);
        add_vertex_output_lines(lines, false);
        lines.emplace_back();
        lines.emplace_back("[code]");
        add_defines(lines);
        for(const auto* include : SHADER_HEADER_INCLUDES)
        {
            lines.push_back(make_include_line(include));
        }
        if(is_forward)
        {
            lines.push_back(make_include_line(FORWARD_HEADER_INCLUDE));
        }
        lines.emplace_back();
        add_declarations(lines);
        lines.insert(lines.end(), functions_.begin(), functions_.end());
        lines.push_back("void evaluate_material(MaterialInputs i, inout MaterialSurface s)");
        lines.push_back("{");
        const size_t body_start = lines.size();
        lines.insert(lines.end(), body_.begin(), body_.end());
        lines.push_back("}");
        lines.emplace_back();
        if(is_forward && has_shadow_casters)
        {
            lines.push_back("#if SHADOW_CASTER");
            lines.push_back(make_include_line(SHADER_SHADOW_MAIN_INCLUDE));
            lines.push_back("#else");
            lines.push_back(make_include_line(FORWARD_MAIN_INCLUDE));
            lines.push_back("#endif");
        }
        else if(is_forward)
        {
            lines.push_back(make_include_line(FORWARD_MAIN_INCLUDE));
        }
        else if(has_shadow_casters)
        {
            lines.push_back("#if SHADOW_CASTER");
            lines.push_back(make_include_line(SHADER_SHADOW_MAIN_INCLUDE));
            lines.push_back("#else");
            lines.push_back(make_include_line(SHADER_MAIN_INCLUDE));
            lines.push_back("#endif");
        }
        else
        {
            lines.push_back(make_include_line(SHADER_MAIN_INCLUDE));
        }
        for(const auto& range : node_lines_)
        {
            auto& info = get_info(range.node);
            info.first_line = uint32_t(body_start + range.first + 1);
            info.line_count = uint32_t(range.count);
        }
        for(const auto& line : lines)
        {
            result_.shader += line;
            result_.shader += '\n';
        }
    }

    /// The varyings only the graph's own vertex shader writes and its pixels read: the object's transform and the
    /// Vertex Interpolators. @p is_vertex: as the vertex shader's outputs, with defaults.
    void add_vertex_output_lines(std::vector<std::string>& lines, bool is_vertex) const
    {
        if(result_.reads_object_data)
        {
            lines.emplace_back(is_vertex ? "flat vec4 v_object0 = vec4(1.0, 0.0, 0.0, 0.0)" : "flat vec4 v_object0");
            lines.emplace_back(is_vertex ? "flat vec4 v_object1 = vec4(0.0, 1.0, 0.0, 0.0)" : "flat vec4 v_object1");
            lines.emplace_back(is_vertex ? "flat vec4 v_object2 = vec4(0.0, 0.0, 1.0, 0.0)" : "flat vec4 v_object2");
        }
        for(uint32_t slot = 0; slot < result_.interpolator_count; ++slot)
        {
            lines.push_back(fmt::format("vec4 v_interpolator{}", slot));
        }
    }

    /// The vertex shader of a graph with a vertex stage (emit_vertex); none for another.
    void assemble_vertex_shader()
    {
        if(!result_.uses_world_position_offset && !result_.reads_vertex_outputs())
        {
            return;
        }
        std::vector<std::string> lines;
        const std::string source = options_.source_name.empty() ? std::string{} : fmt::format(" from {}", make_comment_text(options_.source_name));
        lines.push_back(fmt::format("// Generated by the material graph compiler{}. Edit the graph, not this file. The vertex shader: its World "
                                    "Position Offset.",
                                    source));
        add_lines(lines, VERTEX_SHADER_VARIANTS);
        lines.emplace_back();
        add_lines(lines, VERTEX_SHADER_INTERFACE);
        add_vertex_output_lines(lines, true);
        lines.emplace_back("#endif");
        lines.emplace_back();
        lines.emplace_back("[code]");
        add_defines(lines);
        lines.push_back(make_include_line(VERTEX_HEADER_INCLUDES[0]));
        lines.emplace_back("#if SKINNED || SKINNED_INSTANCED || SKINNED_BAKED_INSTANCED");
        lines.push_back(make_include_line(VERTEX_SKINNING_INCLUDE));
        lines.emplace_back("#endif");
        for(size_t i = 1; i < VERTEX_HEADER_INCLUDES.size(); ++i)
        {
            lines.push_back(make_include_line(VERTEX_HEADER_INCLUDES[i]));
        }
        lines.emplace_back();
        add_declarations(lines);
        lines.insert(lines.end(), vertex_functions_.begin(), vertex_functions_.end());
        lines.push_back("void evaluate_vertex(MaterialInputs i, inout MaterialVertex s)");
        lines.push_back("{");
        lines.insert(lines.end(), vertex_body_.begin(), vertex_body_.end());
        lines.push_back("}");
        lines.emplace_back();
        // vs_material_graph.sh holds the position-only main itself.
        lines.push_back(make_include_line(VERTEX_OFFSET_INCLUDE));
        lines.push_back("#if VELOCITY");
        lines.push_back(make_include_line(VERTEX_VELOCITY_MAIN_INCLUDE));
        lines.push_back("#elif !POSITION_ONLY");
        lines.push_back(make_include_line(VERTEX_MAIN_INCLUDE));
        lines.push_back("#endif");
        for(const auto& line : lines)
        {
            result_.vertex_shader += line;
            result_.vertex_shader += '\n';
        }
    }

    // -----------------------------------------------------------------------------------------
    // Material functions
    // -----------------------------------------------------------------------------------------

    /// Drops what the graph links into hidden inputs, then puts the nodes of each called material function in the
    /// call's place, after the graph's own nodes, which keep their indices.
    void expand_function_calls()
    {
        authored_count_ = flat_.nodes.size();
        for(auto& node : flat_.nodes)
        {
            drop_hidden_links(node);
        }
        std::vector<std::string> stack;
        for(size_t i = 0; i < authored_count_; ++i)
        {
            expand_call(i, stack);
        }
    }

    void drop_hidden_links(material_graph_node& node)
    {
        const auto* def = find_material_node_def(node.type);
        if(def == nullptr)
        {
            return;
        }
        for(auto& entry : node.inputs)
        {
            const auto* pin = find_material_pin_def(def->inputs, parse_material_graph_pin(entry.pin).pin);
            if(pin != nullptr && pin->is_hidden && !entry.link.empty())
            {
                report(material_graph_severity::error, node.id, entry.pin, "This input takes no link: the compiler fills it.");
                entry.link.clear();
            }
        }
    }

    auto resolve_function(const std::string& key) const -> std::shared_ptr<const material_graph>
    {
        return options_.resolve_function ? options_.resolve_function(key) : load_material_function(key);
    }

    /// Puts the nodes of the function node @p index calls in the graph, wired to its pins; then the calls among them.
    void expand_call(size_t index, std::vector<std::string>& stack)
    {
        if(flat_.nodes[index].type != MATERIAL_FUNCTION_CALL_TYPE)
        {
            return;
        }
        const std::string call_id = flat_.nodes[index].id;
        const auto* setting = find_material_graph_setting(flat_.nodes[index], MATERIAL_FUNCTION_KEY_SETTING);
        const std::string key = setting != nullptr ? *setting : std::string{};
        const auto function = key.empty() ? nullptr : resolve_function(key);
        const auto problem = find_call_problem(key, function.get(), stack);
        if(!problem.empty())
        {
            report(material_graph_severity::error, call_id, MATERIAL_FUNCTION_KEY_SETTING, fmt::format("{} Its outputs read 0.", problem));
            return;
        }
        if(std::find(result_.functions.begin(), result_.functions.end(), key) == result_.functions.end())
        {
            result_.functions.push_back(key);
        }
        const auto function_pins = get_material_function_interface(*function);
        const std::string prefix = call_id + FUNCTION_NODE_SEPARATOR;
        const size_t first = flat_.nodes.size();
        for(const auto& node : function->nodes)
        {
            material_graph_node copy = node;
            copy.id = prefix + node.id;
            for(auto& entry : copy.inputs)
            {
                entry.link = entry.link.empty() ? entry.link : prefix + entry.link;
            }
            scope_reroute_name(copy, prefix);
            drop_hidden_links(copy);
            function_calls_[copy.id] = function_node_origin{owner_of(call_id), key, node.id};
            flat_.nodes.push_back(std::move(copy));
        }
        for(uint32_t k = 0; k < uint32_t(function_pins.inputs.size()); ++k)
        {
            bind_function_input(index, k, size_t(find_material_graph_node(flat_, prefix + function_pins.inputs[k].node_id)));
        }
        for(uint32_t k = 0; k < uint32_t(function_pins.outputs.size()); ++k)
        {
            auto& result = get_or_add_material_graph_input(flat_.nodes[index], get_material_function_result_pin(k), {});
            result.link = make_material_graph_link(prefix + function_pins.outputs[k].node_id, "out");
        }
        stack.push_back(key);
        const size_t end = flat_.nodes.size();
        for(size_t i = first; i < end; ++i)
        {
            expand_call(i, stack);
        }
        stack.pop_back();
    }

    /// A function's Named Reroutes are its own: each call's copies take names of their own.
    static void scope_reroute_name(material_graph_node& node, const std::string& prefix)
    {
        const auto* def = find_material_node_def(node.type);
        const bool is_reroute = def != nullptr && (def->code == material_node_code::named_reroute_declaration ||
                                                   def->code == material_node_code::named_reroute);
        const auto* name = find_material_graph_setting(node, REROUTE_NAME_SETTING);
        if(is_reroute && name != nullptr && !name->empty())
        {
            set_material_graph_setting(node, REROUTE_NAME_SETTING, prefix + *name);
        }
    }

    /// Why a call of @p key cannot be put in place; empty when it can.
    static auto find_call_problem(const std::string& key, const material_graph* function, const std::vector<std::string>& stack)
        -> std::string
    {
        if(key.empty())
        {
            return "Names no material function.";
        }
        if(std::find(stack.begin(), stack.end(), key) != stack.end())
        {
            return fmt::format("The function {} calls itself.", key);
        }
        if(stack.size() >= MAX_FUNCTION_DEPTH)
        {
            return fmt::format("Functions call functions {} deep here.", MAX_FUNCTION_DEPTH);
        }
        if(function == nullptr)
        {
            return fmt::format("No material function {}.", key);
        }
        if(!is_material_function_graph(*function))
        {
            return fmt::format("{} is not a material function: it has no Function Output, or a Surface Output.", key);
        }
        const auto function_pins = get_material_function_interface(*function);
        if(function_pins.inputs.size() > MATERIAL_FUNCTION_MAX_INPUTS || function_pins.outputs.size() > MATERIAL_FUNCTION_MAX_OUTPUTS)
        {
            return fmt::format("{} has more than {} inputs or {} outputs.", key, MATERIAL_FUNCTION_MAX_INPUTS, MATERIAL_FUNCTION_MAX_OUTPUTS);
        }
        return {};
    }

    /// Gives the copy of a function's input @p k what the call's pin in<k> reads: its link, or its value as the
    /// input's default.
    void bind_function_input(size_t call, uint32_t k, size_t input)
    {
        const auto* entry = find_material_graph_input(flat_.nodes[call], get_material_function_input_pin(k));
        if(entry == nullptr)
        {
            return;
        }
        const material_graph_input caller = *entry;
        auto& target = flat_.nodes[input];
        if(caller.link.empty())
        {
            // A call's pin holds one number, as an unlinked dynamic input does: every component takes it.
            get_or_add_material_graph_input(target, "default", {}).value = math::vec4(caller.value.x);
            return;
        }
        get_or_add_material_graph_input(target, "in", {}).link = caller.link;
    }

    /// The node of the graph's own a node belongs to: itself, or the call that put it in the graph.
    auto owner_of(const std::string& id) const -> std::string
    {
        const auto found = function_calls_.find(id);
        return found == function_calls_.end() ? id : found->second.call;
    }

    /// Problems of the nodes calls put in the graph are told on the call, naming the function and the node.
    void map_function_diagnostics()
    {
        for(auto& diagnostic : result_.diagnostics)
        {
            const auto found = function_calls_.find(diagnostic.node_id);
            if(found == function_calls_.end())
            {
                continue;
            }
            diagnostic.message = fmt::format("In {}, {}: {}", found->second.function, found->second.node, diagnostic.message);
            diagnostic.node_id = found->second.call;
            diagnostic.pin.clear();
        }
    }

    struct line_range
    {
        size_t node{};
        size_t first{};
        size_t count{};
    };

    /// Where a node a call put in the graph comes from: the graph's own call and the function's node.
    struct function_node_origin
    {
        std::string call;
        std::string function;
        std::string node;
    };

    /// The graph with the nodes of the functions it calls: what is compiled.
    material_graph flat_;
    const material_graph& graph_;
    /// The graph's own nodes, the first of flat_.
    size_t authored_count_{};
    std::map<std::string, function_node_origin> function_calls_;
    const material_graph_compile_options& options_;
    material_graph_compile_result& result_;
    std::vector<node_state> states_;
    int output_{-1};
    std::map<size_t, std::string> parameter_names_;
    std::map<size_t, size_t> texture_indices_;
    /// The render_change aspects each node feeds (collect_node_aspects).
    std::vector<render_change::mask> node_aspects_;
    std::vector<std::string> body_;
    std::vector<line_range> node_lines_;
    std::vector<std::string> defines_;
    /// The functions of Custom nodes, written before the graph's own.
    std::vector<std::string> functions_;
    /// The vertex shader's code and Custom functions (emit_vertex).
    std::vector<std::string> vertex_body_;
    std::vector<std::string> vertex_functions_;
    /// The Vertex Interpolators the pixel stage reads, by node, and the varying each takes.
    std::map<size_t, uint32_t> interpolator_slots_;
    /// emit_vertex is emitting: Vertex Interpolators give their input itself.
    bool is_emitting_vertex_{};
    /// A decal texture's UV function is being emitted (emit_decal_uv_function): its reads use the screen's gradients.
    bool is_emitting_decal_uv_{};
};

/// Identifies a problem across two compiles of a graph.
auto get_problem_key(const material_graph_diagnostic& diagnostic) -> std::string
{
    return fmt::format("{}|{}|{}", diagnostic.node_id, diagnostic.pin, diagnostic.message);
}

/// The first error the graph gets from linking @p link into entry @p pin of node @p node that it does not have now;
/// empty when it gets none. A trial compile finds what a type check of the two pins misses: a dynamic node widened by
/// the link into a vector that then goes into a float further on.
auto find_new_link_error(const material_graph& graph, size_t node, hpp::string_view pin, const std::string& link) -> std::string
{
    material_graph trial = graph;
    auto& entry = get_or_add_material_graph_input(trial.nodes[node], pin, {});
    entry.link = link;
    std::set<std::string> known;
    for(const auto& diagnostic : compile_material_graph(graph).diagnostics)
    {
        known.insert(get_problem_key(diagnostic));
    }
    for(const auto& diagnostic : compile_material_graph(trial).diagnostics)
    {
        if(diagnostic.severity == material_graph_severity::error && known.count(get_problem_key(diagnostic)) == 0)
        {
            return diagnostic.message;
        }
    }
    return {};
}

/// True when the node with index @p from reads, through its links, the node with id @p target.
auto reads_node(const material_graph& graph, size_t from, hpp::string_view target, std::vector<bool>& visited) -> bool
{
    if(visited[from])
    {
        return false;
    }
    visited[from] = true;
    if(graph.nodes[from].id == target)
    {
        return true;
    }
    for(const auto& input : graph.nodes[from].inputs)
    {
        if(input.link.empty())
        {
            continue;
        }
        const int source = find_material_graph_node(graph, parse_material_graph_link(input.link).node);
        if(source >= 0 && reads_node(graph, size_t(source), target, visited))
        {
            return true;
        }
    }
    return false;
}
} // namespace

auto material_graph_compile_result::reads_vertex_outputs() const -> bool
{
    return reads_object_data || interpolator_count > 0;
}

auto material_graph_compile_result::computes_vertex_values() const -> bool
{
    return uses_world_position_offset || interpolator_count > 0;
}

auto material_graph_compile_result::is_translucent() const -> bool
{
    return blend_mode != material_blend_mode::opaque;
}

auto material_graph_compile_result::is_forward() const -> bool
{
    return is_material_blend_mode_forward(blend_mode);
}

auto material_graph_compile_result::has_errors() const -> bool
{
    return std::any_of(diagnostics.begin(),
                       diagnostics.end(),
                       [](const material_graph_diagnostic& diagnostic)
                       {
                           return diagnostic.severity == material_graph_severity::error;
                       });
}

auto material_graph_compile_result::get_node_diagnostics(hpp::string_view node_id) const -> std::vector<const material_graph_diagnostic*>
{
    std::vector<const material_graph_diagnostic*> found;
    for(const auto& diagnostic : diagnostics)
    {
        if(diagnostic.node_id == node_id)
        {
            found.push_back(&diagnostic);
        }
    }
    std::stable_sort(found.begin(),
                     found.end(),
                     [](const material_graph_diagnostic* a, const material_graph_diagnostic* b)
                     {
                         return a->severity > b->severity;
                     });
    return found;
}

auto make_material_graph_parameter_registers(const material_graph_compile_result& result) -> std::vector<math::vec4>
{
    return make_material_graph_parameter_registers(make_material_graph_binding(result));
}

auto make_material_graph_binding(const material_graph_compile_result& result) -> material_graph_binding
{
    material_graph_binding binding;
    binding.parameters = result.parameters;
    binding.parameter_registers = result.parameter_registers;
    binding.textures = result.textures;
    binding.static_switches = result.static_switches;
    binding.emissive = result.emissive;
    binding.uses_opacity_mask = result.uses_opacity_mask;
    binding.is_opacity_mask_animated = result.is_opacity_mask_animated;
    binding.blend_mode = result.blend_mode;
    binding.shading_model = result.shading_model;
    binding.preserve_specular = result.preserve_specular;
    binding.taa_response = result.taa_response;
    binding.order_independent = result.order_independent;
    binding.has_gi_variant = result.has_gi_variant;
    binding.depth_prepass = result.depth_prepass;
    binding.writes_velocity = result.writes_velocity;
    binding.casts_forward_shadow = result.casts_forward_shadow;
    binding.uses_world_position_offset = result.uses_world_position_offset;
    binding.is_world_position_offset_animated = result.is_world_position_offset_animated;
    binding.max_world_position_offset = result.max_world_position_offset;
    binding.reads_vertex_outputs = result.reads_vertex_outputs();
    binding.computes_vertex_values = result.computes_vertex_values();
    binding.domain = result.domain;
    binding.decal_writes = result.decal_writes;
    binding.decal_reads_surface = result.decal_reads_surface;
    binding.post_location = result.post_location;
    binding.post_priority = result.post_priority;
    binding.scene_textures = result.scene_textures;
    binding.samples_scene_neighbours = result.samples_scene_neighbours;
    return binding;
}

auto get_material_graph_max_textures(material_blend_mode mode, material_shading_model model, uint32_t scene_textures) -> uint32_t
{
    if(!is_material_blend_mode_forward(mode))
    {
        return MATERIAL_GRAPH_MAX_TEXTURES;
    }
    const uint32_t limit = model == material_shading_model::unlit ? MATERIAL_GRAPH_MAX_FORWARD_UNLIT_TEXTURES : MATERIAL_GRAPH_MAX_FORWARD_LIT_TEXTURES;
    return limit - count_forward_scene_reads(scene_textures);
}

auto count_forward_scene_reads(uint32_t scene_textures) -> uint32_t
{
    return ((scene_textures & material_scene_texture_color) != 0 ? 1u : 0u) +
           ((scene_textures & material_scene_texture_depth) != 0 ? 1u : 0u) +
           ((scene_textures & material_scene_texture_blurred_color) != 0 ? 1u : 0u);
}

auto get_forward_scene_texture_stages(bool is_lit, uint32_t scene_textures, bool is_gi) -> forward_scene_texture_stages
{
    const bool reads_color = (scene_textures & material_scene_texture_color) != 0;
    const bool reads_depth = (scene_textures & material_scene_texture_depth) != 0;
    // The stage just below the engine's, then the next free ones down (stage 7 is the skinning palette's, stage 8 a lit
    // surface's shadow atlas). The blurred copy only comes with both reads (a rough Refraction).
    // The "gi" variant keeps stages 5 and 6 for the radiance cache: its reads take the three below.
    const uint8_t gi_shift =
        is_lit && is_gi ? uint8_t(MATERIAL_GRAPH_MAX_FORWARD_LIT_TEXTURES - MATERIAL_GRAPH_MAX_FORWARD_GI_TEXTURES) : uint8_t(0);
    const uint8_t first = uint8_t((is_lit ? 6 : 13) - gi_shift);
    const uint8_t second = uint8_t((is_lit ? 5 : 12) - gi_shift);
    const uint8_t third = uint8_t((is_lit ? 4 : 11) - gi_shift);
    forward_scene_texture_stages stages;
    stages.color = first;
    stages.depth = reads_color && reads_depth ? second : first;
    stages.blurred_color = third;
    return stages;
}

auto get_material_graph_max_textures(material_domain domain) -> uint32_t
{
    switch(domain)
    {
        case material_domain::decal:
            return MATERIAL_GRAPH_MAX_DECAL_TEXTURES;
        case material_domain::post_process:
            return MATERIAL_GRAPH_MAX_POST_PROCESS_TEXTURES;
        case material_domain::surface:
            break;
    }
    return MATERIAL_GRAPH_MAX_TEXTURES;
}

auto make_material_graph_parameter_registers(const material_graph_binding& binding) -> std::vector<math::vec4>
{
    std::vector<math::vec4> registers(binding.parameter_registers, math::vec4(0.0f));
    for(const auto& parameter : binding.parameters)
    {
        auto& value = registers[parameter.register_index];
        if(get_material_value_width(parameter.type) == 1)
        {
            value[int(parameter.component)] = parameter.default_value.x;
        }
        else
        {
            value = parameter.default_value;
        }
    }
    return registers;
}

auto has_material_node_preview(const material_node_def& def) -> bool
{
    // A pass-through shows its input's value: the node it reads has the swatch.
    const bool is_reroute = def.code == material_node_code::reroute || def.code == material_node_code::named_reroute_declaration ||
                            def.code == material_node_code::named_reroute || def.code == material_node_code::function_output;
    return def.code != material_node_code::surface_output && !is_reroute && !def.outputs.empty();
}

auto has_material_graph_preview(const material_graph& graph, const material_graph_compile_result& result, size_t index) -> bool
{
    if(index >= graph.nodes.size() || index >= result.nodes.size())
    {
        return false;
    }
    const auto* def = find_material_node_def(graph.nodes[index].type);
    return def != nullptr && has_material_node_preview(*def);
}

auto get_material_graph_shader_includes() -> std::vector<std::string>
{
    std::vector<std::string> includes(SHADER_HEADER_INCLUDES.begin(), SHADER_HEADER_INCLUDES.end());
    includes.emplace_back(SHADER_MAIN_INCLUDE);
    includes.emplace_back(SHADER_SHADOW_MAIN_INCLUDE);
    includes.emplace_back(VERTEX_HEADER_INCLUDES[1]);
    includes.emplace_back(VERTEX_SKINNING_INCLUDE);
    includes.emplace_back(VERTEX_OFFSET_INCLUDE);
    includes.emplace_back(VERTEX_MAIN_INCLUDE);
    includes.emplace_back(VERTEX_VELOCITY_MAIN_INCLUDE);
    includes.emplace_back(DECAL_HEADER_INCLUDE);
    includes.emplace_back(DECAL_MAIN_INCLUDE);
    includes.emplace_back(POST_PROCESS_HEADER_INCLUDE);
    includes.emplace_back(POST_PROCESS_MAIN_INCLUDE);
    includes.emplace_back("pre_exposure.sh");
    includes.emplace_back(FORWARD_MAIN_INCLUDE);
    includes.emplace_back(FORWARD_HEADER_INCLUDE);
    includes.emplace_back("fog/fog.sh");
    includes.emplace_back("reflection_probe/sphere_probe.sh");
    includes.emplace_back("reflection_probe/box_probe.sh");
    return includes;
}

auto get_material_graph_sampler_name(uint32_t index) -> std::string
{
    return fmt::format("{}{}", SAMPLER_PREFIX, index);
}

auto get_material_graph_texture_stage(uint32_t index) -> uint8_t
{
    return uint8_t(index < SKINNING_PALETTE_STAGE ? index : index + 1);
}

auto compile_material_graph(const material_graph& graph, const material_graph_compile_options& options) -> material_graph_compile_result
{
    material_graph_compile_result result;
    graph_compiler compiler(graph, options, result);
    compiler.run();
    return result;
}

auto check_material_graph_link(const material_graph& graph,
                               hpp::string_view from_node,
                               hpp::string_view from_pin,
                               hpp::string_view to_node,
                               hpp::string_view to_pin) -> std::string
{
    const int from = find_material_graph_node(graph, from_node);
    const int to = find_material_graph_node(graph, to_node);
    if(from < 0 || to < 0)
    {
        return "The node does not exist.";
    }
    if(from == to)
    {
        return "A node cannot read its own output.";
    }
    const auto* from_def = find_material_node_def(graph.nodes[size_t(from)].type);
    const auto* to_def = find_material_node_def(graph.nodes[size_t(to)].type);
    if(!from_def || !to_def)
    {
        return "The node type is unknown.";
    }
    const auto output = parse_material_graph_link(fmt::format("{}.{}", from_node, from_pin));
    const bool has_output = output.pin.empty() ? !from_def->outputs.empty() : find_material_pin_def(from_def->outputs, output.pin) != nullptr;
    const auto input_pin = parse_material_graph_pin(to_pin);
    const auto* input = find_material_pin_def(to_def->inputs, input_pin.pin);
    if(!has_output || !input || (input_pin.pin != to_pin && (input_pin.component < 0 || !can_split_material_pin(*input))))
    {
        return "The pin does not exist.";
    }
    if(input->is_value_only || input->is_hidden)
    {
        return input->label.empty() ? std::string("This input takes no link.") : fmt::format("{} takes no link.", input->label);
    }
    std::vector<bool> visited(graph.nodes.size());
    if(reads_node(graph, size_t(from), to_node, visited))
    {
        return "The link would close a loop.";
    }
    return find_new_link_error(graph, size_t(to), to_pin, make_material_graph_link(from_node, output.pin, output.components));
}

} // namespace unravel
