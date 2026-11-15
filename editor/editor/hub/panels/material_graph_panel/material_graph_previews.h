#pragma once

#include <context/context.hpp>
#include <engine/assets/asset_handle.h>
#include <engine/meta/rendering/shader_variants.hpp>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/graph_material_programs.h>

#include <graphics/frame_buffer.h>
#include <graphics/texture.h>
#include <imgui/imgui.h>
#include <threadpp/thread_pool.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief What the Material Graph window previews of the graph on screen, drawn with shaders it compiles itself: a
 * swatch of every node's value (the preview shader, material_graph_compile_options::is_preview) and the material
 * (its G-buffer shader, for the 3D preview).
 *
 * Both compile in the background, for the editor's renderer only, once the graph's code has rested a moment after a
 * change; until then the previews keep the last compiled code with its own values, each node its own swatch. A change
 * of values alone, which leaves the code as it is (a parameter, a texture), reaches them at once. A graph with errors
 * is not compiled: the previews keep the last code that was.
 */
class material_graph_previews
{
public:
    void init(rtti::context& ctx);
    void deinit();

    /// Forgets the graph followed so far, its values and textures among them: a compile running is dropped when it
    /// finishes, and the previews draw nothing until the next graph compiles.
    void reset();

    /**
     * @brief Follows @p graph: takes a finished compile, gives the previews the graph's values when its code is the
     * compiled one, and starts the compile of new code once it rests.
     * @param surface The graph's compile, as the window made it: the 3D preview draws its shader.
     * @param revision Changes with every change of the graph's code or values.
     * @param name Names the graph in the generated code's comments.
     */
    void update(rtti::context& ctx,
                const material_graph& graph,
                const material_graph_compile_result& surface,
                uint64_t revision,
                const std::string& name);

    /// Draws the swatches of the graph's nodes into the atlas: after a change, and while the graph animates.
    void render_swatches(const material_graph& graph);

    /// The atlas holding the swatches; null before init.
    auto get_swatch_texture() const -> const gfx::texture::ptr&;

    /// The region of the atlas holding the swatch of node @p node_id; false while it has none to show.
    auto get_swatch_uv(const std::string& node_id, ImVec2& uv0, ImVec2& uv1) const -> bool;

    /// The graph as a material, with the shader compiled for it, for the 3D preview; drawn as a default material until
    /// the first compile.
    auto get_material() const -> const asset_handle<material>&;

    /// A compile is waiting for the code to rest, or running.
    auto is_compiling() const -> bool;

    /// The last compile failed; the log says why.
    auto has_failed() const -> bool;

    /// The graph has errors, so its code is not compiled.
    auto has_graph_errors() const -> bool;

private:
    /// What a draw of the swatches binds: the u_mg_params registers and the textures of the preview code.
    struct swatch_values
    {
        std::vector<math::vec4> registers;
        std::vector<material_graph_texture> textures;
    };

    /// The code the previews draw with.
    struct swatch_code
    {
        std::string code_hash;
        /// The nodes with a swatch, by id: the index the preview code knows each one by, its tile in the atlas.
        std::map<std::string, size_t> tiles;
        bool is_animated{};
    };

    /// One compile's input: the shaders' code, its identity and what it is drawn with.
    struct compile_request
    {
        std::string preview_code;
        std::string surface_code;
        /// Empty while World Position Offset moves nothing.
        std::string vertex_code;
        swatch_code code;
        swatch_values values;
        material_graph graph;
    };

    struct compile_output
    {
        bool is_compiled{};
        std::vector<compiled_shader_variant> preview;
        std::vector<compiled_shader_variant> surface;
        std::vector<compiled_shader_variant> vertex;
    };

    void take_compile(rtti::context& ctx);
    void start_compile(rtti::context& ctx);

    graph_material_programs programs_;
    gfx::program::uniform_ptr preview_uniform_;
    asset_handle<gfx::shader> quad_shader_;
    std::unique_ptr<gpu_program> swatch_program_;
    gfx::texture::ptr atlas_;
    gfx::frame_buffer::ptr atlas_target_;

    std::shared_ptr<graph_material> material_;
    asset_handle<material> material_handle_;

    /// The code the previews draw with, and its values.
    swatch_code shown_code_;
    swatch_values shown_values_;

    /// The newest code waiting to compile; an empty code hash for none.
    compile_request wanted_;
    compile_request running_;
    std::chrono::steady_clock::time_point changed_at_{};
    tpp::shared_future<compile_output> job_;
    bool has_failed_{};
    bool has_graph_errors_{};

    uint64_t revision_{~uint64_t(0)};
    bool is_dirty_{true};
    std::chrono::steady_clock::time_point rendered_at_{};
};

} // namespace unravel
