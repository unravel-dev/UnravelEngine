#pragma once
#include <engine/engine_export.h>

#include <hpp/string_view.hpp>
#include <math/math.h>

#include <cstdint>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief Value type of a pin or a node's value.
 */
enum class material_value_type : uint8_t
{
    float1,
    float2,
    float3,
    float4,
    /// Resolved per node: the widest value linked into the node's dynamic inputs, a float1 when none is linked.
    dynamic,
};

/// Components of a resolved type (1 to 4); 1 for dynamic.
auto get_material_value_width(material_value_type type) -> uint32_t;

/// The vector type of @p width components (1 to 4).
auto get_material_value_type(uint32_t width) -> material_value_type;

/// The type as the shader names it: "float", "vec2", "vec3", "vec4".
auto get_material_value_shader_type(material_value_type type) -> const char*;

/// The type as the editor shows it: "Float", "Vector 2"...
auto get_material_value_type_name(material_value_type type) -> const char*;

/**
 * @brief The group a node type is listed in; the editor colours a node's header by it.
 */
enum class material_node_category : uint8_t
{
    output,
    input,
    constant,
    parameter,
    texture,
    math,
    vector,
    procedural,
    utility,
    function,
};

auto get_material_node_category_name(material_node_category category) -> const char*;

/**
 * @brief How the compiler turns a node into code. Most nodes are an expression template; the others need the
 * compiler's own handling.
 */
enum class material_node_code : uint8_t
{
    /// `expression`, with {pin} replaced by each input's code and {setting} by a choice or swizzle setting.
    expression,
    /// A uniform of the material, exposed to material instances (scalar, vector, colour parameters).
    parameter,
    /// A texture of the material read at the uv input.
    texture_sample,
    /// A tangent-space normal map read at the uv input.
    normal_map,
    /// Two values joined into one wider vector.
    append,
    /// Components picked from the input by the `mask` setting.
    swizzle,
    /// The input passed on unchanged: a bend point for links.
    reroute,
    /// The material's channels.
    surface_output,
    /// One of two inputs passed on, as the `value` setting picks when the material compiles: the other branch is not
    /// in the code.
    static_switch,
    /// A named value: its input passed on, and read by name by named_reroute nodes anywhere in the graph.
    named_reroute_declaration,
    /// The value of the named_reroute_declaration its `name` setting names; the compiler links its hidden input.
    named_reroute,
    /// The user's code: the body of a function of the inputs (and the MaterialInputs i) returning the `output` type.
    custom,
    /// A texture of the material read along the three world axes, blended by the normal.
    triplanar_sample,
    /// A material function's input: what the caller links into its pin, else its default (material_graph.h).
    function_input,
    /// A material function's output: its input passed on to the caller's pin.
    function_output,
    /// A call of a material function: the compiler puts the function's nodes in its place.
    function_call,
    /// The pixel is not drawn where Value is below Threshold. No output: the compiler emits every one in the pixel
    /// stage, as it emits the Surface Output.
    discard,
};

/**
 * @brief How the type of a node's value is found.
 */
enum class material_type_rule : uint8_t
{
    /// The node's value_type.
    fixed,
    /// The widest value linked into the node's dynamic inputs (a float1 when none is linked).
    dynamic,
    /// The widths of inputs a and b added up (at most 4).
    append,
    /// As many components as the `mask` setting names.
    swizzle,
    /// The width of the input the `value` setting picks (static_switch); each input keeps its own width.
    chosen_input,
    /// The type the `output` setting names (custom); each input keeps its own width.
    custom,
    /// The type the `type` setting names (function_input).
    declared,
    /// The type of the first output the function gives (function_call); each pin keeps its own width.
    function_call,
};

/**
 * @brief A pin of a node type.
 */
