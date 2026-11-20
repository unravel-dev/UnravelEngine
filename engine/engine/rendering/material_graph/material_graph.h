#pragma once
#include <engine/engine_export.h>

#include <engine/assets/asset_handle.h>
#include <graphics/texture.h>
#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>
#include <math/math.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief One input pin of a graph node as authored: the output linked into it, or the value it reads while
 * nothing is linked.
 */
struct material_graph_input
{
    friend auto operator==(const material_graph_input&, const material_graph_input&) -> bool = default;

    /// Name of the input pin, as the node's type declares it (material_graph_library.h). "pin.c" (one component
    /// letter: "base_color.r") feeds one component of a vector pin: a linked component makes the pin a vector
    /// composed of its components.
    std::string pin;
    /// The output feeding the pin, "node_id.output_pin"; "node_id" alone is the node's first output, and
    /// "node_id.output_pin.components" reads some of its components ("color.rgba.r", "color.rgba.rg"). Empty while
    /// unlinked.
    std::string link;
    /// The value read while unlinked. A pin of one component reads x; a pin of a dynamic type broadcasts x.
    math::vec4 value{};
};

/**
 * @brief A setting of a node that is not a pin: a texture, a parameter name, a swizzle mask, a choice.
 */
struct material_graph_setting
{
    friend auto operator==(const material_graph_setting&, const material_graph_setting&) -> bool = default;

    std::string name;
    std::string value;
};

/**
 * @brief A node of a material graph: an instance of a node type of the library (material_graph_library.h).
 */
struct material_graph_node
{
    friend auto operator==(const material_graph_node&, const material_graph_node&) -> bool = default;

    /// Unique in the graph and stable across edits; links name it, and the generated code names the node's value
    /// after it.
    std::string id;
    /// The node type, a key of the node library ("multiply", "texture_sample", "surface_output").
    std::string type;
    /// Editor only: the node's top-left corner on the canvas, in graph units.
    math::vec2 position{};
    /// The inputs that are linked or hold a value of their own. An input not listed reads its pin's default.
    std::vector<material_graph_input> inputs;
    /// The settings that differ from their default. A setting not listed has its default.
    std::vector<material_graph_setting> settings;
    /// Texture nodes only: the texture read.
    asset_handle<gfx::texture> texture;
    /// Editor only: vector outputs shown with a pin per component, besides their own.
    std::vector<std::string> split_outputs;
    /// Editor only: vector inputs shown as a pin per component, in place of their own.
    std::vector<std::string> split_inputs;
    /// Editor only: the node's swatch of its value is folded away.
    bool is_preview_hidden{};
};

/**
 * @brief A note on the canvas framing a group of nodes. Editor only: it changes no code.
 */
struct material_graph_comment
{
    friend auto operator==(const material_graph_comment&, const material_graph_comment&) -> bool = default;

    std::string text;
    math::vec2 position{};
    math::vec2 size{};
};

/**
 * @brief A material authored as a graph of nodes flowing into one surface output node, the source the material
 * graph compiler (material_graph_compiler.h) generates a G-buffer fragment shader from.
 *
 * The graph is plain data with string ids: a link is "node_id.output_pin" stored on the input it feeds, so a graph
 * reads and writes as JSON a person or a tool can author directly (save_to_string in
 * engine/meta/rendering/material_graph.hpp).
 */
struct material_graph
{
    friend auto operator==(const material_graph&, const material_graph&) -> bool = default;

    std::vector<material_graph_node> nodes;
    std::vector<material_graph_comment> comments;
};

/// The node type of the graph's surface output: one per graph, where the material's channels are linked.
inline constexpr const char* MATERIAL_GRAPH_OUTPUT_TYPE = "surface_output";
/// The output of a decal material: what a decal lays onto the surfaces its box projects onto.
inline constexpr const char* MATERIAL_GRAPH_DECAL_OUTPUT_TYPE = "decal_output";
/// The output of a post-process material: the new colour of each pixel of the view at its Location.
inline constexpr const char* MATERIAL_GRAPH_POST_PROCESS_OUTPUT_TYPE = "post_process_output";

