#pragma once
#include <engine/engine_export.h>

#include "material_graph_compiler.h"

#include <engine/assets/asset_handle.h>
#include <engine/rendering/gpu_program.h>

#include <graphics/shader.h>
#include <graphics/texture.h>
#include <hpp/string_view.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace unravel
{
class graph_material;
class material;

/// The sampler state a material texture is read with: its own (wrap, border...) with anisotropic filtering, unless it
/// asks for point sampling; a texture node's Address and Filter settings replace the texture's own.
auto get_material_sampler_flags(const gfx::texture::ptr& texture,
                                material_texture_address address = material_texture_address::texture,
                                material_texture_filter filter = material_texture_filter::anisotropic) -> std::uint32_t;

/// The seconds graph materials animate with this render frame (u_material_time.x) and the frame before (.y): real time
/// since the first call, read once per frame so every pass of a frame draws the same moment. The frame before is the
/// last one that read, at most MAX_MATERIAL_FRAME_SECONDS back: after a pause in drawing, a frame's motion stays one
/// frame's worth.
auto get_material_graph_times() -> math::vec2;

/// The seconds graph materials animate with this render frame (get_material_graph_times().x).
auto get_material_graph_time() -> float;

/**
 * @brief The programs graph materials draw with, and what their draws bind.
 *
 * One program per material shader object and pair of variants, made at its first draw and released after
 * PROGRAM_IDLE_FRAMES frames without one (release_unused): the program holds its shaders, so a material whose
 * recompile replaced its shader, or one no longer drawn, lets them go then. The vertex shader is vs_deferred_geom.eshader, whose variants (default, skinned, instanced,
 * skinned_instanced, skinned_baked_instanced, card_capture) every geometry draw of the renderer has, or the material's
 * own for a graph whose World Position Offset moves its vertices, with the same variants; the graph shader's variant is
 * the pass's (default, card_capture, the shadow casters). Passes of their own fragment shader - the velocity pass, the
 * editor's selection outline and picking - draw such a graph with its vertex shader too (begin_moved_vertices).
 *
 * init() registers every uniform a graph shader can declare before any of these programs exists (the uniforms_cache
 * order contract): the samplers are named by index for that. A graph whose pixels read what only its own vertex shader
 * writes draws nothing until that shader is loaded.
 */
class graph_material_programs : public uniforms_cache
{
public:
    void init(asset_handle<gfx::shader> vertex_shader);

    /**
     * @brief The program drawing @p mat with the vertex shader's @p vertex_variant and its shader's @p
     * fragment_variant, made at the first call; null while it cannot run: no shader yet, one of no such variant, or one
     * that does not build.
     */
    auto begin(const graph_material& mat, const std::string& vertex_variant, const std::string& fragment_variant) -> gpu_program*;

    /**
     * @brief The program drawing @p mat in a pass of @p fragment_shader when it is a graph material whose World Position
     * Offset moves its vertices: its vertex shader's @p vertex_variant, made at the first call, begun with the graph's
     * values and @p camera_position (what the offset reads, the G-buffer's) bound. Null for every other material, and
     * while the vertex shader is not there: the pass draws it with its own program, as the G-buffer does then.
     */
    auto begin_moved_vertices(const material& mat,
                              const std::string& vertex_variant,
                              const asset_handle<gfx::shader>& fragment_shader,
                              const math::vec3& camera_position) -> gpu_program*;

    /// The variant of a moved-vertices vertex shader that draws the velocity of @p vertex_variant's draws: "velocity"
    /// for the default, "velocity_" + the variant for the others.
    static auto get_velocity_variant(const std::string& vertex_variant) -> std::string;

    /// The variant of a moved-vertices vertex shader that writes the moved position alone, for fragment shaders that
    /// read nothing (the editor's picking and selection outline): bgfx pairs only stages whose varyings match.
    static auto get_position_only_variant(bool is_skinned) -> const char*;

    /// Binds @p mat's textures and parameters, and the time, for the next draw.
    void submit_values(const graph_material& mat) const;

    /// Binds @p registers (u_mg_params) and @p textures, and the times, for the next draw.
    void submit_values(const std::vector<math::vec4>& registers, const std::vector<material_graph_texture>& textures) const;

    /// Sets the camera position the next draw evaluates the graph for (u_camera_wpos), for passes whose views are not
    /// the camera's, such as shadow maps.
    void submit_camera_position(const math::vec3& position) const;

    /// Releases the programs no draw began for PROGRAM_IDLE_FRAMES frames.
    void release_unused();

    /// Frames a program lives without a draw before release_unused releases it.
    static constexpr uint32_t PROGRAM_IDLE_FRAMES = 120;

private:
    /// What tells programs apart: the material's shader object (its compile), the vertex and fragment shader objects
    /// (null for this cache's own vertex shader) and their variants.
    struct program_key
    {
        const gfx::shader* material_shader{};
        const gfx::shader* vertex_shader{};
        const gfx::shader* fragment_shader{};
        std::string vertex_variant;
        std::string fragment_variant;
    };

    /// A program_key a draw looks up without copying the variant names.
    struct program_key_view
    {
        const gfx::shader* material_shader{};
        const gfx::shader* vertex_shader{};
        const gfx::shader* fragment_shader{};
        hpp::string_view vertex_variant;
        hpp::string_view fragment_variant;
    };

    /// Orders program_key and program_key_view alike, so a view finds a key.
    struct program_key_less
    {
        using is_transparent = void;

        template<typename Lhs, typename Rhs>
        auto operator()(const Lhs& lhs, const Rhs& rhs) const -> bool
        {
            return to_tuple(lhs) < to_tuple(rhs);
        }

        template<typename Key>
        static auto to_tuple(const Key& key)
        {
            return std::make_tuple(key.material_shader,
                                   key.vertex_shader,
                                   key.fragment_shader,
                                   hpp::string_view(key.vertex_variant),
                                   hpp::string_view(key.fragment_variant));
        }
    };

    struct program_entry
    {
        std::unique_ptr<gpu_program> program;
        /// The render frame of its last draw (release_unused).
        uint32_t last_used_frame{};
    };

    /// The program of @p key, made of the two shaders and variants at the first call; null while it cannot run.
    auto begin_entry(const program_key_view& key,
                     const asset_handle<gfx::shader>& vertex_shader,
                     const asset_handle<gfx::shader>& fragment_shader) -> gpu_program*;

    std::vector<gfx::program::uniform_ptr> samplers_;
    gfx::program::uniform_ptr parameters_;
    gfx::program::uniform_ptr normal_channels_;
    gfx::program::uniform_ptr time_;
    gfx::program::uniform_ptr camera_position_;
    asset_handle<gfx::shader> vertex_shader_;
    std::map<program_key, program_entry, program_key_less> programs_;
};

} // namespace unravel