struct material_pin_def
{
    std::string name;
    std::string label;
    material_value_type type{material_value_type::dynamic};
    /// The value of an unlinked input that has none of its own.
    math::vec4 default_value{};
    /// Code an unlinked input reads instead of its value, for an input with a natural source (a uv input reads
    /// the mesh's first texture coordinates). Empty: the value.
    std::string default_code;
    /// Outputs only: read from the node's value with this swizzle (".rgb", ".r"); empty for the whole value.
    std::string swizzle;
    /// Inputs only: edited on the node, never linked (a constant's value).
    bool is_value_only{};
    /// Edited with a colour picker.
    bool is_color{};
    /// How the editor drags an unlinked input's value, when it differs from the editor's own: the speed per pixel (0:
    /// the editor's), and the range the value keeps to while max_value > min_value (a float shows as a slider).
    float drag_speed{};
    float min_value{};
    float max_value{};
    /// Never shown or linked by hand; the compiler fills it (a Named Reroute Usage's declaration, a function call's
    /// results).
    bool is_hidden{};
    /// Shown by the editor as the pin's tooltip.
    std::string tooltip;
};

/**
 * @brief How a node setting is edited and what the compiler does with it.
 */
enum class material_setting_kind : uint8_t
{
    /// Free text the compiler never puts into code (a parameter's name).
    text,
    /// One of `choices`; an expression may name it ({setting}).
    choice,
    /// A number; an expression may name it.
    number,
    /// Lines of shader code the compiler writes as they are (a Custom node's body).
    code,
    /// Up to four components out of x, y, z, w (or r, g, b, a); an expression may name it.
    swizzle,
};

/**
 * @brief A setting of a node type: what is not a pin.
 */
struct material_setting_def
{
    std::string name;
    std::string label;
    material_setting_kind kind{material_setting_kind::text};
    std::string default_value;
    /// The values a choice takes.
    std::vector<std::string> choices;
    /// What the editor shows for each of `choices`, in their order; empty shows the values themselves.
    std::vector<std::string> choice_labels;
    std::string tooltip;
};

/**
 * @brief A node type of the library. Adding a node to the editor and the compiler is adding one of these.
 */
struct material_node_def
{
    /// Key the graph names the type by: stable, saved in files.
    std::string type;
    std::string label;
    material_node_category category{material_node_category::math};
    /// What the node does, in one or two sentences: the editor's tooltip and the search's text.
    std::string description;
    /// More words the node search matches ("mul times *").
    std::string keywords;
    /// Key that places the node when held during a click on the canvas ('M' multiply, '1' constant); 0 for none.
    char shortcut{};
    std::vector<material_pin_def> inputs;
    std::vector<material_pin_def> outputs;
    std::vector<material_setting_def> settings;
    material_node_code code{material_node_code::expression};
    material_type_rule type_rule{material_type_rule::fixed};
    /// The node's value type under the fixed rule.
    material_value_type value_type{material_value_type::float1};
    /// Code of the node's value for material_node_code::expression.
    std::string expression;
    /// Code used instead while the dynamic inputs are floats: HLSL has no float overloads of the vector functions
    /// (normalize, length, distance, dot). Empty: the expression.
    std::string scalar_expression;
    /// The value is cheap (a constant, a built-in input): it is written where it is read instead of into a variable.
    bool is_inline{};
};

/**
 * @brief Every node type, in the order the editor lists them.
 */
auto get_material_node_library() -> const std::vector<material_node_def>&;

/// The node type @p type, or null for a type the library does not have.
auto find_material_node_def(hpp::string_view type) -> const material_node_def*;

/// The pin named @p name among @p pins, or null.
auto find_material_pin_def(const std::vector<material_pin_def>& pins, hpp::string_view name) -> const material_pin_def*;

/// The setting named @p name of @p def, or null.
auto find_material_setting_def(const material_node_def& def, hpp::string_view name) -> const material_setting_def*;

/// What the editor shows for @p value of the choice @p setting: its entry in choice_labels, else @p value itself.
auto get_material_setting_choice_label(const material_setting_def& setting, const std::string& value) -> const std::string&;

/// True for a node type that reads a texture (material_graph_node::texture).
auto is_material_texture_node(const material_node_def& def) -> bool;

/// True when a pin can be split into a pin per component: a vector of fixed width that takes links (or an output).
auto can_split_material_pin(const material_pin_def& pin) -> bool;

/// The letters a pin's components are named by: "rgba" for colours, "xyzw" for any other vector.
auto get_material_component_letters(const material_pin_def& pin) -> const char*;

} // namespace unravel