/**
 * @brief What a material draws, as its graph's output node says: a surface of a mesh, a decal projected onto other
 * surfaces, or a full-screen post-process pass. The values are saved in compiled materials: append only.
 */
enum class material_domain : uint8_t
{
    surface,
    decal,
    post_process,
};

/// The number of material domains.
inline constexpr uint32_t MATERIAL_DOMAIN_COUNT = 3;

/// The output node type of @p domain.
auto get_material_graph_output_type(material_domain domain) -> const char*;
/// The domain whose output node type is @p type; empty for any other node type.
auto find_material_domain_of_output_type(hpp::string_view type) -> hpp::optional<material_domain>;
/// Whether @p type is the output node type of a domain.
auto is_material_graph_output_type(hpp::string_view type) -> bool;
/// The domain as the editor names it ("Surface", "Decal", "Post Process").
auto get_material_domain_label(material_domain domain) -> const char*;
/// The domain as files and tools name it ("surface", "decal", "post_process").
auto get_material_domain_key(material_domain domain) -> const char*;
/// The domain a file or tool names; empty for an unknown name.
auto find_material_domain(hpp::string_view key) -> hpp::optional<material_domain>;
/// The domain of @p graph: its output node's; Surface for a graph without one.
auto get_material_graph_domain(const material_graph& graph) -> material_domain;
/**
 * @brief Makes @p graph a material of @p domain: its output node becomes the domain's, keeping its id, position and
 * every input whose pin the new node has (Opacity Mask and Opacity carry over to each other). Settings the new node
 * has no setting for are dropped. A graph without an output gets the domain's.
 * @return True when the graph changed.
 */
auto set_material_graph_domain(material_graph& graph, material_domain domain) -> bool;

/// Decal Output: the opacity every channel the decal writes is scaled by; also the Post Process Output's.
inline constexpr const char* MATERIAL_OPACITY_PIN = "opacity";
/// Decal Output: the optional opacities of the normal and of metallic and roughness (unlinked: Opacity).
inline constexpr const char* MATERIAL_NORMAL_OPACITY_PIN = "normal_opacity";
inline constexpr const char* MATERIAL_SURFACE_OPACITY_PIN = "surface_opacity";
/// Post Process Output: the pixel's new colour.
inline constexpr const char* MATERIAL_COLOR_PIN = "color";
/// Decal Output: how the decal's normal meets the surface's: "detail" (bends the surface's own normal, the default)
/// or "replace" (replaces it over the flat surface).
inline constexpr const char* MATERIAL_DECAL_NORMAL_BLEND_SETTING = "normal_blend";
inline constexpr const char* MATERIAL_DECAL_NORMAL_BLEND_DETAIL = "detail";
inline constexpr const char* MATERIAL_DECAL_NORMAL_BLEND_REPLACE = "replace";
/// Decal Output: the share of the box's depth over which the decal fades out at each end (0 to 0.5).
inline constexpr const char* MATERIAL_DECAL_EDGE_FADE_SETTING = "edge_fade";
/// Post Process Output: where in the frame the material runs (material_post_process_location's keys).
inline constexpr const char* MATERIAL_POST_PROCESS_LOCATION_SETTING = "location";
/// Post Process Output: the order among the materials of one Location; higher runs later.
inline constexpr const char* MATERIAL_POST_PROCESS_PRIORITY_SETTING = "priority";
/// The Post Process Output's setting naming the Post Process Mask value it runs over; 0 runs everywhere.
inline constexpr const char* MATERIAL_POST_PROCESS_MASK_SETTING = "run_where_mask";

/**
 * @brief Where a post-process material runs in the frame. The keys and their order are a contract: a location is
 * added, never renamed or moved. The values are saved in compiled materials: append only.
 */
enum class material_post_process_location : uint8_t
{
    /// After the sky and the fog, before particles and TAA: the scene's light at the render size, jittered.
    before_transparent,
    /// After TAA and the exposure meter, before bloom and tonemapping: the scene's light, stable.
    before_bloom,
    /// After tonemapping, before FXAA and the UI: the display colour, 0 to 1.
    after_tonemapping,
};

