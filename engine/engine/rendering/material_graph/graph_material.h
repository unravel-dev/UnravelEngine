#pragma once
#include <engine/engine_export.h>

#include "material_graph.h"
#include "material_graph_compiler.h"

#include <engine/rendering/material.h>

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <map>
#include <memory>
#include <vector>

namespace unravel
{

/// A value given to a graph material's parameter by name, over the graph's default.
struct graph_parameter_value
{
    std::string name;
    math::vec4 value{};

    auto operator==(const graph_parameter_value& other) const -> bool = default;
};

/// A texture given to a graph material's texture node by name (the node's id), over the graph's.
struct graph_texture_value
{
    std::string name;
    asset_handle<gfx::texture> texture;

    auto operator==(const graph_texture_value& other) const -> bool = default;
};

/**
 * @class graph_material
 * @brief A material made of a material graph: the deferred pipeline draws it with the G-buffer shader generated from
 * the graph.
 *
 * The shader is part of the compiled material: compiling the asset generates the code and compiles it, once per
 * renderer, into files beside the compiled material (get_graph_material_shader_file), each marked with the hash of the
 * code (get_shader_hash). The compiled material keeps what that compile laid out (get_binding): the parameters' default
 * values packed as the shader's u_mg_params registers, the textures and the traits of the code, so loading it compiles
 * nothing; the loader gives it its renderer's shader (get_shader). A deploy that cooks another quality cooks the
 * material too, with that quality's hash and binding (set_compiled_code). Editing follows the graph at once: set_graph
 * compiles it for what a draw binds, and its code reaches the GPU with the next compile of the asset.
 *
 * Parameters and textures take values by name over the graph's (set_parameter, set_texture): what scripts set at run
 * time and material instances override. They are not saved with the material: the graph holds its defaults.
 *
 * A graph with a vertex stage has a vertex shader too (get_vertex_shader), compiled into the same files: its World
 * Position Offset moves the vertices, or its pixels read what that shader writes (the object's transform, Vertex
 * Interpolators). Every geometry pass draws it with that one; where the offset moves the vertices, the velocity pass
 * with its velocity variants, and its shadow with the shader's own casters (draws_own_shadow).
 */
class graph_material : public crtp_meta_type<graph_material, material>
{
public:
    SERIALIZABLE(graph_material)

    graph_material();

    auto clone() const -> std::shared_ptr<material> override;

    void copy_unreflected_values(const material& source) override;

    /// Takes the values @p source's parameters and textures were given by name, not its graph or shaders: what a
    /// script's edits of a shared material during play are undone with.
    void copy_values(const graph_material& source);

    /// The graph the material is made of.
    auto get_graph() const -> const material_graph&;

    /// Replaces the graph and compiles it for what a draw binds; the graph gets a surface output when it has none.
    /// Values set by name stay, for the parameters and textures the new graph still has.
    void set_graph(material_graph graph);

    /// What a draw binds, as the compile of the graph's code laid it out, before the values set by name.
    auto get_binding() const -> const material_graph_binding&;

    /// Takes the hash and the binding of the graph compiled at another quality (a deploy's cook): the code its
    /// shader files hold, and what a draw of that code binds.
    void set_compiled_code(std::string shader_hash, material_graph_binding binding);

    /// The shader generated from the graph, for the renderer that draws; empty until the asset compiled it for that
    /// renderer. Set when the material loads, never saved.
    auto get_shader() const -> const asset_handle<gfx::shader>&;

    void set_shader(asset_handle<gfx::shader> shader);

    /// The vertex shader generated from the graph when its World Position Offset moves the vertices, for the renderer
    /// that draws; empty for every other graph, and until the asset compiled it. Set when the material loads, never
    /// saved.
    auto get_vertex_shader() const -> const asset_handle<gfx::shader>&;

    void set_vertex_shader(asset_handle<gfx::shader> shader);

    /// Whether a draw shows the material as it is, not the stand-in surface or default textures: its shaders are
    /// compiled and loaded, and so is every texture it samples. Starts the loads it finds missing, and never waits.
    auto is_ready_to_draw() const -> bool;

    /// The hash of the code the compiled material's shaders were compiled from: a shader file of another hash belongs
    /// to another version of the graph. Saved in the compiled material only.
    auto get_shader_hash() const -> const std::string&;

    void set_shader_hash(std::string hash);

