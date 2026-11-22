#include "standard_material.h"
#include "graph_material.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace unravel
{

namespace
{
constexpr const char* PARAMETER_VALUE_PIN = "value";
constexpr const char* UV_PIN = "uv";
/// The node every map is read through: the mesh's UV at the material's tiling.
constexpr const char* TILED_UV_ID = "uv";
constexpr const char* PACKING_SWITCH_ID = "packed_metal_roughness";

/// The PBR inspector's sliders (standard_material.cpp of the PBR material), which the parameters keep.
constexpr float UNIT_SLIDER_MAX = 1.0f;
constexpr float BUMPINESS_SLIDER_MAX = 10.0f;
constexpr float EMISSIVE_INTENSITY_SLIDER_MAX = 100.0f;

/// The Opacity Mask Clip of an opaque surface whose base color's alpha is below 1: the alpha at which the mask dithers
/// as the PBR material dithered it (fs_material_graph_gbuffer.sh's TRANSLUCENT_DITHER_CLIP), where alpha 1 draws every
/// pixel and alpha 0 none.
constexpr float DITHERED_ALPHA_CLIP = 0.25f;

/// The dielectric reflectance the specular solve is calibrated for, and its guard against a division by zero.
constexpr float DIELECTRIC_F0 = 0.04f;
constexpr float SOLVE_EPSILON = 1e-6f;

/// The rows the inspector shows each map's hint in, as the PBR inspector did.
constexpr const char* ROUGHNESS_MAP_HINT = "Roughness in red, or in green with Packed Metal Roughness (one map holding "
                                           "both, glTF 2.0).";
constexpr const char* METALLIC_MAP_HINT = "Metallic in red, or in blue with Packed Metal Roughness (one map holding "
                                          "both, glTF 2.0).";
constexpr const char* PACKING_SWITCH_HINT = "The Roughness Map holds roughness in green and the Metallic Map the "
                                            "metallic value in blue, as one map packing both does (glTF 2.0); off, each "
                                            "in red.";
constexpr const char* SPECULAR_MAP_HINT = "Specular color in rgb, glossiness in alpha.";
constexpr const char* AO_MAP_HINT = "Ambient occlusion in red: white is unoccluded.";

/// Adds a node of @p type named @p id. Set it up before the next node is added, which may move it.
auto add_graph_node(material_graph& graph, const std::string& id, const char* type) -> material_graph_node&
{
    auto& node = graph.nodes.emplace_back();
    node.id = id;
    node.type = type;
    return node;
}

void link_input(material_graph_node& node, const char* pin, const std::string& source)
{
    get_or_add_material_graph_input(node, pin, {}).link = source;
}

void set_input_value(material_graph_node& node, const char* pin, const math::vec4& value)
{
    get_or_add_material_graph_input(node, pin, {}).value = value;
}

auto get_output(material_graph& graph) -> material_graph_node&
{
    return graph.nodes[size_t(find_material_graph_output(graph))];
}

/// Links @p source into channel @p pin of the graph's Surface Output.
void link_output(material_graph& graph, const char* pin, const std::string& source)
{
    link_input(get_output(graph), pin, source);
}

/// Where a parameter's register goes: among the first, which an instanced draw reads per instance, or after them.
enum class register_use
{
    per_instance,
    shared,
};

/// A parameter named @p name, set to @p value while nothing sets it.
auto add_parameter(material_graph& graph,
                   const std::string& id,
                   const char* type,
                   const char* name,
                   const math::vec4& value,
                   register_use use = register_use::per_instance) -> material_graph_node&
{
    auto& node = add_graph_node(graph, id, type);
    set_material_graph_setting(node, MATERIAL_PARAMETER_NAME_SETTING, name);
    if(use == register_use::shared)
    {
        set_material_graph_setting(node, MATERIAL_PARAMETER_PER_INSTANCE_SETTING, MATERIAL_PARAMETER_PER_INSTANCE_OFF);
    }
    set_input_value(node, PARAMETER_VALUE_PIN, value);
    return node;
}

void add_color_parameter(material_graph& graph,
                         const std::string& id,
                         const char* name,
                         const math::color& color,
                         register_use use = register_use::per_instance)
{
    add_parameter(graph, id, "color_parameter", name, color.to_linear().value, use);
}

/// A scalar parameter; edited with a slider from 0 to @p slider_max, as the PBR inspector edits it, when that is above 0.
void add_scalar_parameter(material_graph& graph,
                          const std::string& id,
                          const char* name,
                          float value,
                          float slider_max,
                          register_use use = register_use::per_instance)
{
    auto& node = add_parameter(graph, id, "scalar_parameter", name, math::vec4(value), use);
    if(slider_max > 0.0f)
    {
        set_material_graph_setting(node, MATERIAL_PARAMETER_SLIDER_MAX_SETTING, std::to_string(slider_max));
    }
}

/// A named Texture Sample of @p texture at the tiled UV. An empty one reads the default texture, as the PBR draw bound.
void add_map(material_graph& graph, const std::string& id, const char* name, const asset_handle<gfx::texture>& texture, const char* hint = nullptr)
{
    auto& node = add_graph_node(graph, id, "texture_sample");
    node.texture = texture;
    set_material_graph_setting(node, MATERIAL_PARAMETER_NAME_SETTING, name);
    if(hint != nullptr)
    {
        set_material_graph_setting(node, MATERIAL_PARAMETER_DESCRIPTION_SETTING, hint);
    }
    link_input(node, UV_PIN, TILED_UV_ID);
}

/// @p a times @p b; the product's id.
auto add_multiply(material_graph& graph, const std::string& id, const std::string& a, const std::string& b) -> std::string
{
    auto& node = add_graph_node(graph, id, "multiply");
    link_input(node, "a", a);
    link_input(node, "b", b);
    return id;
}

auto has_packed_metal_roughness(const standard_surface& surface) -> bool
{
    return surface.roughness_map && surface.roughness_map == surface.metallic_map;
}

/// The parameters, in the order the PBR inspector showed them: what material instances override per entity first.
void add_parameters(material_graph& graph, const standard_surface& surface)
{
    add_color_parameter(graph, "base_color", "Base Color", surface.base_color);
    add_color_parameter(graph, "subsurface_color", "Subsurface Color", surface.subsurface_color);
    const math::color emissive(math::vec4(math::vec3(surface.emissive_color.value), 1.0f));
    add_color_parameter(graph, "emissive_color", "Emissive Color", emissive);
    add_scalar_parameter(graph, "emissive_intensity", "Emissive Intensity", surface.emissive_intensity, EMISSIVE_INTENSITY_SLIDER_MAX);
    if(surface.workflow == standard_workflow::specular)
    {
        // Four registers hold the per-instance values: the specular color stays the material's, so the intensity,
        // the glossiness and the bumpiness keep the fourth.
        add_color_parameter(graph, "specular_color", "Specular Color", surface.specular_color, register_use::shared);
        add_scalar_parameter(graph, "glossiness", "Glossiness", surface.glossiness, UNIT_SLIDER_MAX);
    }
    else
    {
        add_scalar_parameter(graph, "roughness", "Roughness", surface.roughness, UNIT_SLIDER_MAX);
        add_scalar_parameter(graph, "metallic", "Metallic", surface.metallic, UNIT_SLIDER_MAX);
    }
    add_scalar_parameter(graph, "bumpiness", "Bumpiness", surface.bumpiness, BUMPINESS_SLIDER_MAX);
    if(surface.alpha == alpha_mode::mask)
    {
        add_scalar_parameter(graph, "alpha_cutoff", "Alpha Cutoff", surface.alpha_cutoff, UNIT_SLIDER_MAX, register_use::shared);
    }
    add_parameter(graph, "tiling", "vector_parameter", "Tiling", math::vec4(surface.tiling, 0.0f, 0.0f), register_use::shared);
    add_scalar_parameter(graph, "dither_threshold", "Dither Threshold", surface.camera_fade_distance, 0.0f, register_use::shared);
}

/// Every map, set or not, in the order the PBR inspector showed them, and the UV they are read at.
void add_maps(material_graph& graph, const standard_surface& surface)
{
    auto& uv = add_graph_node(graph, TILED_UV_ID, "tiling_offset");
    link_input(uv, "tiling", "tiling.xyzw.xy");
    add_map(graph, "color_map", "Color Map", surface.color_map);
    auto& normal = add_graph_node(graph, "normal_map", "normal_map");
    normal.texture = surface.normal_map;
    set_material_graph_setting(normal, MATERIAL_PARAMETER_NAME_SETTING, "Normal Map");
    link_input(normal, UV_PIN, TILED_UV_ID);
    link_input(normal, "strength", "bumpiness");
    if(surface.workflow == standard_workflow::specular)
    {
        add_map(graph, "specular_map", "Specular Map", surface.specular_map, SPECULAR_MAP_HINT);
    }
    else
    {
        add_map(graph, "roughness_map", "Roughness Map", surface.roughness_map, ROUGHNESS_MAP_HINT);
        add_map(graph, "metallic_map", "Metallic Map", surface.metallic_map, METALLIC_MAP_HINT);
    }
    add_map(graph, "emissive_map", "Emissive Map", surface.emissive_map);
    add_map(graph, "ao_map", "AO Map", surface.ao_map, AO_MAP_HINT);
}

/// The color map tinted by the base color, and by the vertex colour where the surface multiplies by it: its rgb the base
/// (diffuse) color, its alpha what a masked material clips.
void link_base_color(material_graph& graph, const standard_surface& surface)
{
    if(surface.multiply_vertex_color)
    {
        add_multiply(graph, "tint", "color_map.rgba", "base_color.rgba");
        add_graph_node(graph, "vertex_color", "vertex_color");
        add_multiply(graph, "albedo", "tint.out", "vertex_color.rgba");
    }
    else
    {
        add_multiply(graph, "albedo", "color_map.rgba", "base_color.rgba");
    }
    link_output(graph, "base_color", "albedo.out.rgb");
}

/// Roughness and metallic, each scaled by its map; the PBR shader's branch between one map packing both and a map of
/// each is a Static Switch Parameter.
void link_roughness_and_metallic(material_graph& graph, const standard_surface& surface)
{
    auto& separate = add_graph_node(graph, "roughness_metallic", "append");
    link_input(separate, "a", "roughness_map.r");
    link_input(separate, "b", "metallic_map.r");
    // Both maps are read either way, so both keep their rows; packed, they are one texture read twice.
    auto& packed = add_graph_node(graph, "packed_roughness_metallic", "append");
    link_input(packed, "a", "roughness_map.g");
    link_input(packed, "b", "metallic_map.b");
    auto& packing = add_graph_node(graph, PACKING_SWITCH_ID, "static_switch");
    set_material_graph_setting(packing, MATERIAL_PARAMETER_NAME_SETTING, "Packed Metal Roughness");
    set_material_graph_setting(packing, MATERIAL_PARAMETER_DESCRIPTION_SETTING, PACKING_SWITCH_HINT);
    set_material_graph_setting(packing,
                               MATERIAL_STATIC_SWITCH_VALUE_SETTING,
                               has_packed_metal_roughness(surface) ? MATERIAL_STATIC_SWITCH_TRUE : MATERIAL_STATIC_SWITCH_FALSE);
    link_input(packing, "true", "packed_roughness_metallic");
    link_input(packing, "false", "roughness_metallic");
    link_output(graph, "roughness", add_multiply(graph, "roughness_times_map", "roughness", "packed_metal_roughness.out.x"));
    link_output(graph, "metallic", add_multiply(graph, "metallic_times_map", "metallic", "packed_metal_roughness.out.y"));
}

/// The specular map tinted by the specular color, and its alpha times the glossiness.
void link_specular(material_graph& graph)
{
    link_output(graph, "specular", add_multiply(graph, "specular_times_map", "specular_map.rgb", "specular_color.rgb"));
    link_output(graph, "glossiness", add_multiply(graph, "glossiness_times_map", "specular_map.a", "glossiness"));
}

/// The emissive map tinted by the emissive color times its intensity.
void link_emissive(material_graph& graph)
{
    add_multiply(graph, "emissive_tint", "emissive_color.rgb", "emissive_intensity");
    link_output(graph, "emissive", add_multiply(graph, "emissive", "emissive_map.rgb", "emissive_tint"));
}

/// The albedo's alpha becomes the Opacity Mask: a masked material clips below the Alpha Cutoff parameter; a transparent
/// one (Blend) takes the see-through Blend Mode k_default_translucent_blend_mode names and reads it as its Opacity; an
/// opaque one whose base color's alpha is below 1 dithers at DITHERED_ALPHA_CLIP.
void link_alpha(material_graph& graph, const standard_surface& surface)
{
    switch(surface.alpha)
    {
        case alpha_mode::mask:
            link_output(graph, "opacity_mask", "albedo.out.a");
            link_output(graph, MATERIAL_OPACITY_MASK_CLIP_PIN, "alpha_cutoff");
            break;
        case alpha_mode::blend:
            link_output(graph, "opacity_mask", "albedo.out.a");
            set_material_graph_setting(get_output(graph), MATERIAL_BLEND_MODE_SETTING, get_material_blend_mode_key(k_default_translucent_blend_mode));
            break;
        case alpha_mode::opaque:
            if(surface.base_color.value.w < 1.0f)
            {
                link_output(graph, "opacity_mask", "albedo.out.a");
                set_input_value(get_output(graph), MATERIAL_OPACITY_MASK_CLIP_PIN, math::vec4(DITHERED_ALPHA_CLIP));
            }
            break;
    }
}

/// No culling becomes Two Sided, front-face culling (Clockwise) Cull Front Faces; the graph culls back faces otherwise.
void set_culling(material_graph& graph, const standard_surface& surface)
{
    if(surface.cull == cull_type::none)
    {
        set_material_graph_setting(get_output(graph), MATERIAL_TWO_SIDED_SETTING, MATERIAL_TWO_SIDED_ON);
    }
    else if(surface.cull == cull_type::clockwise)
    {
        set_material_graph_setting(get_output(graph), MATERIAL_CULL_FRONT_FACES_SETTING, MATERIAL_CULL_FRONT_FACES_ON);
    }
}

/// The BT.601 luminance the specular solve weighs colors by.
auto get_perceived_brightness(const math::vec3& color) -> float
{
    return std::sqrt(0.299f * color.x * color.x + 0.587f * color.y * color.y + 0.114f * color.z * color.z);
}

/// The metallic that makes lerp(dielectric F0, base color, metallic) the specular color, with the diffuse identity.
auto solve_metallic(float diffuse, float specular, float one_minus_specular_strength) -> float
{
    if(specular < DIELECTRIC_F0)
    {
        return 0.0f;
    }
    const float a = DIELECTRIC_F0;
    const float b = diffuse * one_minus_specular_strength / (1.0f - DIELECTRIC_F0) + specular - 2.0f * DIELECTRIC_F0;
    const float c = DIELECTRIC_F0 - specular;
    const float discriminant = std::max(b * b - 4.0f * a * c, 0.0f);
    return math::clamp((-b + std::sqrt(discriminant)) / (2.0f * a), 0.0f, 1.0f);
}
} // namespace

auto get_standard_properties() -> const std::vector<standard_property>&
{
    using kind = standard_property::kind;
    static const std::vector<standard_property> properties = {
        {"base_color", "Base Color", kind::color},
        {"subsurface_color", "Subsurface Color", kind::color},
        {"emissive_color", "Emissive Color", kind::color},
        {"emissive_intensity", "Emissive Intensity", kind::scalar},
        {"roughness", "Roughness", kind::scalar},
        {"metallic", "Metallic", kind::scalar},
        {"specular_color", "Specular Color", kind::color},
        {"glossiness", "Glossiness", kind::scalar},
        {"bumpiness", "Bumpiness", kind::scalar},
        {"alpha_cutoff", "Alpha Cutoff", kind::scalar},
        {"tiling", "Tiling", kind::vector2},
        {"dither_threshold", "Dither Threshold", kind::scalar},
        {"color_map", "Color Map", kind::texture},
        {"normal_map", "Normal Map", kind::texture},
        {"roughness_map", "Roughness Map", kind::texture},
        {"metallic_map", "Metallic Map", kind::texture},
        {"specular_map", "Specular Map", kind::texture},
        {"emissive_map", "Emissive Map", kind::texture},
        {"ao_map", "AO Map", kind::texture},
    };
    return properties;
}

auto find_standard_property(hpp::string_view key) -> const standard_property*
{
    const auto& properties = get_standard_properties();
    const auto it = std::find_if(properties.begin(),
                                 properties.end(),
                                 [&key](const standard_property& property)
                                 {
                                     return key == property.key;
                                 });
    return it == properties.end() ? nullptr : &*it;
}

namespace
{
/// The Standard properties a PBR material spelled otherwise: (Standard key, PBR key).
constexpr std::array<std::pair<const char*, const char*>, 2> LEGACY_PBR_KEYS = {{
    {"metallic", "metalness"},
    {"metallic_map", "metalness_map"},
}};
} // namespace

auto get_legacy_pbr_key(const standard_property& property) -> const char*
{
    const auto found = std::find_if(LEGACY_PBR_KEYS.begin(),
                                    LEGACY_PBR_KEYS.end(),
                                    [&property](const auto& keys)
                                    {
                                        return hpp::string_view(keys.first) == property.key;
                                    });
    return found == LEGACY_PBR_KEYS.end() ? property.key : found->second;
}

auto find_legacy_pbr_property(hpp::string_view key) -> const standard_property*
{
    const auto found = std::find_if(LEGACY_PBR_KEYS.begin(),
                                    LEGACY_PBR_KEYS.end(),
                                    [&key](const auto& keys)
                                    {
                                        return key == keys.second;
                                    });
    return find_standard_property(found == LEGACY_PBR_KEYS.end() ? key : hpp::string_view(found->first));
}

auto make_standard_parameter_value(const standard_property& property, const math::vec4& value) -> math::vec4
{
    switch(property.type)
    {
        case standard_property::kind::color:
            return math::color(value).to_linear().value;
        case standard_property::kind::vector2:
            return math::vec4(value.x, value.y, 0.0f, 0.0f);
        case standard_property::kind::scalar:
            return math::vec4(value.x);
        case standard_property::kind::texture:
            break;
    }
    return value;
}

auto make_standard_property_value(const standard_property& property, const math::vec4& parameter) -> math::vec4
{
    switch(property.type)
    {
        case standard_property::kind::color:
            return math::color(parameter).to_srgb().value;
        case standard_property::kind::vector2:
            return math::vec4(parameter.x, parameter.y, 0.0f, 0.0f);
        case standard_property::kind::scalar:
            return math::vec4(parameter.x);
        case standard_property::kind::texture:
            break;
    }
    return parameter;
}

auto resolve_standard_property(const graph_material& mat, const standard_property& property) -> standard_property_target
{
    const bool is_texture = property.type == standard_property::kind::texture;
    const bool is_held = is_texture ? mat.find_texture(property.name) != nullptr : mat.find_parameter(property.name) != nullptr;
    if(is_held)
    {
        return {&property, false};
    }
    const auto* glossiness = hpp::string_view(property.key) == "roughness" ? find_standard_property("glossiness") : nullptr;
    if(glossiness != nullptr && mat.find_parameter(glossiness->name) != nullptr)
    {
        return {glossiness, true};
    }
    return {};
}

auto apply_standard_inversion(const standard_property_target& target, const math::vec4& value) -> math::vec4
{
    return target.is_inverted ? math::vec4(1.0f) - value : value;
}

auto make_standard_material_graph(const standard_surface& surface) -> material_graph
{
    material_graph graph;
    ensure_material_graph_output(graph);
    if(surface.workflow == standard_workflow::specular)
    {
        set_material_graph_setting(get_output(graph), MATERIAL_WORKFLOW_SETTING, MATERIAL_WORKFLOW_SPECULAR);
    }
    add_parameters(graph, surface);
    add_maps(graph, surface);
    link_base_color(graph, surface);
    if(surface.workflow == standard_workflow::specular)
    {
        link_specular(graph);
    }
    else
    {
        link_roughness_and_metallic(graph, surface);
    }
    link_output(graph, "normal", "normal_map");
    link_output(graph, "ambient_occlusion", "ao_map.r");
    if(surface.unlit)
    {
        // Unlit shows the albedo as it is: its Emissive.
        set_material_graph_setting(get_output(graph), MATERIAL_SHADING_MODEL_SETTING, get_material_shading_model_key(material_shading_model::unlit));
        link_output(graph, "emissive", "albedo.out.rgb");
    }
    else
    {
        link_emissive(graph);
    }
    link_output(graph, "subsurface_color", "subsurface_color.rgb");
    link_output(graph, "subsurface_opacity", "subsurface_color.a");
    link_alpha(graph, surface);
    if(surface.refraction != 1.0f)
    {
        set_input_value(get_output(graph), MATERIAL_REFRACTION_PIN, math::vec4(surface.refraction));
    }
    link_output(graph, "camera_fade_distance", "dither_threshold");
    set_culling(graph, surface);
    return graph;
}

auto convert_specular_to_metallic(const math::vec3& diffuse, const math::vec3& specular, float glossiness) -> metallic_surface
{
    const float max_specular = std::max({specular.x, specular.y, specular.z});
    const float metallic = solve_metallic(get_perceived_brightness(diffuse), get_perceived_brightness(specular), 1.0f - max_specular);
    // Base color from the diffuse identity and from the specular one, blended by metallic squared.
    const float denominator = std::max(1.0f - metallic * DIELECTRIC_F0, SOLVE_EPSILON);
    const math::vec3 from_diffuse = diffuse * (1.0f - DIELECTRIC_F0) / denominator;
    const math::vec3 from_specular = specular - math::vec3(DIELECTRIC_F0 * (1.0f - metallic));
    metallic_surface result;
    result.base_color = math::clamp(math::mix(from_diffuse, from_specular, metallic * metallic), math::vec3(0.0f), math::vec3(1.0f));
    result.metallic = metallic;
    result.roughness = math::clamp(1.0f - glossiness, 0.0f, 1.0f);
    return result;
}

auto make_standard_material(const standard_surface& surface) -> std::shared_ptr<graph_material>
{
    auto mat = std::make_shared<graph_material>();
    mat->set_graph(make_standard_material_graph(surface));
    return mat;
}

auto upgrade_legacy_material(std::shared_ptr<material> mat) -> std::shared_ptr<material>
{
    const auto* legacy = mat ? mat->safe_cast<legacy_pbr_material>() : nullptr;
    if(legacy == nullptr)
    {
        return mat;
    }
    return make_standard_material(legacy->surface);
}

} // namespace unravel