inline constexpr uint32_t MATERIAL_POST_PROCESS_LOCATION_COUNT = 3;
/// The Location setting's value of @p location ("before_transparent", ...).
auto get_material_post_process_location_key(material_post_process_location location) -> const char*;
/// The Location as the editor names it ("Before Transparent", ...).
auto get_material_post_process_location_label(material_post_process_location location) -> const char*;
/// The location a setting value names; empty for an unknown value.
auto find_material_post_process_location(hpp::string_view key) -> hpp::optional<material_post_process_location>;
/// The setting naming a parameter node's parameter, which material instances and scripts set it by; on a texture
/// node, the name its texture is set by; on a Static Switch, the name that shows it in the material's inspector.
inline constexpr const char* MATERIAL_PARAMETER_NAME_SETTING = "name";
/// The setting of a parameter, texture or named Static Switch node holding the hint of its row in the inspector.
inline constexpr const char* MATERIAL_PARAMETER_DESCRIPTION_SETTING = "description";
/// A parameter's setting placing it among the registers material instances override per model without a draw of
/// their own ("on", the default) or after them ("off").
inline constexpr const char* MATERIAL_PARAMETER_PER_INSTANCE_SETTING = "per_instance";
inline constexpr const char* MATERIAL_PARAMETER_PER_INSTANCE_OFF = "off";
/// A Static Switch's setting picking its input ("true" / "false").
inline constexpr const char* MATERIAL_STATIC_SWITCH_VALUE_SETTING = "value";
inline constexpr const char* MATERIAL_STATIC_SWITCH_TRUE = "true";
inline constexpr const char* MATERIAL_STATIC_SWITCH_FALSE = "false";
/// The Surface Output's setting that draws back faces too ("on" / "off").
inline constexpr const char* MATERIAL_TWO_SIDED_SETTING = "two_sided";
inline constexpr const char* MATERIAL_TWO_SIDED_ON = "on";
/// The Surface Output's setting that hides front faces and draws back faces ("on" / "off"), for a mesh seen from inside
/// (a sky dome, an interior); Two Sided takes precedence. A render state alone: it changes no code.
inline constexpr const char* MATERIAL_CULL_FRONT_FACES_SETTING = "cull_front_faces";
inline constexpr const char* MATERIAL_CULL_FRONT_FACES_ON = "on";
/// The setting files written before Blend Mode used to make a surface see-through ("on" / "off");
/// upgrade_material_graph turns "on" into the Blend Mode k_default_translucent_blend_mode names.
inline constexpr const char* MATERIAL_TRANSLUCENT_SETTING = "translucent";
inline constexpr const char* MATERIAL_TRANSLUCENT_ON = "on";

/**
 * @brief How a surface covers what is behind it: the Surface Output's Blend Mode. Opaque and Dithered draw into the
 * G-buffer; the others draw in the translucency pass after the lighting, blended over the scene. The values are saved
 * in compiled materials: append only.
 */
enum class material_blend_mode : uint8_t
{
    /// Covers what is behind it; an Opacity Mask clips it (masked).
    opaque,
    /// See-through by a dither of its Opacity, in the G-buffer: lit, shadowed and reflected like opaque surfaces.
    dithered,
    /// Blended over the scene by its Opacity, lit in the translucency pass: glass, water, fades.
    translucent,
    /// Its colour is added as it is, and Opacity covers what is behind: light that also occludes.
    premultiplied,
    /// Adds its light to the scene, scaled by Opacity: glows, beams, holograms.
    additive,
    /// Multiplies the scene behind it by its colour (unlit): tints, stains, shade.
    modulate,
};

inline constexpr uint32_t MATERIAL_BLEND_MODE_COUNT = 6;

/**
 * @brief The Blend Mode a surface made see-through takes where nothing names one: the Translucent setting of older
 * files, a glTF material's BLEND alpha mode, a PBR material's Blend. Changing it here moves every such default.
 */