    /// Copies of the material functions the graph calls, by key: what its shader was compiled from, and what it
    /// compiles with before their files (a game has none). Saved in the compiled material only.
    using function_copies = std::map<std::string, std::shared_ptr<const material_graph>>;
    auto get_functions() const -> const function_copies&;
    void set_functions(function_copies functions);

    /// Takes a copy of every material function the graph calls from its file, and drops the others. False when one
    /// has no file.
    auto copy_functions() -> bool;

    /// The graph's parameters: what set_parameter takes, each with its type, default and register.
    auto get_parameters() const -> const std::vector<material_graph_parameter>&;

    /// The graph's parameter named @p name; null when it has none.
    auto find_parameter(hpp::string_view name) const -> const material_graph_parameter*;

    /// What a draw gives parameter @p name: the value set_parameter gave it, else the graph's default. Empty when the
    /// graph has no such parameter.
    auto get_parameter(hpp::string_view name) const -> hpp::optional<math::vec4>;

    /**
     * @brief Gives parameter @p name @p value over the graph's default. A float parameter reads x, a vector the
     * components it has.
     * @return False when the graph has no such parameter.
     */
    auto set_parameter(hpp::string_view name, const math::vec4& value) -> bool;

    /// The values set_parameter gave, in the order they were first set.
    auto get_parameter_values() const -> const std::vector<graph_parameter_value>&;

    /// The first texture named @p name (its node's Parameter Name, else its id) among the textures of a draw; null when
    /// the graph has none.
    auto find_texture(hpp::string_view name) const -> const material_graph_texture*;

    /// What a draw samples for texture node @p name: the texture set_texture gave it, else the graph's. Empty when the
    /// graph has no such node, or it names no texture.
    auto get_texture(hpp::string_view name) const -> asset_handle<gfx::texture>;

    /**
     * @brief Gives the texture nodes named @p name (their Parameter Name, else their id) @p texture over the graph's;
     * an empty handle samples the default texture. Several nodes of one name (calls of a function holding one) read it
     * all.
     * @return False when the graph has no such texture node.
     */
    auto set_texture(hpp::string_view name, const asset_handle<gfx::texture>& texture) -> bool;

    /// The textures set_texture gave, in the order they were first set.
    auto get_texture_values() const -> const std::vector<graph_texture_value>&;

    /// The u_mg_params registers of a draw: every parameter's value in its place.
    auto get_parameter_registers() const -> const std::vector<math::vec4>&;

    /// The textures of a draw, each with its sampler and stage; an empty handle reads the default texture.
    auto get_textures() const -> const std::vector<material_graph_texture>&;

    /// The graph's named Static Switches, which the material's inspector shows; a change of one is an edit of the graph.
    auto get_static_switches() const -> const std::vector<material_graph_static_switch>&;

    /// What the surface gives off as the GI estimates it (material_graph_emissive_estimate), with the parameters'
    /// values as a draw gives them; black where the graph's Emissive cannot be estimated.
    auto get_emissive_estimate() const -> math::vec3;

    /// The graph clips (its Opacity Mask, or a Discard node): its shader has shadow caster variants, and its shadow is
    /// clipped too.
    auto uses_opacity_mask() const -> bool;

    /// The Opacity Mask reads the time, the camera or the screen: its shadow changes every frame without an edit.
    auto is_opacity_mask_animated() const -> bool;

    /// The graph's World Position Offset moves the vertices: draws take the vertex shader (get_vertex_shader).
    auto uses_world_position_offset() const -> bool;

    /// The graph's pixels read what only its own vertex shader writes (the object's transform, Vertex Interpolators):
    /// draws need that vertex shader, and wait for it.
    auto reads_vertex_outputs() const -> bool;

    /// The graph has a vertex shader of its own (an offset, or pixels that read its outputs): geometry draws take it.
    auto has_vertex_stage() const -> bool;

    /// The vertex shader computes values from the material's parameters (an offset, interpolators): an instance's own
    /// values would not reach it, so instances overriding parameters draw on their own.
    auto computes_vertex_values() const -> bool;

    /// World Position Offset reads the time or the camera: the vertices move every frame without an edit.
    auto is_world_position_offset_animated() const -> bool;

    /// How far World Position Offset moves a vertex at most, in world units: what the bounds of the meshes drawn with
    /// the material grow by.
    auto get_max_world_position_offset() const -> float;

