#pragma once
#include <engine/engine_export.h>

#include "material_graph.h"
#include "material_graph_library.h"
#include "material_graph_quality.h"

#include <engine/rendering/render_change.h>

#include <hpp/string_view.hpp>
#include <math/math.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace unravel
{

/// The most textures a graph material reads: bgfx's 16 stages less the skinning palette's (SKINNING_PALETTE_STAGE).
constexpr uint32_t MATERIAL_GRAPH_MAX_TEXTURES = 15;
/// The most textures a decal material reads: its main reads the depth copy and two G-buffer targets on stages of its
/// own (fs_material_graph_decal.sh).
constexpr uint32_t MATERIAL_GRAPH_MAX_DECAL_TEXTURES = 12;
/// The most textures a post-process material reads: its main reads the scene's colour, depth, G-buffer, velocity, the
/// custom mask and the pre-exposure on stages of its own (mg_post.sh).
constexpr uint32_t MATERIAL_GRAPH_MAX_POST_PROCESS_TEXTURES = 7;
/// The most textures a lit surface of the translucency pass reads: its main reads the sky's SH, the probe, the sun's
/// four cascades, the point and spot lights' shadow atlas and the fog's froxel column on stages of its own
/// (mg_forward.sh).
constexpr uint32_t MATERIAL_GRAPH_MAX_FORWARD_LIT_TEXTURES = 7;
/// The most textures an unlit surface of the translucency pass reads: its main reads the sky's SH (for the
/// pre-exposure) and the fog's froxel column on stages of its own.
constexpr uint32_t MATERIAL_GRAPH_MAX_FORWARD_UNLIT_TEXTURES = 13;
/// The most textures, its own and its scene reads, a lit surface of the translucency pass reads and still has the
/// "gi" variant, which reads the GI's radiance cache on the two stages above them (mg_forward.sh FORWARD_GI).
constexpr uint32_t MATERIAL_GRAPH_MAX_FORWARD_GI_TEXTURES = 5;
/// The stages of the radiance cache's indirection and SH in the "gi" variant: the two the lit graph limit gives up.
constexpr uint8_t MATERIAL_FORWARD_GI_INDIRECTION_STAGE = 6;
constexpr uint8_t MATERIAL_FORWARD_GI_SH_STAGE = 5;
static_assert(MATERIAL_GRAPH_MAX_FORWARD_LIT_TEXTURES - MATERIAL_GRAPH_MAX_FORWARD_GI_TEXTURES == 2,
              "the gi variant gives up two stages, for the radiance cache's indirection and SH");
/// The roughness from which a refracting surface blurs what it shows through itself (MATERIAL_ROUGH_REFRACTION): a
/// smoother constant Roughness reads the sharp copy alone and takes no stage for the blurred one.
constexpr float MATERIAL_ROUGH_REFRACTION_MIN = 0.03f;
/// The most textures a material of @p domain reads (a surface of the G-buffer).
auto get_material_graph_max_textures(material_domain domain) -> uint32_t;
/// The most textures a surface of @p mode and @p model reads, less a stage for each of the scene's colour and depth
/// a surface of the translucency pass reads (@p scene_textures: material_scene_texture_bits).
auto get_material_graph_max_textures(material_blend_mode mode, material_shading_model model, uint32_t scene_textures = 0)
    -> uint32_t;

/// The stages a surface of the translucency pass reads the scene's colour and depth at (mg_forward.sh): the stage below
/// the engine's for the first, the next free one for the second, and the one after for the blurred colour of a rough
/// Refraction.
struct forward_scene_texture_stages
{
    uint8_t color{};
    uint8_t depth{};
    uint8_t blurred_color{};
};
/// The stages of @p scene_textures for a lit (@p is_lit) surface, in its "gi" variant when @p is_gi: there they sit
/// below the GI's two stages (MATERIAL_GRAPH_MAX_FORWARD_GI_TEXTURES).
auto get_forward_scene_texture_stages(bool is_lit, uint32_t scene_textures, bool is_gi = false) -> forward_scene_texture_stages;
/// How many stages the scene reads of @p scene_textures take (material_scene_texture_bits: colour, depth, blurred).
auto count_forward_scene_reads(uint32_t scene_textures) -> uint32_t;

/// What a decal material writes (material_graph_compile_result::decal_writes): the channels its Decal Output links.
enum material_decal_write_bits : uint32_t
{
    material_decal_writes_base_color = 1u << 0u,
    material_decal_writes_normal = 1u << 1u,
    /// Metallic and roughness, written together.
    material_decal_writes_surface = 1u << 2u,
    material_decal_writes_emissive = 1u << 3u,
};
/// The most vec4 registers a graph material's parameters take (the size the u_mg_params uniform is made with).
constexpr uint32_t MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS = 64;
/// The uniform array holding a graph material's parameters.
constexpr const char* MATERIAL_GRAPH_PARAMETERS_UNIFORM = "u_mg_params";
/// The uniform holding the time a graph material animates with: x = seconds.
constexpr const char* MATERIAL_GRAPH_TIME_UNIFORM = "u_material_time";
/// The uniform array holding where each normal map a graph material reads keeps its vector, by texture index, one
/// per component: the gfx::normal_map_channels of the texture a draw binds. A platform compresses a normal map its
/// own way (BC5 keeps it in red and green, ASTC in red and alpha), so the layout is the texture's, never the graph's.
constexpr const char* MATERIAL_GRAPH_NORMAL_CHANNELS_UNIFORM = "u_mg_normal_channels";
/// The vec4 registers of MATERIAL_GRAPH_NORMAL_CHANNELS_UNIFORM: a component per texture.
constexpr uint32_t MATERIAL_GRAPH_NORMAL_CHANNEL_REGISTERS = (MATERIAL_GRAPH_MAX_TEXTURES + 3) / 4;

/**
 * @brief The engine shader files every generated shader includes, relative to engine:/data/shaders: what a compiled
 * graph material depends on beside its graph.
 */
auto get_material_graph_shader_includes() -> std::vector<std::string>;

/// The uniform the preview shader reads the node to show from: x = the node's index in the graph.
constexpr const char* MATERIAL_GRAPH_PREVIEW_UNIFORM = "u_mg_preview";

/**
 * @brief The sampler of a graph material's texture @p index (0 to MATERIAL_GRAPH_MAX_TEXTURES - 1). Named by index,
 * not by node, so a renderer registers every sampler a graph material can have before it builds any graph program.
 */
auto get_material_graph_sampler_name(uint32_t index) -> std::string;

/// The texture stage of a graph material's texture @p index: the next free one, past the skinning palette's.
auto get_material_graph_texture_stage(uint32_t index) -> uint8_t;

enum class material_graph_severity : uint8_t
{
    info,
    warning,
    error,
};

/**
 * @brief A problem the compiler found, on a node (and pin) or on the whole graph.
 */
struct material_graph_diagnostic
{
    material_graph_severity severity{material_graph_severity::error};
    /// The node it is about; empty for the graph.
    std::string node_id;
    /// The pin or setting it is about; empty for the whole node.
    std::string pin;
    std::string message;
};

/**
 * @brief A parameter node's uniform: what material instances and scripts set by name.
 */
struct material_graph_parameter
{
    std::string node_id;
    std::string name;
    /// The hint of the parameter's row in the inspector (MATERIAL_PARAMETER_DESCRIPTION_SETTING); may be empty.
    std::string description;
    material_value_type type{material_value_type::float1};
    math::vec4 default_value{};
    /// Where the value sits in the u_mg_params array: the vec4 and its first component.
    uint32_t register_index{};
    uint32_t component{};
    /// The render_change aspects the parameter feeds, by the Surface Output's channels it reaches: what a change of its
    /// value changes.
    render_change::mask changes{render_change::material_aspects};
};

/// What a texture node's texture reads outside 0..1 (MATERIAL_TEXTURE_ADDRESS_SETTING): the texture's own, or the
/// node's.
enum class material_texture_address : uint8_t
{
    texture,
    wrap,
    clamp,
    mirror,
};

/// How a texture node's texture blends its texels (MATERIAL_TEXTURE_FILTER_SETTING).
enum class material_texture_filter : uint8_t
{
    anisotropic,
    trilinear,
    point,
};

/**
 * @brief A texture node's sampler.
 */
struct material_graph_texture
{
    std::string node_id;
    /// The name material instances and scripts set the texture by: the node's Parameter Name, else its id.
    std::string name;
    /// The hint of the texture's row in the inspector (MATERIAL_PARAMETER_DESCRIPTION_SETTING); may be empty.
    std::string description;
    /// The texture; empty when the node names none (it reads the default texture).
    asset_handle<gfx::texture> texture;
    /// The sampler's name in the shader (get_material_graph_sampler_name).
    std::string sampler;
    /// The texture stage (get_material_graph_texture_stage).
    uint32_t stage{};
    bool is_normal_map{};
    /// texture2d, or texture_cube for a cube sampler (a texture of another kind binds the default of this one).
    material_value_type kind{material_value_type::texture2d};
    material_texture_address address{material_texture_address::texture};
    material_texture_filter filter{material_texture_filter::anisotropic};
    /// The render_change aspects the texture feeds, as material_graph_parameter::changes.
    render_change::mask changes{render_change::material_aspects};
};

/**
 * @brief A Static Switch the graph names (MATERIAL_PARAMETER_NAME_SETTING): the material's inspector shows it as a
 * checkbox. It picks code when the material compiles, so a change of it is an edit of the graph.
 */
struct material_graph_static_switch
{
    std::string node_id;
    std::string name;
    std::string description;
    /// The input it passes on: True.
    bool value{};
};

/**
 * @brief The Emissive channel as the GI estimates it to tell a light source: the product of the constants and the
 * parameters along the Multiply nodes linked into Emissive, where a texture reads white. Unknown where Emissive reads
 * anything else: such a material is no light source to the GI.
 */
struct material_graph_emissive_estimate
{
    bool is_known{};
    math::vec3 constant{0.0f};
    /// The parameters multiplied in, by name: a scalar parameter's value, a vector's xyz.
    std::vector<std::string> parameters;
};

/**
 * @brief What the compiler found out about one node.
 */
struct material_graph_node_info
{
    /// The node's value type, its dynamic type resolved.
    material_value_type type{material_value_type::float1};
    /// What each input of the node type reads, resolved (a linked input of Append or Swizzle keeps its own width).
    std::vector<material_value_type> input_types;
    /// What each output of the node type gives, resolved.
    std::vector<material_value_type> output_types;
    /// The node feeds the surface output: its code is in the shader.
    bool is_used{};
    /// The lines of the shader holding the node's code (1-based); 0 when it has none.
    uint32_t first_line{};
    uint32_t line_count{};
};

struct material_graph_compile_options
{
    /// What Quality Switch nodes pass on: their High input, or their Low one.
    material_graph_quality quality{material_graph_quality::high};
    /// Named in the shader's header comment, usually the graph's asset key.
    std::string source_name;
    /// Finds the material functions the graph calls; empty: their files (load_material_function).
    material_function_resolver resolve_function;
    /**
     * Writes the preview shader of the editor's node swatches in place of the G-buffer shader: every node is computed,
     * whether it feeds the output or not, and evaluate_preview(i, node) returns the value of node index `node` (the
     * graph's order) for the swatch (fs_material_graph_preview.sh).
     */
    bool is_preview{};
};

/**
 * @brief A compiled graph: the G-buffer fragment shader and what binding it needs.
 */
struct material_graph_compile_result
{
    /// The whole .eshader text: its variants, inputs and code.
    std::string shader;
    /// The vertex shader's .eshader text when World Position Offset moves the vertices (uses_world_position_offset):
    /// vs_deferred_geom.eshader's variants and vs_velocity.eshader's as velocity_*, moving each vertex by the offset.
    /// Empty for every other graph, which draws with vs_deferred_geom.eshader.
    std::string vertex_shader;
    std::vector<material_graph_diagnostic> diagnostics;
    /// In the graph's order; their registers are laid out per instance first (material_graph_parameter::register_index).
    std::vector<material_graph_parameter> parameters;
    /// The named Static Switches in the shader, in the graph's order.
    std::vector<material_graph_static_switch> static_switches;
    /// The Emissive channel as the GI estimates it.
    material_graph_emissive_estimate emissive;
    /// Size of the u_mg_params array, in vec4s.
    uint32_t parameter_registers{};
    std::vector<material_graph_texture> textures;
    /// One per node of the graph, in the graph's order.
    std::vector<material_graph_node_info> nodes;
    /// The keys of the material functions the graph calls, those they call too, each once.
    std::vector<std::string> functions;
    /// The material is masked: its Opacity Mask clips, or a Discard node does. The shader has the shadow caster
    /// variants (shadow, shadow_linear, shadow_vsm, shadow_vsm_linear), which clip the material's shadow as its surface.
    bool uses_opacity_mask{};
    /// What clips reads the time, the camera or the screen, so its shadow changes without an edit.
    bool is_opacity_mask_animated{};
    /// The Surface Output's Blend Mode: Opaque and Dithered draw in the G-buffer, the others in the translucency pass.
    material_blend_mode blend_mode{material_blend_mode::opaque};
    /// The Surface Output's Shading Model (Unlit for Modulate).
    material_shading_model shading_model{material_shading_model::default_lit};
    /// Translucent surfaces: highlights and reflections at full strength (MATERIAL_PRESERVE_SPECULAR_SETTING).
    bool preserve_specular{true};
    /// Surfaces of the translucency pass: how strongly they mark the TAA reactive mask (0 to 1).
    float taa_response{MATERIAL_DEFAULT_TAA_RESPONSE};
    /// Translucent and Premultiplied surfaces: blended without sorting (MATERIAL_ORDER_INDEPENDENT_SETTING); the shader
    /// has the "oit" variant.
    bool order_independent{};
    /// A lit surface of the translucency pass whose textures leave the GI's stages free: the shader has the "gi" variant
    /// (and "oit_gi" when Order Independent), lit by the GI's radiance cache where the view runs the GI.
    bool has_gi_variant{};
    /// A surface with the "gi" variant whose GI Reflections is on (MATERIAL_GI_REFLECTIONS_SETTING), Translucent or
    /// Premultiplied: the shader has the "gi_layer" variant, which draws the front layer the GI reflections trace from,
    /// and "gi_reflect" (and "oit_gi_reflect"), which read what they traced.
    bool has_gi_reflection_variant{};
    /// Sorted Translucent and Premultiplied surfaces: the depth draws before the colour (MATERIAL_DEPTH_PREPASS_SETTING).
    bool depth_prepass{};
    /// Surfaces of the translucency pass that draw their motion into the velocity buffer (MATERIAL_WRITE_VELOCITY_SETTING).
    bool writes_velocity{};
    /// A surface of the translucency pass that casts a shadow dithered by its Opacity (MATERIAL_CAST_SHADOW_SETTING): the
    /// shader has the shadow caster variants.
    bool casts_forward_shadow{};
    /// World Position Offset moves the vertices: there is a vertex shader, and the shader has the shadow caster
    /// variants, as the shadow moves with the surface.
    bool uses_world_position_offset{};
    /// World Position Offset reads the time or the camera: the vertices move without an edit, and draws need their
    /// velocity and shadow every frame.
    bool is_world_position_offset_animated{};
    /// How far World Position Offset moves a vertex at most, in world units (the Surface Output's setting): what the
    /// bounds of the meshes drawn with it grow by, so the culling keeps what moved out of them.
    float max_world_position_offset{};
    /// The pixel stage reads the object's transform (Object Position, Object Scale, Per Instance Random, Transform from
    /// or to object space): the vertex shader passes it (MATERIAL_OBJECT_DATA).
    bool reads_object_data{};
    /// How many Vertex Interpolators the pixel stage reads (MATERIAL_INTERPOLATORS): values the vertex shader computes.
    uint32_t interpolator_count{};
    /// The pixel stage reads the mesh's second UV set or its vertex colour: the vertex shader passes them (MATERIAL_UV1,
    /// MATERIAL_VERTEX_COLOR).
    bool reads_uv1{};
    bool reads_vertex_color{};
    /// What the material draws: its output node's domain.
    material_domain domain{material_domain::surface};
    /// Decals: the channels written (material_decal_write_bits).
    uint32_t decal_writes{};
    /// Decals: the material reads the surface it lies on (a Scene Texture of the G-buffer, or a Detail normal blend),
    /// so it draws through the decal buffers, where the G-buffer is not a target.
    bool decal_reads_surface{};
    /// Post-process: where it runs and its order there.
    material_post_process_location post_location{material_post_process_location::after_tonemapping};
    int post_priority{};
    /// Decal and post-process: the view textures the Scene Texture nodes read (material_scene_texture_bits).
    uint32_t scene_textures{};
    /// A Scene Texture node reads another pixel than its own (its UV is linked).
    bool samples_scene_neighbours{};

    /// The pixel stage reads what only the graph's own vertex shader writes: draws need that vertex shader, never the
    /// renderer's.
    auto reads_vertex_outputs() const -> bool;
    /// The vertex shader computes something from the graph: an offset or interpolators, which read the material's own
    /// parameter values (never an instance's).
    auto computes_vertex_values() const -> bool;

    /// The surface is see-through: any Blend Mode but Opaque. It casts no shadow and places no distance field.
    auto is_translucent() const -> bool;
    /// The surface draws in the translucency pass (is_material_blend_mode_forward), not the G-buffer.
    auto is_forward() const -> bool;

    auto has_errors() const -> bool;
    /// The diagnostics of one node, worst first.
    auto get_node_diagnostics(hpp::string_view node_id) const -> std::vector<const material_graph_diagnostic*>;
};

/**
 * @brief Generates the G-buffer fragment shader of a material graph.
 *
 * Each node that feeds the surface output becomes one statement, `<type> n_<id> = <code>;`, in dependency order;
 * constants and built-in inputs are written where they are read. A value linked into an input of another width is
 * converted where it is read: a float is broadcast, a wider vector loses its last components, a narrower one gets
 * zeros (alpha 1). The rest of the shader - the G-buffer encoding, dithering, LOD fades and two-sided normals - is
 * the shared engine_data/data/shaders/material_graph/fs_material_graph_gbuffer.sh.
 *
 * The compiler never fails: problems are reported per node, and a graph with errors still produces a shader with the
 * faulty values replaced by their defaults.
 */
auto compile_material_graph(const material_graph& graph, const material_graph_compile_options& options = {})
    -> material_graph_compile_result;

/// The u_mg_params registers of a compiled graph: every parameter's default value in its place.
auto make_material_graph_parameter_registers(const material_graph_compile_result& result) -> std::vector<math::vec4>;

/**
 * @brief What a draw of a graph material binds, as one compile of its graph laid it out: the parameters with their
 * defaults and registers, the textures and their samplers, the named switches, the emissive estimate and the traits of
 * the code. A compiled material keeps the binding of each quality it has code for (graph_material_code), so loading it
 * compiles nothing.
 */
struct material_graph_binding
{
    std::vector<material_graph_parameter> parameters;
    /// The vec4 registers the parameters take (material_graph_compile_result::parameter_registers).
    uint32_t parameter_registers{};
    std::vector<material_graph_texture> textures;
    std::vector<material_graph_static_switch> static_switches;
    material_graph_emissive_estimate emissive;
    bool uses_opacity_mask{};
    bool is_opacity_mask_animated{};
    /// material_graph_compile_result's of the same names.
    material_blend_mode blend_mode{material_blend_mode::opaque};
    material_shading_model shading_model{material_shading_model::default_lit};
    bool preserve_specular{true};
    float taa_response{MATERIAL_DEFAULT_TAA_RESPONSE};
    bool order_independent{};
    bool has_gi_variant{};
    bool has_gi_reflection_variant{};
    bool depth_prepass{};
    bool writes_velocity{};
    bool casts_forward_shadow{};
    bool uses_world_position_offset{};
    bool is_world_position_offset_animated{};
    float max_world_position_offset{};
    /// material_graph_compile_result::reads_vertex_outputs.
    bool reads_vertex_outputs{};
    /// material_graph_compile_result::computes_vertex_values.
    bool computes_vertex_values{};
    /// What the material draws and the traits of its domain (material_graph_compile_result's of the same names).
    material_domain domain{material_domain::surface};
    uint32_t decal_writes{};
    bool decal_reads_surface{};
    material_post_process_location post_location{material_post_process_location::after_tonemapping};
    int post_priority{};
    uint32_t scene_textures{};
    bool samples_scene_neighbours{};
};

/// The binding of a compiled graph.
auto make_material_graph_binding(const material_graph_compile_result& result) -> material_graph_binding;

/// The u_mg_params registers of a binding: every parameter's default value in its place.
auto make_material_graph_parameter_registers(const material_graph_binding& binding) -> std::vector<math::vec4>;

/// Whether nodes of type @p def have a preview swatch: every node with a value of its own.
auto has_material_node_preview(const material_node_def& def) -> bool;

/// Whether the preview shader shows a swatch for node @p index of a compiled graph (has_material_node_preview).
auto has_material_graph_preview(const material_graph& graph, const material_graph_compile_result& result, size_t index) -> bool;

/**
 * @brief Why output @p from_pin of node @p from_node cannot be linked into input @p to_pin of node @p to_node;
 * empty when it can.
 *
 * @p from_pin may pick components ("rgba.r"), @p to_pin may name one component of a vector input ("base_color.r").
 * A link is refused when the pins do not exist, the input takes no link, or the graph would have a problem it does
 * not have now: a loop, or a vector going into a float - through this link or any node it widens.
 */
auto check_material_graph_link(const material_graph& graph,
                               hpp::string_view from_node,
                               hpp::string_view from_pin,
                               hpp::string_view to_node,
                               hpp::string_view to_pin) -> std::string;

} // namespace unravel