inline constexpr material_blend_mode k_default_translucent_blend_mode = material_blend_mode::dithered;

/// The Surface Output's setting naming its Blend Mode (material_blend_mode's keys); "opaque" by default.
inline constexpr const char* MATERIAL_BLEND_MODE_SETTING = "blend_mode";

/// The Blend Mode as files and tools name it ("opaque", "dithered", ...).
auto get_material_blend_mode_key(material_blend_mode mode) -> const char*;
/// The Blend Mode as the editor names it ("Opaque", "Dithered", ...).
auto get_material_blend_mode_label(material_blend_mode mode) -> const char*;
/// The Blend Mode a file or tool names; empty for an unknown name.
auto find_material_blend_mode(hpp::string_view key) -> hpp::optional<material_blend_mode>;
/// Whether surfaces of @p mode draw in the translucency pass rather than the G-buffer.
auto is_material_blend_mode_forward(material_blend_mode mode) -> bool;
/// Whether @p mode blends by an order (Translucent, Premultiplied): the modes that sort, may be Order Independent and
/// refract.
auto is_material_blend_mode_order_dependent(material_blend_mode mode) -> bool;

/**
 * @brief Which light a surface takes: the Surface Output's Shading Model. Unlit shows its Emissive alone. The values
 * are saved in compiled materials: append only.
 */
enum class material_shading_model : uint8_t
{
    default_lit,
    unlit,
};

inline constexpr uint32_t MATERIAL_SHADING_MODEL_COUNT = 2;

/// The Surface Output's setting naming its Shading Model ("default_lit", "unlit"); "default_lit" by default.
inline constexpr const char* MATERIAL_SHADING_MODEL_SETTING = "shading_model";

/// The key a Surface Output's MATERIAL_SHADING_MODEL_SETTING stores for @p model ("default_lit", "unlit").
auto get_material_shading_model_key(material_shading_model model) -> const char*;
/// The name the editor shows for @p model ("Default Lit", "Unlit").
auto get_material_shading_model_label(material_shading_model model) -> const char*;
/// The Shading Model a setting value names; none for an unknown key.
auto find_material_shading_model(hpp::string_view key) -> hpp::optional<material_shading_model>;