    /// The shader's own shadow casters draw the shadow: the Opacity Mask clips it, or World Position Offset moves it.
    auto draws_own_shadow() const -> bool;

    /// The surface is see-through (the Surface Output's Translucent): dithered out by its Opacity Mask.
    auto is_translucent() const -> bool;

    /// The material casts a shadow: every one but a translucent one.
    auto casts_shadow() const -> bool;

    /// The shadow changes every frame without an edit: an animated Opacity Mask or World Position Offset.
    auto is_shadow_animated() const -> bool;

    /// Whether a graph material alive now grows bounds by its World Position Offset (get_max_world_position_offset
    /// above zero): while none does, the bounds of models skip looking for one.
    static auto has_offset_bounds() -> bool;

    /// Whether a graph material alive now has a shadow that changes every frame (is_shadow_animated): while none does,
    /// the shadow casters skip looking for one.
    static auto has_animated_shadows() -> bool;

    /// The first MAX_INSTANCE_CONSTANTS u_mg_params registers: what the shader's instance_material variant reads per
    /// instance (material_graph_compiler.h).
    auto write_instance_constants(math::vec4* out) const -> uint32_t override;

protected:
    /// Each texture's content identity for the aspects it feeds, and the shaders' for every aspect.
    void add_content_versions(render_change_values& keys) const override;

private:
    /**
     * @brief Takes @p graph with @p binding, what a draw of it binds. What changed is marked: everything for new code
     * (or new @p is_code_kept false), else the aspects of the parameters and textures whose defaults changed, as
     * set_parameter and set_texture mark them.
     */
    void bind_graph(material_graph graph, material_graph_binding binding, bool is_code_kept);
    /// Compiles @p graph, with the functions the material has, for what a draw binds.
    auto compile_binding(const material_graph& graph) const -> material_graph_binding;
    /// Takes @p binding and puts the values set by name over its defaults (apply_values).
    void apply_binding(material_graph_binding binding);
    /// Takes the graph, hash and binding of a compiled material as they were saved, compiling nothing.
    void take_compiled(material_graph graph, std::string shader_hash, material_graph_binding binding);
    /// Puts the values set by name over the binding's defaults.
    void apply_values();
    /// Takes @p source's values set by name, marking what they feed changed; false when they were these already. The
    /// caller applies them (apply_values, or a compile).
    auto take_values(const graph_material& source) -> bool;
    /// Writes @p value into the registers of @p parameter.
    void write_parameter(const material_graph_parameter& parameter, const math::vec4& value);
    /// Gives every texture named @p name @p texture; the aspects they feed.
    auto write_texture(hpp::string_view name, const asset_handle<gfx::texture>& texture) -> render_change::mask;
    auto find_parameter_value(hpp::string_view name) -> graph_parameter_value*;
    auto find_texture_value(hpp::string_view name) -> graph_texture_value*;

    /// The function at @p key: the copy where there is one (what the shader was compiled from), else its file.
    auto resolve_function(const std::string& key) const -> std::shared_ptr<const material_graph>;

    /// The counts of live graph materials: those growing bounds (has_offset_bounds), and those whose shadow animates
    /// (has_animated_shadows).
    enum class live_count_kind : uint8_t
    {
        offset_bounds,
        animated_shadow,
    };

    /// Counts its material in the live count of its kind while set, copies and all.
    class live_count_registration
    {
    public:
        explicit live_count_registration(live_count_kind kind);
        live_count_registration(const live_count_registration& other);
        auto operator=(const live_count_registration& other) -> live_count_registration&;
        ~live_count_registration();

        void set_counted(bool is_counted);

    private:
        live_count_kind kind_;
        bool is_counted_{};
    };

    material_graph graph_;
    function_copies functions_;
    asset_handle<gfx::shader> shader_;
    asset_handle<gfx::shader> vertex_shader_;
    std::string shader_hash_;
    material_graph_binding binding_;
    /// The binding's registers, before the values set by name.
    std::vector<math::vec4> default_registers_;
    std::vector<math::vec4> parameter_registers_;
    std::vector<material_graph_texture> textures_;
    std::vector<graph_parameter_value> parameter_values_;
    std::vector<graph_texture_value> texture_values_;
    live_count_registration offset_bounds_registration_{live_count_kind::offset_bounds};
    live_count_registration animated_shadow_registration_{live_count_kind::animated_shadow};
};

} // namespace unravel
