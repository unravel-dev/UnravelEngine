#include "material_graph_library.h"

#include "material_graph.h"

#include <algorithm>
#include <array>

namespace unravel
{

namespace
{
using vt = material_value_type;
using category = material_node_category;

constexpr const char* UV_CODE = "i.uv0";
constexpr const char* TIME_CODE = "i.time";
constexpr const char* VERTEX_NORMAL_CODE = "i.vertex_normal";
constexpr const char* WORLD_POSITION_CODE = "i.world_position";
constexpr float DEFAULT_TRIPLANAR_SHARPNESS = 4.0f;
constexpr float DEFAULT_GREY = 0.8f;
constexpr float DEFAULT_ROUGHNESS = 0.5f;
constexpr float DEFAULT_CHECKER_SCALE = 8.0f;
constexpr float DEFAULT_NOISE_SCALE = 10.0f;
constexpr float DEFAULT_VORONOI_SCALE = 5.0f;
constexpr float DEFAULT_FRESNEL_POWER = 5.0f;
constexpr float DEFAULT_PANNER_SPEED = 0.1f;
constexpr float DEFAULT_OPACITY_MASK_CLIP = 0.5f;
/// How far apart If's A and B may be and still count as equal.
constexpr float DEFAULT_IF_EQUAL_THRESHOLD = 0.00001f;
/// The value above which Branch takes its True input: a 0 / 1 mask from Compare or Step, or a mask read from a texture.
constexpr float BRANCH_CONDITION_THRESHOLD = 0.5f;

auto in(const char* name, const char* label, vt type, const math::vec4& value = {}) -> material_pin_def
{
    material_pin_def pin;
    pin.name = name;
    pin.label = label;
    pin.type = type;
    pin.default_value = value;
    return pin;
}

/// An input reading @p code while unlinked.
auto in_code(const char* name, const char* label, vt type, const char* code) -> material_pin_def
{
    material_pin_def pin = in(name, label, type);
    pin.default_code = code;
    return pin;
}

auto in_color(const char* name, const char* label, vt type, const math::vec4& value) -> material_pin_def
{
    material_pin_def pin = in(name, label, type, value);
    pin.is_color = true;
    return pin;
}

/// @p pin with the editor's tooltip @p tooltip.
auto with_tooltip(material_pin_def pin, const char* tooltip) -> material_pin_def
{
    pin.tooltip = tooltip;
    return pin;
}

/// An input whose value is a fraction, kept between 0 and 1 while edited (metallic, roughness).
auto in_unit(const char* name, const char* label, vt type, const math::vec4& value = {}) -> material_pin_def
{
    material_pin_def pin = in(name, label, type, value);
    pin.max_value = 1.0f;
    return pin;
}

/// A value edited on the node and never linked.
auto in_value(const char* name, const char* label, vt type, const math::vec4& value = {}, bool is_color = false)
    -> material_pin_def
{
    material_pin_def pin = in(name, label, type, value);
    pin.is_value_only = true;
    pin.is_color = is_color;
    return pin;
}

auto out(const char* name, const char* label, vt type, const char* swizzle = "") -> material_pin_def
{
    material_pin_def pin;
    pin.name = name;
    pin.label = label;
    pin.type = type;
    pin.swizzle = swizzle;
    return pin;
}

/// The outputs of a colour: the whole value, its rgb and its alpha.
auto color_outputs() -> std::vector<material_pin_def>
{
    return {out("rgba", "RGBA", vt::float4), out("rgb", "RGB", vt::float3, ".rgb"), out("a", "A", vt::float1, ".a")};
}

/// The outputs of a texture read: the whole texel, its rgb and each channel.
auto texel_outputs() -> std::vector<material_pin_def>
{
    return {out("rgba", "RGBA", vt::float4),
            out("rgb", "RGB", vt::float3, ".rgb"),
            out("r", "R", vt::float1, ".r"),
            out("g", "G", vt::float1, ".g"),
            out("b", "B", vt::float1, ".b"),
            out("a", "A", vt::float1, ".a")};
}

auto make_node(const char* type, const char* label, category group, const char* description) -> material_node_def
{
    material_node_def def;
    def.type = type;
    def.label = label;
    def.category = group;
    def.description = description;
    return def;
}

/// A node whose value is @p expression of fixed type @p value_type.
auto make_fixed(const char* type,
                const char* label,
                category group,
                const char* description,
                std::vector<material_pin_def> inputs,
                std::vector<material_pin_def> outputs,
                vt value_type,
                const char* expression) -> material_node_def
{
    material_node_def def = make_node(type, label, group, description);
    def.inputs = std::move(inputs);
    def.outputs = std::move(outputs);
    def.value_type = value_type;
    def.expression = expression;
    return def;
}

/// A node whose value takes the widest type linked into its dynamic inputs.
auto make_dynamic(const char* type,
                  const char* label,
                  category group,
                  const char* description,
                  std::vector<material_pin_def> inputs,
                  const char* expression) -> material_node_def
{
    material_node_def def = make_node(type, label, group, description);
    def.inputs = std::move(inputs);
    def.outputs = {out("out", "", vt::dynamic)};
    def.type_rule = material_type_rule::dynamic;
    def.value_type = vt::dynamic;
    def.expression = expression;
    return def;
}

/// A math function of one value, applied to each component; an unlinked input reads @p default_value.
auto make_unary(const char* type, const char* label, const char* description, const char* expression, float default_value = 0.0f)
    -> material_node_def
{
    return make_dynamic(type, label, category::math, description, {in("in", "", vt::dynamic, math::vec4(default_value))}, expression);
}

/// A math function of two values, applied to each component.
auto make_binary(const char* type,
                 const char* label,
                 const char* description,
                 const char* expression,
                 float a_default,
                 float b_default) -> material_node_def
{
    return make_dynamic(type,
                        label,
                        category::math,
                        description,
                        {in("a", "A", vt::dynamic, math::vec4(a_default)), in("b", "B", vt::dynamic, math::vec4(b_default))},
                        expression);
}

auto with_shortcut(material_node_def def, char shortcut, const char* keywords = "") -> material_node_def
{
    def.shortcut = shortcut;
    def.keywords = keywords;
    return def;
}

auto with_keywords(material_node_def def, const char* keywords) -> material_node_def
{
    def.keywords = keywords;
    return def;
}

auto as_inline(material_node_def def) -> material_node_def
{
    def.is_inline = true;
    return def;
}

/// @p def as a node only the pixel stage runs: it reads derivatives.
auto as_pixel_only(material_node_def def) -> material_node_def
{
    def.is_pixel_only = true;
    return def;
}

/// @p def in category @p group.
auto in_category(material_node_def def, category group) -> material_node_def
{
    def.category = group;
    return def;
}

/// A choice setting whose values are put into code ({name}), shown by @p labels.
auto make_choice_setting(const char* name,
                         const char* label,
                         std::vector<std::string> choices,
                         std::vector<std::string> labels,
                         const char* default_value,
                         const char* tooltip) -> material_setting_def
{
    material_setting_def setting;
    setting.name = name;
    setting.label = label;
    setting.kind = material_setting_kind::choice;
    setting.choices = std::move(choices);
    setting.choice_labels = std::move(labels);
    setting.default_value = default_value;
    setting.tooltip = tooltip;
    return setting;
}

/// A node of fixed type @p value_type whose dynamic inputs read at their own width (Length, Dot), its code chosen by
/// the width of the first one.
auto make_width_dispatch(const char* type,
                         const char* label,
                         category group,
                         const char* description,
                         std::vector<material_pin_def> inputs,
                         std::vector<material_pin_def> outputs,
                         vt value_type,
                         std::vector<std::string> width_expressions) -> material_node_def
{
    material_node_def def = make_node(type, label, group, description);
    def.inputs = std::move(inputs);
    def.outputs = std::move(outputs);
    def.value_type = value_type;
    def.expression = width_expressions.back();
    def.width_expressions = std::move(width_expressions);
    return def;
}

constexpr const char* PARAMETER_NAME_TOOLTIP = "The name material instances and scripts set the value by, and the "
                                                "row's name in the material's inspector. Empty: the node's id.";
constexpr const char* TEXTURE_NAME_TOOLTIP = "The name material instances and scripts set the texture by, and the row's "
                                             "name in the material's inspector. Empty: the node's id.";
constexpr const char* SWITCH_NAME_TOOLTIP = "Named, the material's inspector shows the switch as a checkbox, and "
                                            "changing it compiles the material again. Empty: it is the graph's alone.";

auto make_parameter_name_setting(const char* tooltip = PARAMETER_NAME_TOOLTIP) -> material_setting_def
{
    material_setting_def setting;
    setting.name = MATERIAL_PARAMETER_NAME_SETTING;
    setting.label = "Parameter Name";
    setting.kind = material_setting_kind::text;
    setting.tooltip = tooltip;
    return setting;
}

/// The hint the material's inspector shows on the row of a parameter, a texture or a named Static Switch.
auto make_description_setting() -> material_setting_def
{
    material_setting_def setting;
    setting.name = MATERIAL_PARAMETER_DESCRIPTION_SETTING;
    setting.label = "Description";
    setting.kind = material_setting_kind::text;
    setting.tooltip = "The hint the material's inspector shows on the row.";
    return setting;
}

/// Where a parameter's value goes: among the registers material instances override without a draw of their own.
auto make_per_instance_setting() -> material_setting_def
{
    material_setting_def setting;
    setting.name = MATERIAL_PARAMETER_PER_INSTANCE_SETTING;
    setting.label = "Per Instance";
    setting.kind = material_setting_kind::choice;
    setting.choices = {"on", MATERIAL_PARAMETER_PER_INSTANCE_OFF};
    setting.choice_labels = {"On", "Off"};
    setting.default_value = "on";
    setting.tooltip = "On: a material instance that overrides the value keeps its model in the material's shared draw, "
                      "as long as the value is among the first four registers (they fill with the parameters that are "
                      "on, vectors first). Off: the value comes after them, and overriding it gives the model a draw "
                      "of its own.";
    return setting;
}

auto make_output_node() -> material_node_def
{
    material_node_def def = make_node(MATERIAL_GRAPH_OUTPUT_TYPE,
                                      "Surface Output",
                                      category::output,
                                      "The material's channels, written to the G-buffer. An unlinked channel takes the "
                                      "value shown on it.");
    def.code = material_node_code::surface_output;
    def.inputs = {
        with_tooltip(in(MATERIAL_ATTRIBUTES_PIN, "Material Attributes", vt::attributes),
                     "A whole surface (Make or Blend Material Attributes): linked, it gives every channel, and only the "
                     "channels linked here besides replace its own. Opacity Mask Clip, Camera Fade Distance and World "
                     "Position Offset stay this node's."),
        in_color("base_color", "Base Color", vt::float3, math::vec4(DEFAULT_GREY, DEFAULT_GREY, DEFAULT_GREY, 1.0f)),
        in_unit(MATERIAL_METALLIC_PIN, "Metallic", vt::float1),
        in_unit(MATERIAL_ROUGHNESS_PIN, "Roughness", vt::float1, math::vec4(DEFAULT_ROUGHNESS)),
        with_tooltip(in_color(MATERIAL_SPECULAR_PIN, "Specular", vt::float3, math::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
                     "The specular workflow's reflectance color (Workflow: Specular). Below a dielectric's 0.04 the "
                     "surface is a dielectric; toward the diffuse color's brightness it turns metallic."),
        with_tooltip(in_unit(MATERIAL_GLOSSINESS_PIN, "Glossiness", vt::float1, math::vec4(1.0f - DEFAULT_ROUGHNESS)),
                     "The specular workflow's smoothness (Workflow: Specular): 1 minus the roughness."),
        with_tooltip(in("normal", "Normal", vt::float3, math::vec4(0.0f, 0.0f, 1.0f, 0.0f)),
                     "Tangent-space normal: (0, 0, 1) is the surface's own normal. A Normal Map node gives one."),
        with_tooltip(in_color("emissive", "Emissive", vt::float3, math::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
                     "Light the surface gives off, in linear colour; values above 1 glow."),
        in_unit("ambient_occlusion", "Ambient Occlusion", vt::float1, math::vec4(1.0f)),
        with_tooltip(in_unit("opacity_mask", "Opacity Mask", vt::float1, math::vec4(1.0f)),
                     "Pixels below Opacity Mask Clip are not drawn (dithered, like a masked material), in its shadow "
                     "neither. Linked or set below 1, the material is masked."),
        with_tooltip(in_unit(MATERIAL_OPACITY_MASK_CLIP_PIN, "Opacity Mask Clip", vt::float1, math::vec4(DEFAULT_OPACITY_MASK_CLIP)),
                     "The Opacity Mask value below which a pixel is cut out. Used only while the material is masked; "
                     "a parameter linked here lets material instances change it."),
        in_color("subsurface_color", "Subsurface Color", vt::float3, math::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
        in_unit("subsurface_opacity", "Subsurface Opacity", vt::float1),
        with_tooltip(in("camera_fade_distance", "Camera Fade Distance", vt::float1),
                     "Within this distance of the camera, in world units, the surface dithers out as the camera comes "
                     "closer, so it never cuts through the view. 0: it does not fade."),
        with_tooltip(in("world_position_offset", "World Position Offset", vt::float3),
                     "Moves each vertex by this, in world units: computed per vertex, for wind, waves or pulses. Its "
                     "shadow and motion follow. Set Max Offset to how far it goes, so the mesh is not culled while "
                     "moved out of its bounds."),
    };
    material_setting_def two_sided;
    two_sided.name = MATERIAL_TWO_SIDED_SETTING;
    two_sided.label = "Two Sided";
    two_sided.kind = material_setting_kind::choice;
    two_sided.choices = {"off", MATERIAL_TWO_SIDED_ON};
    two_sided.choice_labels = {"Off", "On"};
    two_sided.default_value = "off";
    two_sided.tooltip = "Back faces are drawn too, their normal turned toward the viewer.";
    material_setting_def cull_front_faces;
    cull_front_faces.name = MATERIAL_CULL_FRONT_FACES_SETTING;
    cull_front_faces.label = "Cull Front Faces";
    cull_front_faces.kind = material_setting_kind::choice;
    cull_front_faces.choices = {"off", MATERIAL_CULL_FRONT_FACES_ON};
    cull_front_faces.choice_labels = {"Off", "On"};
    cull_front_faces.default_value = "off";
    cull_front_faces.tooltip = "Front faces are hidden and back faces drawn, for a mesh seen from inside (a sky dome, an "
                               "interior). Two Sided draws both and takes precedence.";
    material_setting_def max_offset;
    max_offset.name = "max_world_position_offset";
    max_offset.label = "Max Offset";
    max_offset.kind = material_setting_kind::number;
    max_offset.default_value = "0";
    max_offset.tooltip = "How far World Position Offset moves a vertex at most, in world units: the bounds of the meshes "
                         "drawn with the material grow by it, so they are not culled while moved out of them.";
    material_setting_def workflow;
    workflow.name = MATERIAL_WORKFLOW_SETTING;
    workflow.label = "Workflow";
    workflow.kind = material_setting_kind::choice;
    workflow.choices = {MATERIAL_WORKFLOW_METALLIC, MATERIAL_WORKFLOW_SPECULAR};
    workflow.choice_labels = {"Metallic", "Specular"};
    workflow.default_value = MATERIAL_WORKFLOW_METALLIC;
    workflow.tooltip = "Metallic: Base Color, Metallic and Roughness. Specular: Base Color is the diffuse color, with "
                       "Specular and Glossiness (KHR_materials_pbrSpecularGlossiness); the shader converts them per "
                       "pixel to what the metallic workflow writes.";
    material_setting_def translucent;
    translucent.name = MATERIAL_TRANSLUCENT_SETTING;
    translucent.label = "Translucent";
    translucent.kind = material_setting_kind::choice;
    translucent.choices = {"off", MATERIAL_TRANSLUCENT_ON};
    translucent.choice_labels = {"Off", "On"};
    translucent.default_value = "off";
    translucent.tooltip = "The surface is see-through: the G-buffer dithers it out by its Opacity Mask, read as an "
                          "alpha (not clipped at Opacity Mask Clip). It casts no shadow, and the GI lets light through "
                          "it.";
    def.settings = {workflow, two_sided, cull_front_faces, translucent, max_offset};
    return def;
}

void add_input_nodes(std::vector<material_node_def>& nodes)
{
    auto texcoord = make_fixed("texcoord",
                               "Texture Coordinate",
                               category::input,
                               "The mesh's first texture coordinates (UV).",
                               {},
                               {out("uv", "UV", vt::float2), out("u", "U", vt::float1, ".x"), out("v", "V", vt::float1, ".y")},
                               vt::float2,
                               UV_CODE);
    nodes.push_back(as_inline(with_shortcut(texcoord, 'U', "uv texcoord coordinates")));
    nodes.push_back(as_inline(with_keywords(make_fixed("world_position",
                                                       "World Position",
                                                       category::input,
                                                       "The pixel's position in world space.",
                                                       {},
                                                       {out("xyz", "XYZ", vt::float3)},
                                                       vt::float3,
                                                       "i.world_position"),
                                            "absolute worldpos")));
    nodes.push_back(as_inline(make_fixed("vertex_normal",
                                         "Vertex Normal",
                                         category::input,
                                         "The mesh's interpolated normal in world space, before any normal map.",
                                         {},
                                         {out("xyz", "XYZ", vt::float3)},
                                         vt::float3,
                                         VERTEX_NORMAL_CODE)));
    nodes.push_back(as_inline(with_keywords(make_fixed("view_direction",
                                                       "View Direction",
                                                       category::input,
                                                       "Unit vector from the pixel toward the camera, in world space.",
                                                       {},
                                                       {out("xyz", "XYZ", vt::float3)},
                                                       vt::float3,
                                                       "i.view_direction"),
                                            "camera vector eye")));
    nodes.push_back(as_inline(make_fixed("camera_position",
                                         "Camera Position",
                                         category::input,
                                         "The camera's position in world space.",
                                         {},
                                         {out("xyz", "XYZ", vt::float3)},
                                         vt::float3,
                                         "i.camera_position")));
    nodes.push_back(as_inline(make_fixed("screen_position",
                                         "Screen Position",
                                         category::input,
                                         "The pixel's position on the view, 0 to 1 from the top-left corner.",
                                         {},
                                         {out("uv", "UV", vt::float2)},
                                         vt::float2,
                                         "i.screen_uv")));
    nodes.push_back(as_inline(with_keywords(make_fixed("vertex_tangent",
                                                       "Vertex Tangent",
                                                       category::input,
                                                       "The mesh's tangent in world space: with the bitangent and the vertex "
                                                       "normal, the tangent space normal maps and the Normal channel are in.",
                                                       {},
                                                       {out("xyz", "XYZ", vt::float3)},
                                                       vt::float3,
                                                       "i.tangent"),
                                            "tangent space basis")));
    nodes.push_back(as_inline(with_keywords(make_fixed("vertex_bitangent",
                                                       "Vertex Bitangent",
                                                       category::input,
                                                       "The mesh's bitangent in world space, at a right angle to the tangent "
                                                       "and the vertex normal.",
                                                       {},
                                                       {out("xyz", "XYZ", vt::float3)},
                                                       vt::float3,
                                                       "i.bitangent"),
                                            "binormal tangent space basis")));
    nodes.push_back(as_inline(with_keywords(make_fixed("two_sided_sign",
                                                       "Two Sided Sign",
                                                       category::input,
                                                       "1 where the surface faces the viewer, -1 on its back (a two-sided "
                                                       "material): a different look for each side of leaves, cloth or paper. "
                                                       "A vertex reads 1.",
                                                       {},
                                                       {out("sign", "Sign", vt::float1)},
                                                       vt::float1,
                                                       "i.two_sided_sign"),
                                            "front face back face is front facing")));
    nodes.push_back(as_inline(with_keywords(make_fixed("camera_direction",
                                                       "Camera Direction",
                                                       category::input,
                                                       "The direction the camera looks along, in world space.",
                                                       {},
                                                       {out("xyz", "XYZ", vt::float3)},
                                                       vt::float3,
                                                       "i.camera_direction"),
                                            "camera forward view vector look")));
    nodes.push_back(with_keywords(make_fixed("pixel_depth",
                                             "Pixel Depth",
                                             category::input,
                                             "How far in front of the camera the pixel is, along the camera's direction, in "
                                             "world units.",
                                             {},
                                             {out("depth", "Depth", vt::float1)},
                                             vt::float1,
                                             "dot(i.world_position - i.camera_position, i.camera_direction)"),
                                  "scene depth distance camera"));
    nodes.push_back(as_inline(with_keywords(make_fixed("screen_size",
                                                       "Screen Size",
                                                       category::input,
                                                       "The size of the view in pixels (Size), and the size of one pixel in "
                                                       "screen position units (Texel).",
                                                       {},
                                                       {out("size", "Size", vt::float2, ".xy"), out("texel", "Texel", vt::float2, ".zw")},
                                                       vt::float4,
                                                       "vec4(u_viewRect.zw, 1.0 / max(u_viewRect.zw, vec2_splat(1.0)))"),
                                            "resolution view size texel pixel")));
    nodes.push_back(as_inline(with_keywords(make_fixed("time",
                                                       "Time",
                                                       category::input,
                                                       "Seconds since the game started, for animated materials.",
                                                       {},
                                                       {out("seconds", "Seconds", vt::float1)},
                                                       vt::float1,
                                                       TIME_CODE),
                                            "seconds animate")));
}

void add_constant_nodes(std::vector<material_node_def>& nodes)
{
    nodes.push_back(as_inline(with_shortcut(make_fixed("constant",
                                                       "Constant",
                                                       category::constant,
                                                       "A number.",
                                                       {in_value("value", "Value", vt::float1)},
                                                       {out("value", "", vt::float1)},
                                                       vt::float1,
                                                       "{value}"),
                                            '1',
                                            "scalar float number value")));
    nodes.push_back(as_inline(with_shortcut(make_fixed("vector2",
                                                       "Vector 2",
                                                       category::constant,
                                                       "Two numbers.",
                                                       {in_value("value", "Value", vt::float2)},
                                                       {out("xy", "", vt::float2)},
                                                       vt::float2,
                                                       "{value}"),
                                            '2',
                                            "float2 vec2")));
    nodes.push_back(as_inline(with_shortcut(make_fixed("vector3",
                                                       "Vector 3",
                                                       category::constant,
                                                       "Three numbers.",
                                                       {in_value("value", "Value", vt::float3)},
                                                       {out("xyz", "", vt::float3)},
                                                       vt::float3,
                                                       "{value}"),
                                            '3',
                                            "float3 vec3")));
    nodes.push_back(as_inline(with_shortcut(make_fixed("color",
                                                       "Color",
                                                       category::constant,
                                                       "A colour with alpha, in linear space.",
                                                       {in_value("value", "Color", vt::float4, math::vec4(1.0f), true)},
                                                       color_outputs(),
                                                       vt::float4,
                                                       "{value}"),
                                            '4',
                                            "colour rgb rgba float4 vec4")));
}

/// A number setting of a scalar parameter bounding the slider the editor offers its value with.
auto make_slider_setting(const char* name, const char* label, const char* tooltip) -> material_setting_def
{
    material_setting_def setting;
    setting.name = name;
    setting.label = label;
    setting.kind = material_setting_kind::number;
    setting.default_value = "0";
    setting.tooltip = tooltip;
    return setting;
}

void add_parameter_nodes(std::vector<material_node_def>& nodes)
{
    auto scalar = make_node("scalar_parameter",
                            "Scalar Parameter",
                            category::parameter,
                            "A number material instances and scripts can change without a new shader.");
    scalar.code = material_node_code::parameter;
    scalar.value_type = vt::float1;
    scalar.inputs = {in_value("value", "Default", vt::float1)};
    scalar.outputs = {out("value", "", vt::float1)};
    scalar.settings = {make_parameter_name_setting(),
                       make_description_setting(),
                       make_per_instance_setting(),
                       make_slider_setting(MATERIAL_PARAMETER_SLIDER_MIN_SETTING,
                                           "Slider Min",
                                           "The least value of the slider the inspector edits the parameter with. The "
                                           "slider shows while Slider Max is greater."),
                       make_slider_setting(MATERIAL_PARAMETER_SLIDER_MAX_SETTING,
                                           "Slider Max",
                                           "The greatest value of the slider the inspector edits the parameter with. "
                                           "Equal to Slider Min, the value is dragged without a range.")};
    nodes.push_back(with_shortcut(scalar, 'S', "float exposed property uniform"));
    auto vector = make_node("vector_parameter",
                            "Vector Parameter",
                            category::parameter,
                            "Four numbers material instances and scripts can change without a new shader.");
    vector.code = material_node_code::parameter;
    vector.value_type = vt::float4;
    vector.inputs = {in_value("value", "Default", vt::float4)};
    vector.outputs = {out("xyzw", "XYZW", vt::float4),
                      out("xyz", "XYZ", vt::float3, ".xyz"),
                      out("x", "X", vt::float1, ".x"),
                      out("y", "Y", vt::float1, ".y"),
                      out("z", "Z", vt::float1, ".z"),
                      out("w", "W", vt::float1, ".w")};
    vector.settings = {make_parameter_name_setting(), make_description_setting(), make_per_instance_setting()};
    nodes.push_back(with_shortcut(vector, 'V', "float4 vec4 exposed property uniform"));
    auto color = make_node("color_parameter",
                           "Color Parameter",
                           category::parameter,
                           "A colour material instances and scripts can change without a new shader.");
    color.code = material_node_code::parameter;
    color.value_type = vt::float4;
    color.inputs = {in_value("value", "Default", vt::float4, math::vec4(1.0f), true)};
    color.outputs = color_outputs();
    color.settings = {make_parameter_name_setting(), make_description_setting(), make_per_instance_setting()};
    nodes.push_back(with_keywords(color, "colour tint exposed property uniform"));
}

/// The input of a texture node taking a Texture Object: linked, the node reads that texture instead of its own.
auto make_texture_input(vt kind) -> material_pin_def
{
    auto pin = in(MATERIAL_TEXTURE_PIN, "Texture", kind);
    pin.tooltip = "A Texture Object, or a texture a material function or a reroute passes on: linked, the node reads it "
                  "instead of its own texture.";
    return pin;
}

/// How a texture node's texture is read at its edges and between texels: binding state, never code.
auto make_sampler_settings() -> std::vector<material_setting_def>
{
    return {make_choice_setting(MATERIAL_TEXTURE_ADDRESS_SETTING,
                                "Address",
                                {MATERIAL_TEXTURE_ADDRESS_TEXTURE, "wrap", "clamp", "mirror"},
                                {"From Texture", "Wrap", "Clamp", "Mirror"},
                                MATERIAL_TEXTURE_ADDRESS_TEXTURE,
                                "What UVs outside 0..1 read. From Texture: the texture's import setting. Wrap: it repeats. "
                                "Clamp: its edge texels stretch out (decals, gradients). Mirror: it repeats flipped."),
            make_choice_setting(MATERIAL_TEXTURE_FILTER_SETTING,
                                "Filter",
                                {MATERIAL_TEXTURE_FILTER_ANISOTROPIC, "trilinear", "point"},
                                {"Anisotropic", "Trilinear", "Point"},
                                MATERIAL_TEXTURE_FILTER_ANISOTROPIC,
                                "How texels blend. Anisotropic: sharp at grazing angles, the default. Trilinear: cheaper, "
                                "blurrier at grazing angles. Point: no blending, hard pixels (pixel art, lookup "
                                "tables); a texture imported for point sampling reads so whatever this says.")};
}

/// The settings of a node holding a texture of its own: its name and hint for instances and the inspector, and its
/// sampler state.
auto make_texture_settings() -> std::vector<material_setting_def>
{
    std::vector<material_setting_def> settings{make_parameter_name_setting(TEXTURE_NAME_TOOLTIP), make_description_setting()};
    const auto sampler = make_sampler_settings();
    settings.insert(settings.end(), sampler.begin(), sampler.end());
    return settings;
}

/// The mip a sample reads: picked by the GPU (Auto), shifted (Bias) or given (Level), by the Mip input.
auto make_mip_setting() -> material_setting_def
{
    return make_choice_setting(MATERIAL_TEXTURE_MIP_MODE_SETTING,
                               "Mip",
                               {"auto", "bias", "level"},
                               {"Auto", "Bias", "Level"},
                               "auto",
                               "Auto: the mip the GPU picks for the pixel's footprint. Bias: that mip shifted by the Mip "
                               "input (positive: blurrier). Level: the mip the Mip input gives (0: the full size), the "
                               "same in every stage.");
}

auto make_mip_input() -> material_pin_def
{
    return with_tooltip(in("mip", "Mip", vt::float1), "The mip bias or level the Mip setting reads it as; unused on Auto.");
}

/// The channel a height or mask is read from.
auto make_channel_setting() -> material_setting_def
{
    return make_choice_setting("channel", "Channel", {"r", "g", "b", "a"}, {"Red", "Green", "Blue", "Alpha"}, "r", "The texture's channel holding the height.");
}

void add_texture_nodes(std::vector<material_node_def>& nodes)
{
    auto sample = make_node("texture_sample",
                            "Texture Sample",
                            category::texture,
                            "Reads a texture at the given UV (the mesh's own while unlinked): its own, or the one linked "
                            "into Texture.");
    sample.code = material_node_code::texture_sample;
    sample.value_type = vt::float4;
    sample.inputs = {in_code("uv", "UV", vt::float2, UV_CODE), make_texture_input(vt::texture2d), make_mip_input()};
    sample.outputs = texel_outputs();
    sample.settings = make_texture_settings();
    sample.settings.push_back(make_mip_setting());
    nodes.push_back(with_shortcut(sample, 'T', "texture sampler image map albedo"));
    auto normal = make_node("normal_map",
                            "Normal Map",
                            category::texture,
                            "Reads a normal map as a tangent-space normal for the Normal channel.");
    normal.code = material_node_code::normal_map;
    normal.value_type = vt::float3;
    normal.inputs = {in_code("uv", "UV", vt::float2, UV_CODE), in("strength", "Strength", vt::float1, math::vec4(1.0f)), make_texture_input(vt::texture2d)};
    normal.outputs = {out("normal", "Normal", vt::float3)};
    normal.settings = make_texture_settings();
    nodes.push_back(with_shortcut(normal, 'N', "bump normalmap tangent"));
    auto triplanar = make_node("triplanar_sample",
                               "Triplanar Sample",
                               category::texture,
                               "Reads a texture projected along the three world axes at Position times Scale, blended by "
                               "the Normal: no UVs, and no stretching on steep faces. Sharpness narrows the blends.");
    triplanar.code = material_node_code::triplanar_sample;
    triplanar.value_type = vt::float4;
    triplanar.inputs = {in_code("position", "Position", vt::float3, WORLD_POSITION_CODE),
                        in_code("normal", "Normal", vt::float3, VERTEX_NORMAL_CODE),
                        in("scale", "Scale", vt::float1, math::vec4(1.0f)),
                        in("sharpness", "Sharpness", vt::float1, math::vec4(DEFAULT_TRIPLANAR_SHARPNESS)),
                        make_texture_input(vt::texture2d)};
    triplanar.outputs = texel_outputs();
    triplanar.settings = make_texture_settings();
    nodes.push_back(with_keywords(triplanar, "world projection box mapping cube projection"));
    auto cube = make_node("sample_cubemap",
                          "Sample Cubemap",
                          category::texture,
                          "Reads a cube texture along Direction (unlinked, the view reflected about the vertex normal: "
                          "a cheap mirror of an environment).");
    cube.code = material_node_code::texture_sample;
    cube.value_type = vt::float4;
    cube.inputs = {in_code("direction", "Direction", vt::float3, "reflect(-i.view_direction, i.vertex_normal)"),
                   make_texture_input(vt::texture_cube),
                   make_mip_input()};
    cube.outputs = texel_outputs();
    cube.settings = make_texture_settings();
    cube.settings.push_back(make_mip_setting());
    nodes.push_back(with_keywords(cube, "cubemap environment reflection skybox texture cube"));
    auto object = make_node("texture_object",
                            "Texture Object",
                            category::texture,
                            "A texture, not yet read: link it into a texture node's Texture, a Custom node or a material "
                            "function to read it there. Instances and scripts set it by name, as a Texture Sample's.");
    object.code = material_node_code::texture_object;
    object.value_type = vt::texture2d;
    object.outputs = {out("texture", "Texture", vt::texture2d)};
    object.settings = make_texture_settings();
    nodes.push_back(with_keywords(object, "texture asset reference object parameter sampler"));
    auto cube_object = make_node("cubemap_object",
                                 "Cubemap Object",
                                 category::texture,
                                 "A cube texture, not yet read: link it into a Sample Cubemap, a Custom node or a material "
                                 "function.");
    cube_object.code = material_node_code::texture_object;
    cube_object.value_type = vt::texture_cube;
    cube_object.outputs = {out("texture", "Texture", vt::texture_cube)};
    cube_object.settings = make_texture_settings();
    nodes.push_back(with_keywords(cube_object, "cubemap environment texture cube object reference"));
    auto parallax = make_node("parallax_occlusion",
                              "Parallax Occlusion",
                              category::texture,
                              "The UV a height map shows at the pixel when its bumps rise up to Height Scale above the "
                              "surface (in UV units): steps through the height map along the view, so bumps hide what lies "
                              "behind them. Link the UV into the material's texture reads. The pixel stage only.");
    parallax.code = material_node_code::parallax_occlusion;
    parallax.value_type = vt::float2;
    parallax.is_pixel_only = true;
    parallax.inputs = {in_code("uv", "UV", vt::float2, UV_CODE),
                       make_texture_input(vt::texture2d),
                       in("height_scale", "Height Scale", vt::float1, math::vec4(0.05f)),
                       with_tooltip(in("min_steps", "Min Steps", vt::float1, math::vec4(8.0f)), "Steps where the surface faces the view."),
                       with_tooltip(in("max_steps", "Max Steps", vt::float1, math::vec4(32.0f)),
                                    "Steps at grazing angles, where the bumps cover more; at most 64."),
                       with_tooltip(in_unit("reference_plane", "Reference Plane", vt::float1, math::vec4(1.0f)),
                                    "The height that stays where the mesh is: 1 keeps the tops on the surface and sinks "
                                    "the rest, 0 raises everything above it.")};
    parallax.outputs = {out("uv", "UV", vt::float2)};
    parallax.settings = make_texture_settings();
    parallax.settings.push_back(make_channel_setting());
    nodes.push_back(with_keywords(parallax, "pom parallax occlusion mapping height displacement depth relief"));
    auto from_texture = make_node("normal_from_texture",
                                  "Normal From Texture",
                                  category::texture,
                                  "A tangent-space normal from a height map: its slopes read Offset UV units apart, times "
                                  "Strength.");
    from_texture.code = material_node_code::normal_from_texture;
    from_texture.value_type = vt::float3;
    from_texture.inputs = {in_code("uv", "UV", vt::float2, UV_CODE),
                           make_texture_input(vt::texture2d),
                           in("offset", "Offset", vt::float1, math::vec4(0.002f)),
                           in("strength", "Strength", vt::float1, math::vec4(1.0f))};
    from_texture.outputs = {out("normal", "Normal", vt::float3)};
    from_texture.settings = make_texture_settings();
    from_texture.settings.push_back(make_channel_setting());
    nodes.push_back(with_keywords(from_texture, "height bump normal map from height texture"));
}

/// The channels of a whole surface, as the Surface Output has them (Make Material Attributes takes them in this order,
/// mg_make_attributes reads them so).
auto make_attribute_channels() -> std::vector<material_pin_def>
{
    return {in_color("base_color", "Base Color", vt::float3, math::vec4(DEFAULT_GREY, DEFAULT_GREY, DEFAULT_GREY, 1.0f)),
            in_unit(MATERIAL_METALLIC_PIN, "Metallic", vt::float1),
            in_unit(MATERIAL_ROUGHNESS_PIN, "Roughness", vt::float1, math::vec4(DEFAULT_ROUGHNESS)),
            in_color(MATERIAL_SPECULAR_PIN, "Specular", vt::float3, math::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
            in_unit(MATERIAL_GLOSSINESS_PIN, "Glossiness", vt::float1, math::vec4(1.0f - DEFAULT_ROUGHNESS)),
            in("normal", "Normal", vt::float3, math::vec4(0.0f, 0.0f, 1.0f, 0.0f)),
            in_color("emissive", "Emissive", vt::float3, math::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
            in_unit("ambient_occlusion", "Ambient Occlusion", vt::float1, math::vec4(1.0f)),
            in_unit("opacity_mask", "Opacity Mask", vt::float1, math::vec4(1.0f)),
            in_color("subsurface_color", "Subsurface Color", vt::float3, math::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
            in_unit("subsurface_opacity", "Subsurface Opacity", vt::float1)};
}

void add_attribute_nodes(std::vector<material_node_def>& nodes)
{
    auto make = make_fixed("make_material_attributes",
                           "Make Material Attributes",
                           category::utility,
                           "A whole surface from its channels: blend it with another one (Blend Material Attributes), "
                           "pass it through a material function as one value, or link it into the Surface Output's "
                           "Material Attributes.",
                           make_attribute_channels(),
                           {out("attributes", "Attributes", vt::attributes)},
                           vt::attributes,
                           "mg_make_attributes({base_color}, {metallic}, {roughness}, {specular}, {glossiness}, {normal}, "
                           "{emissive}, {ambient_occlusion}, {opacity_mask}, {subsurface_color}, {subsurface_opacity})");
    nodes.push_back(with_keywords(make, "material attributes layer surface make struct"));
    auto blend = make_fixed("blend_material_attributes",
                            "Blend Material Attributes",
                            category::utility,
                            "Two whole surfaces blended by Alpha (A at 0, B at 1), every channel at once, the normals "
                            "turned between them: wet over dry, snow or moss over rock, layers by a painted mask.",
                            {in("a", "A", vt::attributes), in("b", "B", vt::attributes), in_unit("alpha", "Alpha", vt::float1, math::vec4(0.5f))},
                            {out("attributes", "Attributes", vt::attributes)},
                            vt::attributes,
                            "mg_blend_attributes({a}, {b}, {alpha})");
    nodes.push_back(with_keywords(blend, "material attributes layer blend mix lerp surfaces"));
    auto split = make_fixed("break_material_attributes",
                            "Break Material Attributes",
                            category::utility,
                            "The channels of a whole surface, each on its own output.",
                            {in("attributes", "Attributes", vt::attributes)},
                            {},
                            vt::attributes,
                            "{attributes}");
    for(const auto& channel : make_attribute_channels())
    {
        auto output = out("", "", channel.type);
        output.name = channel.name;
        output.label = channel.label;
        output.member = channel.name;
        split.outputs.push_back(output);
    }
    nodes.push_back(with_keywords(split, "material attributes break split channels struct"));
}

/// Trigonometry, exponents, rounding, derivatives and the small conveniences shader code writes in a line.
void add_more_math_nodes(std::vector<material_node_def>& nodes)
{
    nodes.push_back(with_keywords(make_unary("tan", "Tangent", "The tangent of the value, in radians.", "tan({in})"), "tan trigonometry"));
    nodes.push_back(with_keywords(make_unary("asin", "Arcsine", "The angle, in radians, whose sine is the value (-1 to 1).", "asin({in})"),
                                  "asin arcsin inverse sine trigonometry"));
    nodes.push_back(with_keywords(make_unary("acos", "Arccosine", "The angle, in radians, whose cosine is the value (-1 to 1).", "acos({in})"),
                                  "acos arccos inverse cosine trigonometry"));
    nodes.push_back(with_keywords(make_unary("atan", "Arctangent", "The angle, in radians, whose tangent is the value.", "atan({in})"),
                                  "atan arctan inverse tangent trigonometry"));
    auto atan2 = make_binary("atan2",
                             "Arctangent2",
                             "The angle, in radians from -pi to pi, of the direction (X, Y): the angle around a point, for "
                             "radial patterns.",
                             "atan2({a}, {b})",
                             0.0f,
                             1.0f);
    atan2.inputs[0].label = "Y";
    atan2.inputs[1].label = "X";
    nodes.push_back(with_keywords(atan2, "atan2 angle polar direction trigonometry"));
    nodes.push_back(with_keywords(make_unary("exp", "Exp", "e raised to the value.", "exp({in})"), "exponent exponential e"));
    nodes.push_back(with_keywords(make_unary("exp2", "Exp2", "2 raised to the value.", "exp2({in})"), "exponent power of two"));
    nodes.push_back(with_keywords(make_unary("log", "Log", "The natural logarithm of the value (above 0).", "log({in})", 1.0f), "ln logarithm natural"));
    nodes.push_back(with_keywords(make_unary("log2", "Log2", "The base-2 logarithm of the value (above 0).", "log2({in})", 1.0f), "logarithm binary"));
    nodes.push_back(with_keywords(make_unary("round", "Round", "The nearest whole number to the value.", "round({in})"), "nearest integer"));
    nodes.push_back(with_keywords(make_unary("truncate", "Truncate", "The whole part of the value: its fraction dropped, toward 0.", "trunc({in})"),
                                  "trunc integer whole"));
    nodes.push_back(with_keywords(make_unary("sign", "Sign", "1 for a positive value, -1 for a negative one, 0 for 0.", "sign({in})"),
                                  "signum direction"));
    nodes.push_back(with_keywords(make_unary("reciprocal", "Reciprocal", "1 divided by the value.", "(1.0 / {in})", 1.0f), "inverse 1/x rcp"));
    nodes.push_back(with_keywords(make_unary("radians_to_degrees", "Radians To Degrees", "The angle in degrees.", "degrees({in})"),
                                  "degrees angle convert"));
    nodes.push_back(with_keywords(make_unary("degrees_to_radians", "Degrees To Radians", "The angle in radians.", "radians({in})"),
                                  "radians angle convert"));
    nodes.push_back(with_keywords(make_dynamic("inverse_lerp",
                                               "Inverse Lerp",
                                               category::math,
                                               "Where Value lies between A and B: 0 at A, 1 at B. The inverse of Lerp.",
                                               {in("a", "A", vt::dynamic),
                                                in("b", "B", vt::dynamic, math::vec4(1.0f)),
                                                in("value", "Value", vt::dynamic, math::vec4(0.5f))},
                                               "(({value} - {a}) / ({b} - {a}))"),
                                  "unlerp inverse interpolate fraction"));
    nodes.push_back(with_keywords(make_dynamic("constant_bias_scale",
                                               "Constant Bias Scale",
                                               category::math,
                                               "The value plus Bias, times Scale: -1..1 into 0..1 with the defaults.",
                                               {in("in", "", vt::dynamic),
                                                in("bias", "Bias", vt::float1, math::vec4(1.0f)),
                                                in("scale", "Scale", vt::float1, math::vec4(0.5f))},
                                               "(({in} + {bias}) * {scale})"),
                                  "bias scale signed unsigned range"));
    auto constant = make_fixed("math_constant",
                               "Math Constant",
                               category::constant,
                               "A constant of mathematics: pi, tau (2 pi), pi / 2, e, the golden ratio or the square root of 2.",
                               {},
                               {out("value", "", vt::float1)},
                               vt::float1,
                               "mg_const_{constant}");
    constant.settings = {make_choice_setting("constant",
                                             "Constant",
                                             {"pi", "tau", "half_pi", "e", "golden_ratio", "sqrt2"},
                                             {"Pi", "Tau", "Half Pi", "E", "Golden Ratio", "Square Root Of 2"},
                                             "pi",
                                             "The constant.")};
    nodes.push_back(as_inline(with_keywords(constant, "pi tau e golden ratio sqrt2 number")));
    auto reflect = make_fixed("reflect",
                              "Reflect",
                              category::vector,
                              "In mirrored about Normal, as light bounces. Unlinked, the view mirrored about the vertex normal: "
                              "the reflection vector.",
                              {in_code("in", "In", vt::float3, "(-i.view_direction)"), in_code("normal", "Normal", vt::float3, VERTEX_NORMAL_CODE)},
                              {out("out", "", vt::float3)},
                              vt::float3,
                              "reflect({in}, normalize({normal}))");
    nodes.push_back(with_keywords(reflect, "reflection vector mirror bounce"));
    auto refract = make_fixed("refract",
                              "Refract",
                              category::vector,
                              "In bent through a surface of Normal, as light entering glass or water bends; Ratio is the "
                              "outside's index of refraction over the inside's (1 / 1.33 into water). Unlinked, the view "
                              "through the vertex normal.",
                              {in_code("in", "In", vt::float3, "(-i.view_direction)"),
                               in_code("normal", "Normal", vt::float3, VERTEX_NORMAL_CODE),
                               in("ratio", "Ratio", vt::float1, math::vec4(0.75f))},
                              {out("out", "", vt::float3)},
                              vt::float3,
                              "refract(normalize({in}), normalize({normal}), {ratio})");
    nodes.push_back(with_keywords(refract, "refraction bend glass water ior"));
    // The float forms cast: DXC reads a bare literal as a double, and DXIL has no double derivative.
    auto ddx = make_unary("ddx", "DDX", "How much the value changes from this pixel to the next one across the screen. The pixel stage only.", "dFdx({in})");
    ddx.scalar_expression = "dFdx(float({in}))";
    nodes.push_back(as_pixel_only(with_keywords(ddx, "derivative dfdx partial screen space")));
    auto ddy = make_unary("ddy", "DDY", "How much the value changes from this pixel to the next one down the screen. The pixel stage only.", "dFdy({in})");
    ddy.scalar_expression = "dFdy(float({in}))";
    nodes.push_back(as_pixel_only(with_keywords(ddy, "derivative dfdy partial screen space")));
    auto width = make_unary("fwidth",
                            "FWidth",
                            "How much the value changes over one pixel (DDX and DDY added without their signs): the width to "
                            "smooth a procedural edge over. The pixel stage only.",
                            "fwidth({in})");
    width.scalar_expression = "fwidth(float({in}))";
    nodes.push_back(as_pixel_only(with_keywords(width, "derivative antialias filter width")));
}

void add_math_nodes(std::vector<material_node_def>& nodes)
{
    nodes.push_back(with_shortcut(make_binary("add", "Add", "A + B, per component.", "({a} + {b})", 0.0f, 0.0f), 'A', "plus sum +"));
    nodes.push_back(with_keywords(make_binary("subtract", "Subtract", "A - B, per component.", "({a} - {b})", 0.0f, 0.0f),
                                  "minus difference -"));
    nodes.push_back(with_shortcut(make_binary("multiply", "Multiply", "A x B, per component.", "({a} * {b})", 1.0f, 1.0f),
                                  'M',
                                  "mul times product scale *"));
    nodes.push_back(with_shortcut(make_binary("divide", "Divide", "A / B, per component.", "({a} / {b})", 1.0f, 1.0f),
                                  'D',
                                  "div quotient /"));
    nodes.push_back(with_keywords(make_binary("modulo",
                                              "Modulo",
                                              "The remainder of A / B, with the sign of B.",
                                              "({a} - {b} * floor({a} / {b}))",
                                              0.0f,
                                              1.0f),
                                  "mod fmod remainder %"));
    nodes.push_back(with_keywords(make_binary("minimum", "Minimum", "The smaller of A and B, per component.", "min({a}, {b})", 0.0f, 0.0f),
                                  "min smaller"));
    nodes.push_back(with_keywords(make_binary("maximum", "Maximum", "The larger of A and B, per component.", "max({a}, {b})", 0.0f, 0.0f),
                                  "max larger"));
    nodes.push_back(with_shortcut(make_binary("power", "Power", "Base raised to Exponent, per component.", "pow({a}, {b})", 1.0f, 2.0f),
                                  'E',
                                  "pow exponent"));
    nodes.back().inputs[0].label = "Base";
    nodes.back().inputs[1].label = "Exp";
    nodes.push_back(with_keywords(make_binary("step", "Step", "1 where B is at least A (the edge), else 0.", "step({a}, {b})", 0.5f, 0.0f),
                                  "threshold edge"));
    nodes.back().inputs[0].label = "Edge";
    nodes.back().inputs[1].label = "In";
    nodes.push_back(with_shortcut(make_dynamic("lerp",
                                               "Lerp",
                                               category::math,
                                               "Blends from A to B by Alpha: A at 0, B at 1.",
                                               {in("a", "A", vt::dynamic),
                                                in("b", "B", vt::dynamic, math::vec4(1.0f)),
                                                in("alpha", "Alpha", vt::dynamic, math::vec4(0.5f))},
                                               "mix({a}, {b}, {alpha})"),
                                  'L',
                                  "mix blend interpolate linear"));
    nodes.push_back(with_keywords(make_dynamic("clamp",
                                               "Clamp",
                                               category::math,
                                               "Keeps the value between Min and Max.",
                                               {in("in", "", vt::dynamic),
                                                in("min", "Min", vt::dynamic),
                                                in("max", "Max", vt::dynamic, math::vec4(1.0f))},
                                               "clamp({in}, {min}, {max})"),
                                  "limit range"));
    nodes.push_back(with_keywords(make_dynamic("smoothstep",
                                               "Smooth Step",
                                               category::math,
                                               "0 below Min, 1 above Max, a smooth curve between.",
                                               {in("min", "Min", vt::dynamic),
                                                in("max", "Max", vt::dynamic, math::vec4(1.0f)),
                                                in("in", "In", vt::dynamic)},
                                               "smoothstep({min}, {max}, {in})"),
                                  "smooth hermite falloff"));
    nodes.push_back(with_keywords(make_dynamic("remap",
                                               "Remap",
                                               category::math,
                                               "Maps the value from the In range onto the Out range.",
                                               {in("in", "", vt::dynamic),
                                                in("in_min", "In Min", vt::dynamic),
                                                in("in_max", "In Max", vt::dynamic, math::vec4(1.0f)),
                                                in("out_min", "Out Min", vt::dynamic),
                                                in("out_max", "Out Max", vt::dynamic, math::vec4(1.0f))},
                                               "mix({out_min}, {out_max}, ({in} - {in_min}) / ({in_max} - {in_min}))"),
                                  "range map rescale fit"));
    nodes.push_back(with_shortcut(make_unary("one_minus", "One Minus", "1 - the value: inverts a mask.", "(1.0 - {in})"),
                                  'O',
                                  "invert 1-x complement"));
    nodes.push_back(make_unary("negate", "Negate", "The value with its sign flipped.", "(-{in})"));
    nodes.push_back(make_unary("abs", "Absolute", "The value without its sign.", "abs({in})"));
    nodes.push_back(with_keywords(make_unary("saturate", "Saturate", "Keeps the value between 0 and 1.", "saturate({in})"),
                                  "clamp01"));
    nodes.push_back(make_unary("floor", "Floor", "The largest whole number not above the value.", "floor({in})"));
    nodes.push_back(make_unary("ceil", "Ceil", "The smallest whole number not below the value.", "ceil({in})"));
    nodes.push_back(with_keywords(make_unary("frac", "Frac", "The part of the value after the decimal point.", "fract({in})"),
                                  "fract fraction repeat"));
    nodes.push_back(with_keywords(make_unary("sqrt", "Square Root", "The square root of the value.", "sqrt({in})"), "sqrt root"));
    nodes.push_back(with_keywords(make_unary("sine", "Sine", "The sine of the value, in radians.", "sin({in})"), "sin wave"));
    nodes.push_back(with_keywords(make_unary("cosine", "Cosine", "The cosine of the value, in radians.", "cos({in})"), "cos wave"));
    auto normalize = make_unary("normalize", "Normalize", "The vector scaled to length 1.", "normalize({in})");
    normalize.scalar_expression = "sign({in})";
    nodes.push_back(with_keywords(normalize, "unit direction"));
    auto length = make_dynamic("length", "Length", category::math, "The length of the vector.", {in("in", "", vt::dynamic)}, "length({in})");
    length.type_rule = material_type_rule::fixed;
    length.value_type = vt::float1;
    length.outputs = {out("out", "", vt::float1)};
    length.scalar_expression = "abs({in})";
    nodes.push_back(with_keywords(length, "magnitude size"));
    auto distance = make_dynamic("distance",
                                 "Distance",
                                 category::math,
                                 "The distance between A and B.",
                                 {in("a", "A", vt::dynamic), in("b", "B", vt::dynamic)},
                                 "distance({a}, {b})");
    distance.type_rule = material_type_rule::fixed;
    distance.value_type = vt::float1;
    distance.outputs = {out("out", "", vt::float1)};
    distance.scalar_expression = "abs({a} - {b})";
    nodes.push_back(distance);
    auto dot = make_dynamic("dot",
                            "Dot Product",
                            category::math,
                            "The dot product of A and B: 1 for one direction, 0 at a right angle, -1 for opposite ones.",
                            {in("a", "A", vt::dynamic), in("b", "B", vt::dynamic)},
                            "dot({a}, {b})");
    dot.type_rule = material_type_rule::fixed;
    dot.value_type = vt::float1;
    dot.outputs = {out("out", "", vt::float1)};
    dot.scalar_expression = "({a} * {b})";
    nodes.push_back(with_keywords(dot, "dot product"));
    nodes.push_back(with_keywords(make_fixed("cross",
                                             "Cross Product",
                                             category::math,
                                             "The vector at a right angle to both A and B.",
                                             {in("a", "A", vt::float3), in("b", "B", vt::float3)},
                                             {out("out", "", vt::float3)},
                                             vt::float3,
                                             "cross({a}, {b})"),
                                  "cross product perpendicular"));
    add_more_math_nodes(nodes);
}

void add_vector_nodes(std::vector<material_node_def>& nodes)
{
    auto append = make_node("append", "Append", category::vector, "Joins A and B into one wider vector: A's components first.");
    append.code = material_node_code::append;
    append.type_rule = material_type_rule::append;
    append.value_type = vt::dynamic;
    append.inputs = {in("a", "A", vt::dynamic), in("b", "B", vt::dynamic)};
    append.outputs = {out("out", "", vt::dynamic)};
    nodes.push_back(with_keywords(append, "combine join concat vector"));
    auto split = make_dynamic("split",
                              "Split",
                              category::vector,
                              "The components of a vector, each on its own output; a missing one reads 0.",
                              {in("in", "", vt::dynamic)},
                              "{in}");
    split.outputs = {out("x", "X", vt::float1, ".x"), out("y", "Y", vt::float1, ".y"), out("z", "Z", vt::float1, ".z"), out("w", "W", vt::float1, ".w")};
    nodes.push_back(as_inline(with_keywords(split, "break components channels separate")));
    nodes.push_back(with_keywords(make_fixed("combine",
                                             "Combine",
                                             category::vector,
                                             "Builds a vector of four numbers.",
                                             {in("r", "R", vt::float1),
                                              in("g", "G", vt::float1),
                                              in("b", "B", vt::float1),
                                              in("a", "A", vt::float1, math::vec4(1.0f))},
                                             {out("rgba", "RGBA", vt::float4), out("rgb", "RGB", vt::float3, ".rgb")},
                                             vt::float4,
                                             "vec4({r}, {g}, {b}, {a})"),
                                  "make vector float4 merge"));
    auto swizzle = make_node("swizzle",
                             "Swizzle",
                             category::vector,
                             "Picks and orders components of the input by the mask: \"xy\", \"zyx\", \"rrr\".");
    swizzle.code = material_node_code::swizzle;
    swizzle.type_rule = material_type_rule::swizzle;
    swizzle.value_type = vt::dynamic;
    swizzle.inputs = {in("in", "", vt::dynamic)};
    swizzle.outputs = {out("out", "", vt::dynamic)};
    material_setting_def mask;
    mask.name = "mask";
    mask.label = "Mask";
    mask.kind = material_setting_kind::swizzle;
    mask.default_value = "xyz";
    mask.tooltip = "One to four of x, y, z, w (or r, g, b, a), in the order wanted.";
    swizzle.settings = {mask};
    nodes.push_back(with_keywords(swizzle, "component mask channels shuffle"));
}

/// The inputs of a Noise node reading @p coordinates.
auto make_noise_inputs(material_pin_def coordinates, float scale) -> std::vector<material_pin_def>
{
    return {std::move(coordinates),
            in("scale", "Scale", vt::float1, math::vec4(scale)),
            with_tooltip(in("octaves", "Octaves", vt::float1, math::vec4(1.0f)), "How many layers of noise, 1 to 8."),
            with_tooltip(in("lacunarity", "Lacunarity", vt::float1, math::vec4(2.0f)), "How much finer each layer is than the one before."),
            with_tooltip(in("gain", "Gain", vt::float1, math::vec4(0.5f)), "How much weaker each layer is than the one before.")};
}

auto make_noise_type_setting() -> material_setting_def
{
    return make_choice_setting("type",
                               "Type",
                               {"value", "gradient", "simplex"},
                               {"Value", "Gradient", "Simplex"},
                               "value",
                               "Value: soft blobs, the cheapest. Gradient (Perlin): smooth hills and valleys. Simplex: like "
                               "gradient, without its square grid.");
}

/// A shape node: an anti-aliased mask over the UV square, read with derivatives.
auto make_shape(const char* type, const char* label, const char* description, std::vector<material_pin_def> sizes, const char* expression, const char* keywords)
    -> material_node_def
{
    std::vector<material_pin_def> inputs{in_code("uv", "UV", vt::float2, UV_CODE)};
    inputs.insert(inputs.end(), sizes.begin(), sizes.end());
    return as_pixel_only(with_keywords(make_fixed(type, label, category::procedural, description, std::move(inputs), {out("out", "", vt::float1)}, vt::float1, expression),
                                       keywords));
}

/// Ellipse, Rectangle, Rounded Rectangle and Polygon: anti-aliased masks of a shape over the UV square.
void add_shape_nodes(std::vector<material_node_def>& nodes)
{
    const auto width = []
    {
        return in("width", "Width", vt::float1, math::vec4(0.5f));
    };
    const auto height = []
    {
        return in("height", "Height", vt::float1, math::vec4(0.5f));
    };
    nodes.push_back(make_shape("ellipse",
                               "Ellipse",
                               "1 inside an ellipse Width by Height (1 fills the UV square), 0 outside, with a smooth "
                               "one-pixel edge. The pixel stage only.",
                               {width(), height()},
                               "mg_ellipse({uv}, {width}, {height})",
                               "circle shape disc mask sdf"));
    nodes.push_back(make_shape("rectangle",
                               "Rectangle",
                               "1 inside a rectangle Width by Height (1 fills the UV square), 0 outside, with a smooth "
                               "one-pixel edge. The pixel stage only.",
                               {width(), height()},
                               "mg_rectangle({uv}, {width}, {height})",
                               "square box shape mask sdf"));
    nodes.push_back(make_shape("rounded_rectangle",
                               "Rounded Rectangle",
                               "1 inside a rectangle with corners rounded by Radius, 0 outside, with a smooth one-pixel "
                               "edge. The pixel stage only.",
                               {width(), height(), in("radius", "Radius", vt::float1, math::vec4(0.1f))},
                               "mg_rounded_rectangle({uv}, {width}, {height}, {radius})",
                               "rounded box button shape mask sdf"));
    nodes.push_back(make_shape("polygon",
                               "Polygon",
                               "1 inside a regular polygon of Sides sides, Width by Height, 0 outside, with a smooth "
                               "one-pixel edge. The pixel stage only.",
                               {in("sides", "Sides", vt::float1, math::vec4(6.0f)), width(), height()},
                               "mg_polygon({uv}, {sides}, {width}, {height})",
                               "hexagon triangle shape mask sdf"));
}

void add_procedural_nodes(std::vector<material_node_def>& nodes)
{
    nodes.push_back(with_keywords(make_fixed("checker",
                                             "Checker",
                                             category::procedural,
                                             "A checkerboard of 0 and 1: Scale squares across the UV range.",
                                             {in_code("uv", "UV", vt::float2, UV_CODE),
                                              in("scale", "Scale", vt::float2, math::vec4(DEFAULT_CHECKER_SCALE))},
                                             {out("out", "", vt::float1)},
                                             vt::float1,
                                             "mg_checker({uv}, {scale})"),
                                  "checkerboard grid tiles pattern"));
    auto noise = make_fixed("noise",
                            "Noise",
                            category::procedural,
                            "Smooth noise between 0 and 1 over the UV: Octaves layers of it, each Lacunarity times finer "
                            "and Gain times weaker (fractal noise, fBm).",
                            make_noise_inputs(in_code("uv", "UV", vt::float2, UV_CODE), DEFAULT_NOISE_SCALE),
                            {out("out", "", vt::float1)},
                            vt::float1,
                            "mg_noise_{type}2({uv} * {scale}, {octaves}, {lacunarity}, {gain})");
    noise.settings = {make_noise_type_setting()};
    nodes.push_back(with_keywords(noise, "value gradient perlin simplex fbm fractal octaves random clouds"));
    auto noise_3d = make_fixed("noise_3d",
                               "Noise 3D",
                               category::procedural,
                               "Smooth noise between 0 and 1 through space, at Position (the pixel's world position while "
                               "unlinked): no UV seams. Octaves layer it as Noise does.",
                               make_noise_inputs(in_code("position", "Position", vt::float3, WORLD_POSITION_CODE), 1.0f),
                               {out("out", "", vt::float1)},
                               vt::float1,
                               "mg_noise_{type}3({position} * {scale}, {octaves}, {lacunarity}, {gain})");
    noise_3d.settings = {make_noise_type_setting()};
    nodes.push_back(with_keywords(noise_3d, "value gradient perlin simplex fbm fractal octaves volume world solid"));
    const auto voronoi_outputs = []
    {
        return std::vector<material_pin_def>{with_tooltip(out("distance", "Distance", vt::float1, ".x"), "The distance to the cell's point."),
                                             with_tooltip(out("edges", "Edges", vt::float1, ".y"),
                                                          "The distance to the nearest border between cells: 0 on the borders."),
                                             with_tooltip(out("cell", "Cell", vt::float1, ".z"), "A random value of the cell, 0 to 1.")};
    };
    nodes.push_back(with_keywords(make_fixed("voronoi",
                                             "Voronoi",
                                             category::procedural,
                                             "Cells around points scattered over the UV, Scale cells per UV unit: the "
                                             "distance to the cell's point, to its border, and a random value per cell. "
                                             "Randomness 0 lines the points up in a grid.",
                                             {in_code("uv", "UV", vt::float2, UV_CODE),
                                              in("scale", "Scale", vt::float1, math::vec4(DEFAULT_VORONOI_SCALE)),
                                              in_unit("randomness", "Randomness", vt::float1, math::vec4(1.0f))},
                                             voronoi_outputs(),
                                             vt::float3,
                                             "mg_voronoi({uv} * {scale}, {randomness})"),
                                  "cellular worley cells cracks caustics tiles stones"));
    nodes.push_back(with_keywords(make_fixed("voronoi_3d",
                                             "Voronoi 3D",
                                             category::procedural,
                                             "Cells around points scattered through space, at Position (the world position "
                                             "while unlinked): no UV seams.",
                                             {in_code("position", "Position", vt::float3, WORLD_POSITION_CODE),
                                              in("scale", "Scale", vt::float1, math::vec4(1.0f)),
                                              in_unit("randomness", "Randomness", vt::float1, math::vec4(1.0f))},
                                             voronoi_outputs(),
                                             vt::float3,
                                             "mg_voronoi3({position} * {scale}, {randomness})"),
                                  "cellular worley cells cracks volume world solid"));
    nodes.push_back(with_keywords(make_fixed("fresnel",
                                             "Fresnel",
                                             category::procedural,
                                             "0 where the surface faces the camera, rising to 1 at its edges.",
                                             {in_code("normal", "Normal", vt::float3, VERTEX_NORMAL_CODE),
                                              in("power", "Power", vt::float1, math::vec4(DEFAULT_FRESNEL_POWER))},
                                             {out("out", "", vt::float1)},
                                             vt::float1,
                                             "mg_fresnel({normal}, i.view_direction, {power})"),
                                  "rim edge glow facing"));
    add_shape_nodes(nodes);
    auto random = make_width_dispatch("random",
                                      "Random",
                                      category::procedural,
                                      "A random number between Min and Max for each value of Seed: the same seed gives the "
                                      "same number on every GPU. Link a cell, a position or an id.",
                                      {in("seed", "Seed", vt::dynamic), in("min", "Min", vt::float1), in("max", "Max", vt::float1, math::vec4(1.0f))},
                                      {out("out", "", vt::float1)},
                                      vt::float1,
                                      {"mix({min}, {max}, mg_hash11({seed}))",
                                       "mix({min}, {max}, mg_hash12({seed}))",
                                       "mix({min}, {max}, mg_hash13({seed}))",
                                       "mix({min}, {max}, mg_hash14({seed}))"});
    nodes.push_back(with_keywords(random, "hash rand white noise seed"));
    auto sphere_mask = make_width_dispatch("sphere_mask",
                                           "Sphere Mask",
                                           category::procedural,
                                           "1 within Radius of Center, falling to 0 at Radius: Hardness 1 cuts it off, 0 "
                                           "fades it over the whole radius.",
                                           {in("position", "Position", vt::dynamic),
                                            in("center", "Center", vt::dynamic),
                                            in("radius", "Radius", vt::float1, math::vec4(1.0f)),
                                            in_unit("hardness", "Hardness", vt::float1, math::vec4(0.5f))},
                                           {out("out", "", vt::float1)},
                                           vt::float1,
                                           {"mg_sphere_mask(abs({position} - {center}), {radius}, {hardness})",
                                            "mg_sphere_mask(distance({position}, {center}), {radius}, {hardness})"});
    nodes.push_back(with_keywords(sphere_mask, "radial falloff circle distance mask"));
    auto wave = make_dynamic("wave",
                             "Wave",
                             category::procedural,
                             "A wave between 0 and 1 repeating Frequency times per unit of the input (the time while "
                             "unlinked): a sine, a square, a triangle or a sawtooth.",
                             {in_code("in", "In", vt::dynamic, TIME_CODE),
                              in("frequency", "Frequency", vt::dynamic, math::vec4(1.0f)),
                              in("phase", "Phase", vt::dynamic)},
                             "mg_wave_{shape}({in} * {frequency} + {phase})");
    wave.settings = {make_choice_setting("shape",
                                         "Shape",
                                         {"sine", "square", "triangle", "sawtooth"},
                                         {"Sine", "Square", "Triangle", "Sawtooth"},
                                         "sine",
                                         "The shape of each repeat.")};
    nodes.push_back(with_keywords(wave, "oscillate pulse sawtooth square triangle sine periodic"));
}

/// The setting naming a Named Reroute: the declaration's name, or the declaration a usage reads.
auto make_reroute_name_setting(const char* tooltip) -> material_setting_def
{
    material_setting_def name;
    name.name = "name";
    name.label = "Name";
    name.kind = material_setting_kind::text;
    name.tooltip = tooltip;
    return name;
}

/// Static Switch, Named Reroutes and Custom: what shapes the graph rather than computes.
void add_flow_nodes(std::vector<material_node_def>& nodes)
{
    auto static_switch = make_node("static_switch",
                                   "Static Switch",
                                   category::utility,
                                   "Passes True or False on, as Value picks when the material compiles: the other "
                                   "branch is not in the shader and costs nothing.");
    static_switch.code = material_node_code::static_switch;
    static_switch.type_rule = material_type_rule::chosen_input;
    static_switch.value_type = vt::dynamic;
    static_switch.inputs = {in("true", "True", vt::dynamic), in("false", "False", vt::dynamic)};
    static_switch.outputs = {out("out", "", vt::dynamic)};
    material_setting_def value;
    value.name = MATERIAL_STATIC_SWITCH_VALUE_SETTING;
    value.label = "Value";
    value.kind = material_setting_kind::choice;
    value.choices = {MATERIAL_STATIC_SWITCH_TRUE, MATERIAL_STATIC_SWITCH_FALSE};
    value.choice_labels = {"True", "False"};
    value.default_value = MATERIAL_STATIC_SWITCH_TRUE;
    value.tooltip = "Which input the material uses.";
    static_switch.settings = {value, make_parameter_name_setting(SWITCH_NAME_TOOLTIP), make_description_setting()};
    nodes.push_back(with_keywords(static_switch, "branch if toggle feature compile time bool"));
    auto quality = make_node("quality_switch",
                             "Quality Switch",
                             category::utility,
                             "Passes High on, or Low where the material is compiled for low quality (a deploy profile's "
                             "Material Quality: phones, the web): the other branch is not in the shader and costs nothing.");
    quality.code = material_node_code::static_switch;
    quality.type_rule = material_type_rule::chosen_input;
    quality.value_type = vt::dynamic;
    quality.inputs = {in("high", "High", vt::dynamic), in("low", "Low", vt::dynamic)};
    quality.outputs = {out("out", "", vt::dynamic)};
    nodes.push_back(with_keywords(quality, "quality level feature mobile web platform lod cheap switch"));
    auto declaration = make_node("named_reroute_declaration",
                                 "Named Reroute",
                                 category::utility,
                                 "Names its input, so Named Reroute Usage nodes anywhere in the graph read it without a "
                                 "link across the canvas.");
    declaration.code = material_node_code::named_reroute_declaration;
    declaration.type_rule = material_type_rule::dynamic;
    declaration.value_type = vt::dynamic;
    declaration.inputs = {in("in", "", vt::dynamic)};
    declaration.outputs = {out("out", "", vt::dynamic)};
    declaration.settings = {make_reroute_name_setting("The name usages read it by; empty: the node's id.")};
    nodes.push_back(with_keywords(declaration, "wire portal send declaration variable named"));
    auto usage = make_node("named_reroute",
                           "Named Reroute Usage",
                           category::utility,
                           "Reads the value of the Named Reroute that Name names.");
    usage.code = material_node_code::named_reroute;
    usage.type_rule = material_type_rule::dynamic;
    usage.value_type = vt::dynamic;
    auto hidden = in("in", "", vt::dynamic);
    hidden.is_hidden = true;
    usage.inputs = {hidden};
    usage.outputs = {out("out", "", vt::dynamic)};
    usage.settings = {make_reroute_name_setting("The Named Reroute it reads.")};
    nodes.push_back(with_keywords(usage, "wire portal receive usage variable named get"));
    auto custom = make_node("custom",
                            "Custom",
                            category::utility,
                            "Your own shader code: the body of a function of A, B, C and D (each as wide as what is "
                            "linked into it) and i, the MaterialInputs, that returns the Output type.");
    custom.code = material_node_code::custom;
    custom.type_rule = material_type_rule::custom;
    custom.value_type = vt::dynamic;
    custom.inputs = {in("a", "A", vt::dynamic), in("b", "B", vt::dynamic), in("c", "C", vt::dynamic), in("d", "D", vt::dynamic)};
    custom.outputs = {out("out", "", vt::dynamic)};
    material_setting_def output;
    output.name = "output";
    output.label = "Output";
    output.kind = material_setting_kind::choice;
    output.choices = {"float", "vec2", "vec3", "vec4"};
    output.choice_labels = {"Float", "Vector 2", "Vector 3", "Vector 4"};
    output.default_value = "vec3";
    output.tooltip = "The type the code returns.";
    material_setting_def code;
    code.name = "code";
    code.label = "Code";
    code.kind = material_setting_kind::code;
    code.default_value = "return vec3_splat(a);";
    code.tooltip = "The function's body, in bgfx shader language (GLSL names: vec3, mix, fract...). It reads a, b, c, d "
                   "and i (i.uv0, i.world_position, i.vertex_normal, i.view_direction, i.time...).";
    custom.settings = {output, code};
    nodes.push_back(with_keywords(custom, "code hlsl glsl expression script shader function"));
}

/// If, Compare and Branch, which choose per pixel, and Discard. A condition is a float: 1 true, 0 false.
void add_condition_nodes(std::vector<material_node_def>& nodes)
{
    auto choose = make_dynamic("if",
                               "If",
                               category::utility,
                               "One of three inputs, as A compares to B: A > B, A == B (within Equal Threshold) or A < B. "
                               "Every pixel computes all three.",
                               {in("a", "A", vt::float1),
                                in("b", "B", vt::float1),
                                in("greater", "A > B", vt::dynamic, math::vec4(1.0f)),
                                in("equal", "A == B", vt::dynamic, math::vec4(1.0f)),
                                in("less", "A < B", vt::dynamic),
                                in("threshold", "Equal Threshold", vt::float1, math::vec4(DEFAULT_IF_EQUAL_THRESHOLD))},
                               "(abs({a} - {b}) <= {threshold} ? {equal} : ({a} > {b} ? {greater} : {less}))");
    nodes.push_back(with_keywords(choose, "condition compare branch select greater less equal threshold"));
    auto compare = make_fixed("compare",
                              "Compare",
                              category::utility,
                              "1 where A and B compare as Operation says, else 0: a condition for Branch, or a mask to "
                              "multiply by.",
                              {in("a", "A", vt::float1), in("b", "B", vt::float1)},
                              {out("out", "", vt::float1)},
                              vt::float1,
                              "(({a} {operation} {b}) ? 1.0 : 0.0)");
    material_setting_def operation;
    operation.name = "operation";
    operation.label = "Operation";
    operation.kind = material_setting_kind::choice;
    operation.choices = {">", ">=", "<", "<=", "==", "!="};
    operation.default_value = ">";
    operation.tooltip = "How A compares to B for the result to be 1. == and != compare exactly: If takes a threshold.";
    compare.settings = {operation};
    nodes.push_back(with_keywords(compare, "condition test greater less equal boolean comparison"));
    const std::string branch_expression = "({condition} > " + std::to_string(BRANCH_CONDITION_THRESHOLD) + " ? {true} : {false})";
    auto branch = make_dynamic("branch",
                               "Branch",
                               category::utility,
                               "True where Condition is above 0.5, else False: per pixel, so both inputs are computed. "
                               "Static Switch picks one when the material compiles.",
                               {in("condition", "Condition", vt::float1),
                                in("true", "True", vt::dynamic, math::vec4(1.0f)),
                                in("false", "False", vt::dynamic)},
                               branch_expression.c_str());
    nodes.push_back(with_keywords(branch, "condition select if switch boolean"));
    auto discard = make_node("discard",
                             "Discard",
                             category::utility,
                             "Pixels where Value is below Threshold are not drawn, in the material's shadow neither: a "
                             "hard cut-out anywhere in the graph. It needs no link: every Discard node acts.");
    discard.code = material_node_code::discard;
    discard.inputs = {in("value", "Value", vt::float1, math::vec4(1.0f)),
                      in_unit("threshold", "Threshold", vt::float1, math::vec4(DEFAULT_OPACITY_MASK_CLIP))};
    nodes.push_back(with_keywords(discard, "clip kill cut out alpha test hole"));
    nodes.push_back(with_keywords(make_fixed("and",
                                             "And",
                                             category::utility,
                                             "1 where A and B are both true (above 0.5), else 0.",
                                             {in("a", "A", vt::float1), in("b", "B", vt::float1)},
                                             {out("out", "", vt::float1)},
                                             vt::float1,
                                             "(({a} > 0.5 && {b} > 0.5) ? 1.0 : 0.0)"),
                                  "logic boolean both &&"));
    nodes.push_back(with_keywords(make_fixed("or",
                                             "Or",
                                             category::utility,
                                             "1 where A or B is true (above 0.5), else 0.",
                                             {in("a", "A", vt::float1), in("b", "B", vt::float1)},
                                             {out("out", "", vt::float1)},
                                             vt::float1,
                                             "(({a} > 0.5 || {b} > 0.5) ? 1.0 : 0.0)"),
                                  "logic boolean either ||"));
    nodes.push_back(with_keywords(make_fixed("not",
                                             "Not",
                                             category::utility,
                                             "1 where the value is false (0.5 or less), else 0.",
                                             {in("in", "", vt::float1)},
                                             {out("out", "", vt::float1)},
                                             vt::float1,
                                             "({in} > 0.5 ? 0.0 : 1.0)"),
                                  "logic boolean negate invert !"));
    nodes.push_back(with_keywords(make_dynamic("select",
                                               "Select",
                                               category::utility,
                                               "One of four inputs, by Index rounded to the nearest whole number (0 to 3). "
                                               "Every pixel computes all four.",
                                               {in("index", "Index", vt::float1),
                                                in("in0", "0", vt::dynamic),
                                                in("in1", "1", vt::dynamic),
                                                in("in2", "2", vt::dynamic),
                                                in("in3", "3", vt::dynamic)},
                                               "({index} < 0.5 ? {in0} : ({index} < 1.5 ? {in1} : ({index} < 2.5 ? {in2} : {in3})))"),
                                  "switch choose case index pick"));
}

void add_utility_nodes(std::vector<material_node_def>& nodes)
{
    nodes.push_back(with_shortcut(make_fixed("panner",
                                             "Panner",
                                             category::uv,
                                             "Moves UVs over time at Speed (UV units per second).",
                                             {in_code("uv", "UV", vt::float2, UV_CODE),
                                              in("speed", "Speed", vt::float2, math::vec4(DEFAULT_PANNER_SPEED, 0.0f, 0.0f, 0.0f)),
                                              in_code("time", "Time", vt::float1, TIME_CODE)},
                                             {out("uv", "UV", vt::float2)},
                                             vt::float2,
                                             "({uv} + {speed} * {time})"),
                                  'P',
                                  "scroll move flow animate"));
    nodes.push_back(with_keywords(make_fixed("tiling_offset",
                                             "Tiling And Offset",
                                             category::uv,
                                             "UVs scaled by Tiling, then moved by Offset.",
                                             {in_code("uv", "UV", vt::float2, UV_CODE),
                                              in("tiling", "Tiling", vt::float2, math::vec4(1.0f)),
                                              in("offset", "Offset", vt::float2)},
                                             {out("uv", "UV", vt::float2)},
                                             vt::float2,
                                             "({uv} * {tiling} + {offset})"),
                                  "tile repeat scale uv"));
    nodes.push_back(with_keywords(make_fixed("rotate_uv",
                                             "Rotate UV",
                                             category::uv,
                                             "UVs turned around Center by Angle (radians).",
                                             {in_code("uv", "UV", vt::float2, UV_CODE),
                                              in("center", "Center", vt::float2, math::vec4(0.5f)),
                                              in("angle", "Angle", vt::float1)},
                                             {out("uv", "UV", vt::float2)},
                                             vt::float2,
                                             "mg_rotate_uv({uv}, {center}, {angle})"),
                                  "rotator spin turn"));
    nodes.push_back(with_keywords(make_fixed("desaturate",
                                             "Desaturate",
                                             category::color,
                                             "Moves a colour toward its grey by Amount (1: fully grey).",
                                             {in("color", "Color", vt::float3, math::vec4(1.0f)), in("amount", "Amount", vt::float1, math::vec4(1.0f))},
                                             {out("rgb", "", vt::float3)},
                                             vt::float3,
                                             "mg_desaturate({color}, {amount})"),
                                  "grayscale greyscale luminance"));
    auto reroute = make_node("reroute", "Reroute", category::utility, "A bend point that passes its input on unchanged.");
    reroute.code = material_node_code::reroute;
    reroute.type_rule = material_type_rule::dynamic;
    reroute.value_type = vt::dynamic;
    reroute.inputs = {in("in", "", vt::dynamic)};
    reroute.outputs = {out("out", "", vt::dynamic)};
    nodes.push_back(with_keywords(reroute, "knot wire bend"));
    add_flow_nodes(nodes);
    add_condition_nodes(nodes);
}

/// Function Input, Function Output and the call of a function (material_graph.h, material functions).
void add_function_nodes(std::vector<material_node_def>& nodes)
{
    material_setting_def name;
    name.name = MATERIAL_FUNCTION_NAME_SETTING;
    name.label = "Name";
    name.kind = material_setting_kind::text;
    name.tooltip = "The name the caller's pin shows; empty: the node's id.";
    auto input = make_node(MATERIAL_FUNCTION_INPUT_TYPE,
                           "Function Input",
                           category::function,
                           "An input of a material function: what the caller links into its pin, else Default. Inputs "
                           "are the caller's pins in the graph's order.");
    input.code = material_node_code::function_input;
    input.type_rule = material_type_rule::declared;
    input.value_type = vt::dynamic;
    auto caller = in("in", "", vt::dynamic);
    caller.is_hidden = true;
    input.inputs = {caller, in_value("default", "Default", vt::float4)};
    input.outputs = {out("out", "", vt::dynamic)};
    material_setting_def type;
    type.name = "type";
    type.label = "Type";
    type.kind = material_setting_kind::choice;
    type.choices = {"float", "vec2", "vec3", "vec4", "sampler2D", "samplerCube", "MaterialSurface"};
    type.choice_labels = {"Float", "Vector 2", "Vector 3", "Vector 4", "Texture 2D", "Texture Cube", "Material Attributes"};
    type.default_value = "vec3";
    type.tooltip = "The value the caller's pin takes. A texture input the caller links nothing into reads the node's "
                   "own texture; an attributes input, the default surface.";
    input.settings = {name, type};
    nodes.push_back(with_keywords(input, "function parameter argument"));
    auto output = make_node(MATERIAL_FUNCTION_OUTPUT_TYPE,
                            "Function Output",
                            category::function,
                            "An output of a material function: its input, given to the caller's pin. Outputs are the "
                            "caller's pins in the graph's order.");
    output.code = material_node_code::function_output;
    output.type_rule = material_type_rule::dynamic;
    output.value_type = vt::dynamic;
    output.inputs = {in("in", "", vt::dynamic)};
    auto result = out("out", "", vt::dynamic);
    result.is_hidden = true;
    output.outputs = {result};
    output.settings = {name};
    nodes.push_back(with_keywords(output, "function return result"));
    auto call = make_node(MATERIAL_FUNCTION_CALL_TYPE,
                          "Material Function",
                          category::function,
                          "A material function (a .mgraph of Function Input and Output nodes) in place: its inputs and "
                          "outputs are this node's pins.");
    call.code = material_node_code::function_call;
    call.type_rule = material_type_rule::function_call;
    call.value_type = vt::dynamic;
    for(uint32_t pin = 0; pin < MATERIAL_FUNCTION_MAX_INPUTS; ++pin)
    {
        const std::string pin_name = get_material_function_input_pin(pin);
        call.inputs.push_back(in("", "", vt::dynamic));
        call.inputs.back().name = pin_name;
    }
    for(uint32_t pin = 0; pin < MATERIAL_FUNCTION_MAX_OUTPUTS; ++pin)
    {
        auto hidden = in("", "", vt::dynamic);
        hidden.name = get_material_function_result_pin(pin);
        hidden.is_hidden = true;
        call.inputs.push_back(hidden);
        call.outputs.push_back(out("", "", vt::dynamic));
        call.outputs.back().name = get_material_function_output_pin(pin);
    }
    material_setting_def function;
    function.name = MATERIAL_FUNCTION_KEY_SETTING;
    function.label = "Function";
    function.kind = material_setting_kind::text;
    function.tooltip = "The material function's file: app:/data/<folder>/<name>.mgraph.";
    call.settings = {function};
    nodes.push_back(with_keywords(call, "function subgraph call reuse"));
}

/// The object's transform, the Transform node and the Vertex Interpolator: what reads the graph's vertex stage.
void add_object_nodes(std::vector<material_node_def>& nodes)
{
    nodes.push_back(as_inline(with_keywords(make_fixed("object_position",
                                                       "Object Position",
                                                       category::input,
                                                       "The pivot of the object drawn, in world space: the same for every "
                                                       "pixel and vertex of it. For wind or variation around each object's "
                                                       "own pivot.",
                                                       {},
                                                       {out("xyz", "XYZ", vt::float3)},
                                                       vt::float3,
                                                       "i.object_position"),
                                            "pivot origin actor instance location")));
    nodes.push_back(with_keywords(make_fixed("object_scale",
                                             "Object Scale",
                                             category::input,
                                             "How much the object drawn is scaled along each of its axes.",
                                             {},
                                             {out("xyz", "XYZ", vt::float3), out("x", "X", vt::float1, ".x"), out("y", "Y", vt::float1, ".y"), out("z", "Z", vt::float1, ".z")},
                                             vt::float3,
                                             "mg_object_scale(i)"),
                                  "size bounds instance"));
    nodes.push_back(with_keywords(make_fixed("object_random",
                                             "Per Instance Random",
                                             category::input,
                                             "A random number between 0 and 1 for each object drawn, from its pivot: copies "
                                             "of one model, instanced or not, each get their own tint or phase. An object "
                                             "that moves takes new numbers as it moves.",
                                             {},
                                             {out("out", "", vt::float1)},
                                             vt::float1,
                                             "mg_hash13(i.object_position)"),
                                  "instance random variation seed object id"));
    auto transform = make_fixed("transform",
                                "Transform",
                                category::vector,
                                "A direction, position or normal moved from one space to another: tangent, object, world "
                                "or view (the camera's).",
                                {in("in", "", vt::float3)},
                                {out("out", "", vt::float3)},
                                vt::float3,
                                "mg_world_to_{to}_{kind}(i, mg_{from}_to_world_{kind}(i, {in}))");
    const std::vector<std::string> spaces{"tangent", "object", "world", "view"};
    const std::vector<std::string> space_labels{"Tangent", "Object", "World", "View"};
    transform.settings = {make_choice_setting("from", "From", spaces, space_labels, "tangent", "The space the input is in."),
                          make_choice_setting("to", "To", spaces, space_labels, "world", "The space the output is in."),
                          make_choice_setting("kind",
                                              "Kind",
                                              {"direction", "position", "normal"},
                                              {"Direction", "Position", "Normal"},
                                              "direction",
                                              "Direction: turned and scaled, never moved. Position: moved too (a tangent "
                                              "space position is a direction). Normal: kept at right angles to the "
                                              "surface under a squashing scale, unit length.")};
    nodes.push_back(with_keywords(transform, "space convert tangent object local world view camera matrix"));
    auto interpolator = make_node("vertex_interpolator",
                                  "Vertex Interpolator",
                                  category::utility,
                                  "Computes its input once per vertex and blends it across the triangle for the pixels: "
                                  "cheaper than per pixel for what varies smoothly. A graph reads at most four.");
    interpolator.code = material_node_code::vertex_interpolator;
    interpolator.type_rule = material_type_rule::dynamic;
    interpolator.value_type = vt::dynamic;
    interpolator.inputs = {in("in", "", vt::dynamic)};
    interpolator.outputs = {out("out", "", vt::dynamic)};
    nodes.push_back(with_keywords(interpolator, "varying vertex per vertex interpolate custom interpolator"));
}

/// Polar Coordinates, Twirl, Spherize, Flipbook and Bump Offset: UVs reshaped.
void add_uv_nodes(std::vector<material_node_def>& nodes)
{
    nodes.push_back(with_keywords(make_fixed("polar_coordinates",
                                             "Polar Coordinates",
                                             category::uv,
                                             "The UV around Center as distance (X, 1 at half a UV unit, times Radial Scale) "
                                             "and angle (Y, 0 to 1 once around, times Angle Scale): radial and spiral patterns.",
                                             {in_code("uv", "UV", vt::float2, UV_CODE),
                                              in("center", "Center", vt::float2, math::vec4(0.5f)),
                                              in("radial_scale", "Radial Scale", vt::float1, math::vec4(1.0f)),
                                              in("angle_scale", "Angle Scale", vt::float1, math::vec4(1.0f))},
                                             {out("uv", "UV", vt::float2), out("distance", "Distance", vt::float1, ".x"), out("angle", "Angle", vt::float1, ".y")},
                                             vt::float2,
                                             "mg_polar_coordinates({uv}, {center}, {radial_scale}, {angle_scale})"),
                                  "radial angle circular spiral"));
    nodes.push_back(with_keywords(make_fixed("twirl",
                                             "Twirl",
                                             category::uv,
                                             "The UV turned around Center by Strength radians per UV unit away from it: a "
                                             "whirlpool. Then moved by Offset.",
                                             {in_code("uv", "UV", vt::float2, UV_CODE),
                                              in("center", "Center", vt::float2, math::vec4(0.5f)),
                                              in("strength", "Strength", vt::float1, math::vec4(10.0f)),
                                              in("offset", "Offset", vt::float2)},
                                             {out("uv", "UV", vt::float2)},
                                             vt::float2,
                                             "mg_twirl({uv}, {center}, {strength}, {offset})"),
                                  "swirl whirl vortex spiral"));
    nodes.push_back(with_keywords(make_fixed("spherize",
                                             "Spherize",
                                             category::uv,
                                             "The UV bulged around Center as if wrapped on a sphere, by Strength. Then moved "
                                             "by Offset.",
                                             {in_code("uv", "UV", vt::float2, UV_CODE),
                                              in("center", "Center", vt::float2, math::vec4(0.5f)),
                                              in("strength", "Strength", vt::float1, math::vec4(10.0f)),
                                              in("offset", "Offset", vt::float2)},
                                             {out("uv", "UV", vt::float2)},
                                             vt::float2,
                                             "mg_spherize({uv}, {center}, {strength}, {offset})"),
                                  "bulge fisheye lens sphere"));
    auto flipbook = make_fixed("flipbook",
                               "Flipbook",
                               category::uv,
                               "The UV of one frame of a sheet of Columns by Rows frames, read left to right: link the time "
                               "times a frame rate into Frame to play it. The frame wraps around the sheet.",
                               {in_code("uv", "UV", vt::float2, UV_CODE),
                                in("columns", "Columns", vt::float1, math::vec4(4.0f)),
                                in("rows", "Rows", vt::float1, math::vec4(4.0f)),
                                in("frame", "Frame", vt::float1)},
                               {out("uv", "UV", vt::float2)},
                               vt::float2,
                               "mg_flipbook({uv}, {columns}, {rows}, {frame}, mg_flipbook_from_{start})");
    flipbook.settings = {make_choice_setting("start",
                                             "Start",
                                             {"top", "bottom"},
                                             {"Top Row", "Bottom Row"},
                                             "top",
                                             "Which row of the sheet the first frame is on.")};
    nodes.push_back(with_keywords(flipbook, "sprite sheet atlas animation frames subuv"));
    nodes.push_back(with_keywords(make_fixed("bump_offset",
                                             "Bump Offset",
                                             category::uv,
                                             "The UV moved along the view by Height above Reference Plane, times Ratio: a "
                                             "cheap parallax that gives a height map depth. Link the shifted UV into the "
                                             "material's texture reads.",
                                             {in_code("uv", "UV", vt::float2, UV_CODE),
                                              in("height", "Height", vt::float1, math::vec4(0.5f)),
                                              in("ratio", "Ratio", vt::float1, math::vec4(0.05f)),
                                              in_unit("reference_plane", "Reference Plane", vt::float1, math::vec4(0.5f))},
                                             {out("uv", "UV", vt::float2)},
                                             vt::float2,
                                             "mg_bump_offset({uv}, {height}, {ratio}, {reference_plane}, "
                                             "mg_tangent_view_direction(i.view_direction, i.tangent, i.bitangent, i.vertex_normal))"),
                                  "parallax height offset depth"));
}

/// Rotate About Axis and the normal tools.
void add_more_vector_nodes(std::vector<material_node_def>& nodes)
{
    auto rotate = make_fixed("rotate_about_axis",
                             "Rotate About Axis",
                             category::vector,
                             "Position turned by Angle (radians) around Axis through Pivot. Result Offset gives the move, for "
                             "World Position Offset (wind, spinning); Position gives the turned position.",
                             {in_code("position", "Position", vt::float3, WORLD_POSITION_CODE),
                              in("axis", "Axis", vt::float3, math::vec4(0.0f, 1.0f, 0.0f, 0.0f)),
                              in("angle", "Angle", vt::float1),
                              in("pivot", "Pivot", vt::float3)},
                             {out("out", "", vt::float3)},
                             vt::float3,
                             "mg_rotate_about_axis_{result}({position}, {axis}, {angle}, {pivot})");
    rotate.settings = {make_choice_setting("result",
                                           "Result",
                                           {"offset", "position"},
                                           {"Offset", "Position"},
                                           "offset",
                                           "Offset: the turned position minus Position, what World Position Offset takes. "
                                           "Position: the turned position.")};
    nodes.push_back(with_keywords(rotate, "rotator rotation wind spin axis angle"));
    auto blend = make_fixed("blend_normals",
                            "Blend Normals",
                            category::vector,
                            "Detail laid over Base, both tangent-space normals: a detail normal map over the surface's own.",
                            {in("base", "Base", vt::float3, math::vec4(0.0f, 0.0f, 1.0f, 0.0f)), in("detail", "Detail", vt::float3, math::vec4(0.0f, 0.0f, 1.0f, 0.0f))},
                            {out("normal", "Normal", vt::float3)},
                            vt::float3,
                            "mg_blend_normals_{mode}({base}, {detail})");
    blend.settings = {make_choice_setting("mode",
                                          "Mode",
                                          {"reoriented", "whiteout", "linear"},
                                          {"Reoriented", "Whiteout", "Linear"},
                                          "reoriented",
                                          "Reoriented: the detail turned as the base turns the surface, the most faithful. "
                                          "Whiteout: the slopes added, cheaper. Linear: the two averaged, flattens both.")};
    nodes.push_back(with_keywords(blend, "detail normal rnm combine layer"));
    nodes.push_back(as_pixel_only(with_keywords(make_fixed("normal_from_height",
                                                           "Normal From Height",
                                                           category::vector,
                                                           "The tangent-space normal of the surface raised by Height (in "
                                                           "world units, times Strength): bumps from a height map or a "
                                                           "procedural pattern. The pixel stage only.",
                                                           {in("height", "Height", vt::float1), in("strength", "Strength", vt::float1, math::vec4(1.0f))},
                                                           {out("normal", "Normal", vt::float3)},
                                                           vt::float3,
                                                           "mg_normal_from_height({height}, {strength}, i.world_position, i.vertex_normal, "
                                                           "i.tangent, i.bitangent)"),
                                                "bump height map derivative")));
    nodes.push_back(with_keywords(make_fixed("normal_strength",
                                             "Normal Strength",
                                             category::vector,
                                             "A tangent-space normal's slopes scaled by Strength: 0 flattens it, above 1 deepens it.",
                                             {in("normal", "Normal", vt::float3, math::vec4(0.0f, 0.0f, 1.0f, 0.0f)), in("strength", "Strength", vt::float1, math::vec4(1.0f))},
                                             {out("normal", "Normal", vt::float3)},
                                             vt::float3,
                                             "mg_normal_strength({normal}, {strength})"),
                                  "flatten intensity bumpiness"));
    nodes.push_back(with_keywords(make_fixed("normal_reconstruct_z",
                                             "Normal Reconstruct Z",
                                             category::vector,
                                             "A unit tangent-space normal from its X and Y, as two-channel normal maps store it.",
                                             {in("xy", "XY", vt::float2)},
                                             {out("normal", "Normal", vt::float3)},
                                             vt::float3,
                                             "mg_normal_reconstruct_z({xy})"),
                                  "derive z two channel bc5"));
}

/// The stops of a Color Ramp: hidden inputs the editor's ramp edits.
void add_color_ramp_pins(material_node_def& def)
{
    for(uint32_t stop = 0; stop < MATERIAL_COLOR_RAMP_MAX_STOPS; ++stop)
    {
        const float position = stop == 0 ? 0.0f : 1.0f;
        const float shade = stop == 0 ? 0.0f : 1.0f;
        auto color = in_value("", "", vt::float4, math::vec4(shade, shade, shade, 1.0f), true);
        color.name = get_material_color_ramp_color_pin(stop);
        color.label = "Color " + std::to_string(stop + 1);
        color.is_hidden = true;
        auto place = in_value("", "", vt::float1, math::vec4(position));
        place.name = get_material_color_ramp_position_pin(stop);
        place.label = "Position " + std::to_string(stop + 1);
        place.is_hidden = true;
        def.inputs.push_back(color);
        def.inputs.push_back(place);
    }
}

void add_color_nodes(std::vector<material_node_def>& nodes)
{
    nodes.push_back(with_keywords(make_fixed("rgb_to_hsv",
                                             "RGB To HSV",
                                             category::color,
                                             "A colour as hue (0 to 1 around the colour wheel), saturation and value.",
                                             {in("in", "", vt::float3)},
                                             {out("hsv", "HSV", vt::float3), out("h", "H", vt::float1, ".x"), out("s", "S", vt::float1, ".y"), out("v", "V", vt::float1, ".z")},
                                             vt::float3,
                                             "mg_rgb_to_hsv({in})"),
                                  "hue saturation value convert"));
    nodes.push_back(with_keywords(make_fixed("hsv_to_rgb",
                                             "HSV To RGB",
                                             category::color,
                                             "The colour of a hue (0 to 1 around the colour wheel), saturation and value.",
                                             {in("in", "", vt::float3)},
                                             {out("rgb", "RGB", vt::float3)},
                                             vt::float3,
                                             "mg_hsv_to_rgb({in})"),
                                  "hue saturation value convert rainbow"));
    nodes.push_back(with_keywords(make_fixed("hue_saturation_value",
                                             "Hue Saturation Value",
                                             category::color,
                                             "A colour with its hue turned by Hue Shift (1: once around), its saturation and "
                                             "its value scaled.",
                                             {in_color("color", "Color", vt::float3, math::vec4(1.0f)),
                                              in("hue_shift", "Hue Shift", vt::float1),
                                              in("saturation", "Saturation", vt::float1, math::vec4(1.0f)),
                                              in("value", "Value", vt::float1, math::vec4(1.0f))},
                                             {out("rgb", "", vt::float3)},
                                             vt::float3,
                                             "mg_hsv_adjust({color}, {hue_shift}, {saturation}, {value})"),
                                  "hue shift saturation brightness adjust tint"));
    nodes.push_back(with_keywords(make_fixed("contrast",
                                             "Contrast",
                                             category::color,
                                             "A colour's distance from Pivot scaled by Contrast: above 1 sharpens light against "
                                             "dark, below 1 flattens it. The pivot is mid grey in linear colour.",
                                             {in_color("color", "Color", vt::float3, math::vec4(1.0f)),
                                              in("contrast", "Contrast", vt::float1, math::vec4(1.0f)),
                                              in("pivot", "Pivot", vt::float1, math::vec4(0.2176f))},
                                             {out("rgb", "", vt::float3)},
                                             vt::float3,
                                             "max(({color} - {pivot}) * {contrast} + {pivot}, vec3_splat(0.0))"),
                                  "levels punch"));
    nodes.push_back(with_keywords(make_dynamic("posterize",
                                               "Posterize",
                                               category::color,
                                               "The value cut into Steps flat bands: a toon or retro look.",
                                               {in("in", "", vt::dynamic), in("steps", "Steps", vt::float1, math::vec4(4.0f))},
                                               "(floor({in} * {steps}) / {steps})"),
                                  "quantize bands toon cel steps"));
    auto blend = make_fixed("blend",
                            "Blend",
                            category::color,
                            "Blend laid over Base by Mode, as image editors combine layers, then mixed in by Opacity.",
                            {in_color("base", "Base", vt::float3, math::vec4(0.5f)),
                             in_color("blend", "Blend", vt::float3, math::vec4(0.5f)),
                             in_unit("opacity", "Opacity", vt::float1, math::vec4(1.0f))},
                            {out("rgb", "", vt::float3)},
                            vt::float3,
                            "mix({base}, mg_blend_{mode}({base}, {blend}), {opacity})");
    blend.settings = {make_choice_setting("mode",
                                          "Mode",
                                          {"normal",
                                           "multiply",
                                           "screen",
                                           "overlay",
                                           "soft_light",
                                           "hard_light",
                                           "add",
                                           "subtract",
                                           "difference",
                                           "darken",
                                           "lighten",
                                           "color_dodge",
                                           "color_burn",
                                           "linear_light"},
                                          {"Normal",
                                           "Multiply",
                                           "Screen",
                                           "Overlay",
                                           "Soft Light",
                                           "Hard Light",
                                           "Add",
                                           "Subtract",
                                           "Difference",
                                           "Darken",
                                           "Lighten",
                                           "Color Dodge",
                                           "Color Burn",
                                           "Linear Light"},
                                          "overlay",
                                          "How Blend combines with Base.")};
    nodes.push_back(with_keywords(blend, "blend mode overlay screen multiply soft light layer photoshop"));
    nodes.push_back(with_keywords(make_fixed("luminance",
                                             "Luminance",
                                             category::color,
                                             "How bright a linear colour looks, as one number (Rec. 709 weights).",
                                             {in_color("color", "Color", vt::float3, math::vec4(1.0f))},
                                             {out("out", "", vt::float1)},
                                             vt::float1,
                                             "mg_luminance({color})"),
                                  "brightness grey intensity luma"));
    auto space = make_fixed("color_space",
                            "Color Space Conversion",
                            category::color,
                            "A colour moved between sRGB and linear: for colours read from a texture or a value in the "
                            "other space.",
                            {in_color("in", "", vt::float3, math::vec4(1.0f))},
                            {out("rgb", "", vt::float3)},
                            vt::float3,
                            "mg_{conversion}({in})");
    space.settings = {make_choice_setting("conversion",
                                          "Conversion",
                                          {"srgb_to_linear", "linear_to_srgb"},
                                          {"sRGB To Linear", "Linear To sRGB"},
                                          "srgb_to_linear",
                                          "The direction of the conversion.")};
    nodes.push_back(with_keywords(space, "gamma srgb linear convert"));
    auto ramp = make_node("color_ramp",
                          "Color Ramp",
                          category::color,
                          "The colour of a gradient at Value (0 to 1): stops placed along it, blended as Interpolation "
                          "says. Edit the stops in the node's details.");
    ramp.code = material_node_code::color_ramp;
    ramp.value_type = vt::float4;
    ramp.inputs = {in("value", "Value", vt::float1, math::vec4(0.5f))};
    add_color_ramp_pins(ramp);
    ramp.outputs = color_outputs();
    material_setting_def count;
    count.name = MATERIAL_COLOR_RAMP_COUNT_SETTING;
    count.label = "Stops";
    count.kind = material_setting_kind::number;
    count.default_value = "2";
    count.tooltip = "How many stops the ramp has, 2 to 8.";
    ramp.settings = {count,
                     make_choice_setting(MATERIAL_COLOR_RAMP_INTERPOLATION_SETTING,
                                         "Interpolation",
                                         {"linear", "smooth", "constant"},
                                         {"Linear", "Smooth", "Constant"},
                                         "linear",
                                         "Linear: straight blends between stops. Smooth: eased blends. Constant: each stop's "
                                         "colour until the next, in hard bands.")};
    nodes.push_back(with_keywords(ramp, "gradient color ramp curve lut stylize toon map"));
}

auto build_library() -> std::vector<material_node_def>
{
    std::vector<material_node_def> nodes;
    nodes.push_back(make_output_node());
    add_input_nodes(nodes);
    add_constant_nodes(nodes);
    add_parameter_nodes(nodes);
    add_texture_nodes(nodes);
    add_math_nodes(nodes);
    add_vector_nodes(nodes);
    add_more_vector_nodes(nodes);
    add_object_nodes(nodes);
    add_attribute_nodes(nodes);
    add_color_nodes(nodes);
    add_uv_nodes(nodes);
    add_procedural_nodes(nodes);
    add_utility_nodes(nodes);
    add_function_nodes(nodes);
    // The editor lists the nodes by category, each category's together, in this order.
    constexpr std::array<category, 12> order = {category::output,
                                                category::input,
                                                category::constant,
                                                category::parameter,
                                                category::texture,
                                                category::math,
                                                category::vector,
                                                category::color,
                                                category::uv,
                                                category::procedural,
                                                category::utility,
                                                category::function};
    const auto rank = [&order](category group)
    {
        return std::distance(order.begin(), std::find(order.begin(), order.end(), group));
    };
    std::stable_sort(nodes.begin(),
                     nodes.end(),
                     [&rank](const material_node_def& a, const material_node_def& b)
                     {
                         return rank(a.category) < rank(b.category);
                     });
    return nodes;
}
} // namespace

auto get_material_value_width(material_value_type type) -> uint32_t
{
    switch(type)
    {
        case material_value_type::float2:
            return 2;
        case material_value_type::float3:
            return 3;
        case material_value_type::float4:
            return 4;
        case material_value_type::float1:
        case material_value_type::dynamic:
        case material_value_type::texture2d:
        case material_value_type::texture_cube:
        case material_value_type::attributes:
            break;
    }
    return 1;
}

auto is_material_value_numeric(material_value_type type) -> bool
{
    return type != material_value_type::texture2d && type != material_value_type::texture_cube && type != material_value_type::attributes;
}

auto is_material_texture_type(material_value_type type) -> bool
{
    return type == material_value_type::texture2d || type == material_value_type::texture_cube;
}

auto get_material_value_type(uint32_t width) -> material_value_type
{
    switch(width)
    {
        case 2:
            return material_value_type::float2;
        case 3:
            return material_value_type::float3;
        case 4:
            return material_value_type::float4;
        default:
            break;
    }
    return material_value_type::float1;
}

auto get_material_value_shader_type(material_value_type type) -> const char*
{
    switch(type)
    {
        case material_value_type::texture2d:
            return "sampler2D";
        case material_value_type::texture_cube:
            return "samplerCube";
        case material_value_type::attributes:
            return "MaterialSurface";
        default:
            break;
    }
    switch(get_material_value_width(type))
    {
        case 2:
            return "vec2";
        case 3:
            return "vec3";
        case 4:
            return "vec4";
        default:
            break;
    }
    return "float";
}

auto get_material_value_type_name(material_value_type type) -> const char*
{
    switch(type)
    {
        case material_value_type::float1:
            return "Float";
        case material_value_type::float2:
            return "Vector 2";
        case material_value_type::float3:
            return "Vector 3";
        case material_value_type::float4:
            return "Vector 4";
        case material_value_type::texture2d:
            return "Texture 2D";
        case material_value_type::texture_cube:
            return "Texture Cube";
        case material_value_type::attributes:
            return "Material Attributes";
        case material_value_type::dynamic:
            break;
    }
    return "Dynamic";
}

auto get_material_node_category_name(material_node_category category) -> const char*
{
    switch(category)
    {
        case material_node_category::output:
            return "Output";
        case material_node_category::input:
            return "Inputs";
        case material_node_category::constant:
            return "Constants";
        case material_node_category::parameter:
            return "Parameters";
        case material_node_category::texture:
            return "Textures";
        case material_node_category::math:
            return "Math";
        case material_node_category::vector:
            return "Vector";
        case material_node_category::procedural:
            return "Procedural";
        case material_node_category::function:
            return "Functions";
        case material_node_category::color:
            return "Color";
        case material_node_category::uv:
            return "UV";
        case material_node_category::utility:
            break;
    }
    return "Utility";
}

auto get_material_node_library() -> const std::vector<material_node_def>&
{
    static const std::vector<material_node_def> library = build_library();
    return library;
}

auto find_material_node_def(hpp::string_view type) -> const material_node_def*
{
    const auto& library = get_material_node_library();
    const auto it = std::find_if(library.begin(),
                                 library.end(),
                                 [&](const material_node_def& def)
                                 {
                                     return def.type == type;
                                 });
    return it == library.end() ? nullptr : &*it;
}

auto find_material_pin_def(const std::vector<material_pin_def>& pins, hpp::string_view name) -> const material_pin_def*
{
    const auto it = std::find_if(pins.begin(),
                                 pins.end(),
                                 [&](const material_pin_def& pin)
                                 {
                                     return pin.name == name;
                                 });
    return it == pins.end() ? nullptr : &*it;
}

auto is_material_texture_node(const material_node_def& def) -> bool
{
    return def.code == material_node_code::texture_sample || def.code == material_node_code::normal_map ||
           def.code == material_node_code::triplanar_sample || def.code == material_node_code::texture_object ||
           def.code == material_node_code::parallax_occlusion || def.code == material_node_code::normal_from_texture;
}

auto get_material_texture_node_kind(const material_node_def& def) -> material_value_type
{
    const auto* texture = find_material_pin_def(def.inputs, MATERIAL_TEXTURE_PIN);
    if(texture != nullptr)
    {
        return texture->type;
    }
    return def.outputs.empty() ? material_value_type::texture2d : def.outputs.front().type;
}

auto can_split_material_pin(const material_pin_def& pin) -> bool
{
    return !pin.is_value_only && is_material_value_numeric(pin.type) && pin.type != material_value_type::dynamic &&
           get_material_value_width(pin.type) > 1;
}

auto get_material_component_letters(const material_pin_def& pin) -> const char*
{
    const bool is_color = pin.is_color || pin.name.rfind("rgb", 0) == 0;
    return is_color ? "rgba" : "xyzw";
}

auto find_material_setting_def(const material_node_def& def, hpp::string_view name) -> const material_setting_def*
{
    const auto it = std::find_if(def.settings.begin(),
                                 def.settings.end(),
                                 [&](const material_setting_def& setting)
                                 {
                                     return setting.name == name;
                                 });
    return it == def.settings.end() ? nullptr : &*it;
}

auto get_material_setting_choice_label(const material_setting_def& setting, const std::string& value) -> const std::string&
{
    const auto it = std::find(setting.choices.begin(), setting.choices.end(), value);
    const auto index = size_t(std::distance(setting.choices.begin(), it));
    return index < setting.choice_labels.size() ? setting.choice_labels[index] : value;
}

} // namespace unravel