/// The Surface Output's setting that adds a Translucent surface's highlights and reflections at full strength instead
/// of scaled by its Opacity ("on", the default, / "off").
inline constexpr const char* MATERIAL_PRESERVE_SPECULAR_SETTING = "preserve_specular";
inline constexpr const char* MATERIAL_PRESERVE_SPECULAR_ON = "on";
/// The Surface Output's setting of how strongly a surface of a translucency-pass Blend Mode makes TAA keep less of
/// its history under it (0 to 1).
inline constexpr const char* MATERIAL_TAA_RESPONSE_SETTING = "taa_response";
inline constexpr float MATERIAL_DEFAULT_TAA_RESPONSE = 0.5f;
/// The Surface Output's setting that blends a Translucent or Premultiplied surface without sorting ("on" / "off", the
/// default): weighted blended order-independent transparency, for overlaps no order per object gets right.
inline constexpr const char* MATERIAL_ORDER_INDEPENDENT_SETTING = "order_independent";
inline constexpr const char* MATERIAL_ORDER_INDEPENDENT_ON = "on";
/// The Surface Output's setting that lets a surface of the translucency pass cast a shadow dithered by its Opacity
/// ("dithered" / "off", the default).
inline constexpr const char* MATERIAL_CAST_SHADOW_SETTING = "cast_shadow";
inline constexpr const char* MATERIAL_CAST_SHADOW_DITHERED = "dithered";
/// The Surface Output's setting that draws a sorted Translucent or Premultiplied surface's depth before its colour, so
/// only its nearest layer shows where it overlaps itself ("on" / "off", the default).
inline constexpr const char* MATERIAL_DEPTH_PREPASS_SETTING = "depth_prepass";
/// The Surface Output's setting that draws a surface of the translucency pass's motion into the velocity buffer over
/// the scene behind it ("on" / "off", the default).
inline constexpr const char* MATERIAL_WRITE_VELOCITY_SETTING = "write_velocity";
/// The Surface Output's setting that gives a lit Translucent or Premultiplied surface the GI's traced reflections where
/// it is the nearest such surface of a pixel ("on" / "off", the default).
inline constexpr const char* MATERIAL_GI_REFLECTIONS_SETTING = "gi_reflections";
/// The value of the on / off settings that turns them on.
inline constexpr const char* MATERIAL_SETTING_ON = "on";
/// The Surface Output's setting picking how the surface is described: base color, metallic and roughness
/// ("metallic", the default), or diffuse, specular and glossiness ("specular"), which the shader converts to the
/// first per pixel (mg_specular_to_metallic in material_graph.sh).
inline constexpr const char* MATERIAL_WORKFLOW_SETTING = "workflow";
inline constexpr const char* MATERIAL_WORKFLOW_METALLIC = "metallic";
inline constexpr const char* MATERIAL_WORKFLOW_SPECULAR = "specular";
/// The Surface Output's inputs of the metallic workflow and of the specular one; the other workflow's are unused.
inline constexpr const char* MATERIAL_METALLIC_PIN = "metallic";
inline constexpr const char* MATERIAL_ROUGHNESS_PIN = "roughness";
inline constexpr const char* MATERIAL_SPECULAR_PIN = "specular";
inline constexpr const char* MATERIAL_GLOSSINESS_PIN = "glossiness";
/// The Surface Output's index of refraction (1: no refraction), read by Translucent and Premultiplied surfaces.
inline constexpr const char* MATERIAL_REFRACTION_PIN = "refraction";
/// The node reading how far the opaque surface behind lies past this one, faded over a distance.
inline constexpr const char* MATERIAL_DEPTH_FADE_TYPE = "depth_fade";
/// The Surface Output's input holding the value the Opacity Mask clips below.
inline constexpr const char* MATERIAL_OPACITY_MASK_CLIP_PIN = "opacity_mask_clip";
/// The settings of a scalar parameter naming the range the editor offers its value in.
inline constexpr const char* MATERIAL_PARAMETER_SLIDER_MIN_SETTING = "slider_min";
inline constexpr const char* MATERIAL_PARAMETER_SLIDER_MAX_SETTING = "slider_max";

/*
 * Material functions: a graph of Function Input and Function Output nodes in a .mgraph file of the project, used by
 * Material Function nodes (MATERIAL_FUNCTION_CALL_TYPE) that name its key in their `function` setting. A call's
 * pins in0.. take the function's inputs in the graph's order, out0.. give its outputs; the compiler puts the
 * function's nodes in the call's place. A graph material keeps a copy of every function it uses
 * (graph_material::set_functions), so a game needs none of the files.
 */
inline constexpr const char* MATERIAL_FUNCTION_INPUT_TYPE = "function_input";
inline constexpr const char* MATERIAL_FUNCTION_OUTPUT_TYPE = "function_output";
inline constexpr const char* MATERIAL_FUNCTION_CALL_TYPE = "material_function";
/// The setting naming a function's input or output; empty: the node's id.
inline constexpr const char* MATERIAL_FUNCTION_NAME_SETTING = "name";
/// A call's setting naming the function's file.
inline constexpr const char* MATERIAL_FUNCTION_KEY_SETTING = "function";
inline constexpr uint32_t MATERIAL_FUNCTION_MAX_INPUTS = 8;
inline constexpr uint32_t MATERIAL_FUNCTION_MAX_OUTPUTS = 4;
/// A Color Ramp's stops: at most this many, as many as its count setting says, blended as its interpolation setting
/// says ("linear", "smooth", "constant"). Stop k is the hidden value inputs color<k> (RGBA) and position<k> (0 to 1).
inline constexpr uint32_t MATERIAL_COLOR_RAMP_MAX_STOPS = 8;
inline constexpr const char* MATERIAL_COLOR_RAMP_COUNT_SETTING = "count";
inline constexpr const char* MATERIAL_COLOR_RAMP_INTERPOLATION_SETTING = "interpolation";
auto get_material_color_ramp_color_pin(uint32_t stop) -> std::string;
auto get_material_color_ramp_position_pin(uint32_t stop) -> std::string;

