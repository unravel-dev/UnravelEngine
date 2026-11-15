#include "material_graph_library.h"

#include "material_graph.h"

#include <algorithm>

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

/// A math function of one value, applied to each component.
auto make_unary(const char* type, const char* label, const char* description, const char* expression)
    -> material_node_def
{
    return make_dynamic(type, label, category::math, description, {in("in", "", vt::dynamic)}, expression);
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

void add_texture_nodes(std::vector<material_node_def>& nodes)
{
    auto sample = make_node("texture_sample",
                            "Texture Sample",
                            category::texture,
                            "Reads a texture at the given UV (the mesh's own while unlinked).");
    sample.code = material_node_code::texture_sample;
    sample.value_type = vt::float4;
    sample.inputs = {in_code("uv", "UV", vt::float2, UV_CODE)};
    sample.outputs = texel_outputs();
    sample.settings = {make_parameter_name_setting(TEXTURE_NAME_TOOLTIP), make_description_setting()};
    nodes.push_back(with_shortcut(sample, 'T', "texture sampler image map albedo"));
    auto normal = make_node("normal_map",
                            "Normal Map",
                            category::texture,
                            "Reads a normal map as a tangent-space normal for the Normal channel.");
    normal.code = material_node_code::normal_map;
    normal.value_type = vt::float3;
    normal.inputs = {in_code("uv", "UV", vt::float2, UV_CODE), in("strength", "Strength", vt::float1, math::vec4(1.0f))};
    normal.outputs = {out("normal", "Normal", vt::float3)};
    normal.settings = {make_parameter_name_setting(TEXTURE_NAME_TOOLTIP), make_description_setting()};
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
                        in("sharpness", "Sharpness", vt::float1, math::vec4(DEFAULT_TRIPLANAR_SHARPNESS))};
    triplanar.outputs = texel_outputs();
    triplanar.settings = {make_parameter_name_setting(TEXTURE_NAME_TOOLTIP), make_description_setting()};
    nodes.push_back(with_keywords(triplanar, "world projection box mapping cube projection"));
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
    nodes.push_back(with_keywords(make_fixed("noise",
                                             "Noise",
                                             category::procedural,
                                             "Smooth value noise between 0 and 1.",
                                             {in_code("uv", "UV", vt::float2, UV_CODE),
                                              in("scale", "Scale", vt::float1, math::vec4(DEFAULT_NOISE_SCALE))},
                                             {out("out", "", vt::float1)},
                                             vt::float1,
                                             "mg_value_noise({uv} * {scale})"),
                                  "value noise random perlin clouds"));
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
}

void add_utility_nodes(std::vector<material_node_def>& nodes)
{
    nodes.push_back(with_shortcut(make_fixed("panner",
                                             "Panner",
                                             category::utility,
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
                                             category::utility,
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
                                             category::utility,
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
                                             category::utility,
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
    type.choices = {"float", "vec2", "vec3", "vec4"};
    type.choice_labels = {"Float", "Vector 2", "Vector 3", "Vector 4"};
    type.default_value = "vec3";
    type.tooltip = "The value the caller's pin takes.";
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
    add_procedural_nodes(nodes);
    add_utility_nodes(nodes);
    add_function_nodes(nodes);
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
            break;
    }
    return 1;
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
           def.code == material_node_code::triplanar_sample;
}

auto can_split_material_pin(const material_pin_def& pin) -> bool
{
    return !pin.is_value_only && pin.type != material_value_type::dynamic && get_material_value_width(pin.type) > 1;
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