/// The extension of a function's (and any graph's) file.
inline constexpr const char* MATERIAL_GRAPH_FILE_EXTENSION = ".mgraph";

/// A call's pin taking a function's input @p index ("in0"), giving its output ("out0"), and the hidden one the compiler
/// links to that output ("result0").
auto get_material_function_input_pin(uint32_t index) -> std::string;
auto get_material_function_output_pin(uint32_t index) -> std::string;
auto get_material_function_result_pin(uint32_t index) -> std::string;

/// A graph that is a material function: it has Function Output nodes and no Surface Output.
auto is_material_function_graph(const material_graph& graph) -> bool;

/// One input or output of a material function, as its call shows it.
struct material_function_pin
{
    std::string name;
    std::string node_id;
    /// An input's declared type ("float", "vec2", "vec3", "vec4"); empty for an output, which takes what it is linked.
    std::string type;
};

/// What a call of a function shows: its inputs and outputs in the graph's order.
struct material_function_interface
{
    std::vector<material_function_pin> inputs;
    std::vector<material_function_pin> outputs;
};

auto get_material_function_interface(const material_graph& function) -> material_function_interface;

/// The material function at a key; null when there is none.
using material_function_resolver = std::function<std::shared_ptr<const material_graph>(const std::string& key)>;

/**
 * @brief The keys of the material functions @p graph calls, and those they call in turn, each once in the order first
 * called: what its code reads besides the graph. @p resolve gives a function's graph; a key it gives none for is
 * listed, its own calls unknown. Follows the call nodes alone, without compiling.
 */
auto collect_material_function_keys(const material_graph& graph, const material_function_resolver& resolve)
    -> std::vector<std::string>;

/**
 * @brief The node and output pin a link names.
 */
struct material_graph_link_end
{
    std::string node;
    /// Empty: the node's first output.
    std::string pin;
    /// The components read, in the order read ("r", "rg", "zyx"); empty: the whole value.
    std::string components;
};

/// Splits "node_id.pin.components" (or "node_id.pin", or "node_id") into its parts.
auto parse_material_graph_link(hpp::string_view link) -> material_graph_link_end;

/// The link text naming @p pin of @p node, reading @p components of it; @p pin empty names the node's first output.
auto make_material_graph_link(hpp::string_view node, hpp::string_view pin, hpp::string_view components = {}) -> std::string;

/// An input entry's pin split into the pin and the component it feeds: "base_color.r" -> ("base_color", 0);
/// a whole pin has component -1.
struct material_graph_pin_component
{
    std::string pin;
    int component{-1};
};
auto parse_material_graph_pin(hpp::string_view entry) -> material_graph_pin_component;

/// The input entry name feeding component @p component of @p pin, written with @p letters ("xyzw" or "rgba").
auto make_material_graph_component_pin(hpp::string_view pin, int component, hpp::string_view letters) -> std::string;

/// Index of a component letter, x y z w or r g b a; -1 for any other character.
auto get_material_component_index(char letter) -> int;

/// Index of the node with @p id, or -1.
auto find_material_graph_node(const material_graph& graph, hpp::string_view id) -> int;

/// Index of the surface output node, or -1.
auto find_material_graph_output(const material_graph& graph) -> int;

/// The input entry of @p pin, or null when the node lists none for it.
auto find_material_graph_input(const material_graph_node& node, hpp::string_view pin) -> const material_graph_input*;
auto find_material_graph_input(material_graph_node& node, hpp::string_view pin) -> material_graph_input*;

/// The input entry of @p pin, added (unlinked, holding @p default_value) when the node lists none.
auto get_or_add_material_graph_input(material_graph_node& node, hpp::string_view pin, const math::vec4& default_value)
    -> material_graph_input&;

/// The value of setting @p name, or null when the node does not set it.
auto find_material_graph_setting(const material_graph_node& node, hpp::string_view name) -> const std::string*;

/// Sets setting @p name of the node to @p value.
void set_material_graph_setting(material_graph_node& node, hpp::string_view name, const std::string& value);

/// Whether Surface Output @p output describes the surface in the specular workflow (MATERIAL_WORKFLOW_SETTING).
auto is_material_specular_workflow(const material_graph_node& output) -> bool;

/// Whether the Surface Output of @p graph describes the surface in the specular workflow; false without one.
auto is_material_graph_specular_workflow(const material_graph& graph) -> bool;

/// The workflow of @p graph as its Surface Output's Workflow setting names it to the user ("Metallic", "Specular").
auto get_material_graph_workflow_label(const material_graph& graph) -> std::string;

/// The Blend Mode of Surface Output @p output (MATERIAL_BLEND_MODE_SETTING); Opaque when it names none or an unknown one.
auto get_material_blend_mode(const material_graph_node& output) -> material_blend_mode;

/// The Blend Mode of @p graph's Surface Output; Opaque without one.
auto get_material_graph_blend_mode(const material_graph& graph) -> material_blend_mode;

/// Sets the Blend Mode of @p graph's Surface Output. False when the graph has no Surface Output or already has it.
auto set_material_graph_blend_mode(material_graph& graph, material_blend_mode mode) -> bool;

/// The Shading Model of Surface Output @p output: its setting, and Unlit for the Modulate Blend Mode, which never lights.
auto get_material_shading_model(const material_graph_node& output) -> material_shading_model;

/// The Shading Model of @p graph's Surface Output; Default Lit without one.
auto get_material_graph_shading_model(const material_graph& graph) -> material_shading_model;

/// Whether input @p pin of Surface Output @p output feeds the surface: Metallic and Roughness do not in the specular
/// workflow, Specular and Glossiness not in the metallic one; Unlit reads Emissive, Opacity, World Position Offset and
/// Camera Fade Distance alone (and Normal in the translucency pass); Opacity Mask Clip clips Opaque surfaces only, and
/// the subsurface channels light G-buffer surfaces only.
auto is_material_output_pin_used(const material_graph_node& output, hpp::string_view pin) -> bool;

/// Whether the editor shows setting @p setting of Surface Output @p output: Preserve Specular for Translucent surfaces,
/// Order Independent for Translucent and Premultiplied ones, TAA Response for those of the translucency pass; every
/// other setting always.
auto is_material_output_setting_shown(const material_graph_node& output, hpp::string_view setting) -> bool;

/// What the editor names input @p pin of Surface Output @p output: Opacity Mask is Opacity on see-through surfaces, and
/// Base Color the diffuse color in the specular workflow. Null: the pin's own label.
auto get_material_output_pin_label(const material_graph_node& output, hpp::string_view pin) -> const char*;

/**
 * @brief Brings a graph to the current node library: a Surface Output that names Opacity Mask Clip as a setting, as
 * files written before it was an input do, gets the input's value from it (an input set too wins); one that names the
 * old Translucent setting gets the Blend Mode it means (k_default_translucent_blend_mode for "on") unless it names a
 * Blend Mode too.
 */
void upgrade_material_graph(material_graph& graph);

/// The range the editor offers a parameter node's value in, its Slider Min and Slider Max: empty unless the maximum is
/// the greater. The shader never clamps the value to it.
auto find_material_parameter_slider(const material_graph_node& node) -> hpp::optional<math::vec2>;

/// Sets the default of parameter node @p node_id, the value a material that sets none of its own draws with: the
/// value of the node's first input. False when the graph has no parameter node of that id.
auto set_material_graph_parameter_default(material_graph& graph, hpp::string_view node_id, const math::vec4& value) -> bool;

/// Whether @p node holds a texture of its own (material_graph_node::texture): a texture node, a Texture or Cubemap
/// Object, or a Function Input of a texture type (its texture while the caller links none).
auto holds_material_graph_texture(const material_graph_node& node) -> bool;

/// Sets the texture texture node @p node_id reads. False when the graph has no node of that id that reads one.
auto set_material_graph_node_texture(material_graph& graph, hpp::string_view node_id, const asset_handle<gfx::texture>& texture)
    -> bool;

/// Sets which input Static Switch @p node_id passes on. False when the graph has no Static Switch of that id, or it
/// already passes that one.
auto set_material_graph_static_switch(material_graph& graph, hpp::string_view node_id, bool value) -> bool;

/// Whether @p a and @p b generate the same code: they differ in values (parameter defaults, texture nodes' textures)
/// or the editor's layout (positions, split pins, folded swatches, comments) alone, if at all.
auto has_same_material_graph_code(const material_graph& a, const material_graph& b) -> bool;

/// @p id when no node has it, otherwise @p id with the first free number appended. Characters that cannot appear in
/// a shader identifier become underscores.
auto make_unique_material_graph_node_id(const material_graph& graph, hpp::string_view id) -> std::string;

/// A shader-safe form of @p text: letters, digits and single underscores, lower case, never empty, never starting
/// with a digit.
auto sanitize_material_graph_identifier(hpp::string_view text) -> std::string;

/// Removes the node with @p id and every link into it. The surface output is never removed.
/// @return True when a node was removed.
auto remove_material_graph_node(material_graph& graph, hpp::string_view id) -> bool;

/// Renames a node and every link naming it. @p new_id is made unique first.
/// @return The id the node has now; empty when no node has @p old_id.
auto rename_material_graph_node(material_graph& graph, hpp::string_view old_id, hpp::string_view new_id) -> std::string;

/// Clears every link that reads an output of the node with @p id.
void unlink_material_graph_node_outputs(material_graph& graph, hpp::string_view id);

/// Adds a surface output node when the graph has none.
/// @return True when one was added.
auto ensure_material_graph_output(material_graph& graph) -> bool;

/// True when every node of the graph sits on one spot: a graph authored without positions (in a file or by a tool),
/// which the editor lays out when it shows it.
auto is_material_graph_never_laid_out(const material_graph& graph) -> bool;

/// Whether the editor shows pin @p pin of the node split into components.
auto is_material_graph_input_split(const material_graph_node& node, hpp::string_view pin) -> bool;
auto is_material_graph_output_split(const material_graph_node& node, hpp::string_view pin) -> bool;

/**
 * @brief Shows pin @p pin of the node split into a pin per component, or as one pin again. Split, an input loses its
 * own link (its components are linked one by one) and keeps its value for the unlinked ones; joined again, it loses
 * the links of its components. An output keeps its links either way.
 */
void set_material_graph_pin_split(material_graph_node& node, hpp::string_view pin, bool is_output, bool split);

/**
 * @brief Marks split every pin a component link reads or feeds, so a graph written by hand or by a tool shows its
 * component links on component pins. Editor display state only.
 * @return True when a pin was marked.
 */
auto split_material_graph_component_pins(material_graph& graph) -> bool;

/**
 * @brief The nodes with @p ids as a graph of their own, for the clipboard. Links to nodes outside it stay as they
 * are; paste_material_graph_nodes resolves them.
 */
auto copy_material_graph_nodes(const material_graph& graph, const std::vector<std::string>& ids) -> material_graph;

/**
 * @brief Adds the nodes of @p fragment to @p graph, moved by @p offset.
 *
 * Each node gets an id the graph does not have yet; links between the pasted nodes follow their new ids, links to a
 * node the fragment does not hold stay when the graph has it and are cleared otherwise. A surface output in the
 * fragment is not added: its links and values go to the graph's own output, so a whole graph pasted into an empty
 * one wires up as written; a material function, which has no output, drops them.
 * @return The ids of the added nodes.
 */
auto paste_material_graph_nodes(material_graph& graph, const material_graph& fragment, const math::vec2& offset)
    -> std::vector<std::string>;

} // namespace unravel
