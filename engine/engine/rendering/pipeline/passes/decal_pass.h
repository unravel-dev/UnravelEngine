#pragma once

#include <engine/layers/layer_mask.h>
#include <engine/rendering/feature_limits.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/material.h>
#include <engine/rendering/material_graph/graph_material_programs.h>
#include <graphics/render_view.h>
#include <hpp/span.hpp>
#include <math/math.h>

#include <entt/entt.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace unravel
{
class scene;
class camera;
class depth_copy_pass;
class graph_material;
class mask_pass;
namespace rendering
{
struct visibility_data;
}

/**
 * @brief Draws the Decal components a camera sees into its G-buffer, after the G-buffer of the renderers that receive
 * decals and before any lighting reads it.
 *
 * collect() culls the decals by layer, frustum, distance and screen size, fades them (opacity, screen size, distance,
 * lifetime) and sorts them by Sort Order, then by material and entity, so overlaps draw in a fixed order. run()
 * draws them, a box per decal, instanced per material:
 *
 * - the colour path, while no decal writes a normal, metallic or roughness or reads the surface (or the limits'
 *   decal_surface_channels are off): one view into the G-buffer's base colour and emissive;
 * - the full path otherwise: the decals into three decal buffers (base colour, world normal, metallic and roughness,
 *   each premultiplied with the share of the surface left in alpha) and the emissive, then a resolve over the decals'
 *   rectangle applies the buffers to the G-buffer. The G-buffer's octahedral normal cannot blend in the hardware.
 *
 * Every decal pixel reads a copy of the depth (the depth itself is the target the boxes test against).
 */
class decal_pass
{
public:
    /// What the last run drew.
    struct stats
    {
        uint32_t decals{};
        uint32_t draws{};
        bool is_full_path{};
    };

    /// Loads the programs and makes the box; called once, after bgfx and the asset manager are up.
    auto init(rtti::context& ctx) -> bool;
    void deinit();

    /// The decals a run of @p cam draws: culled, faded and sorted. A run without decals draws none of the passes.
    void collect(scene& scn, const camera& cam, const feature_limits& limits, layer_mask render_mask);

    /// Whether the last collect found a decal to draw.
    auto has_decals() const -> bool;

    /**
     * @brief Draws the collected decals into the G-buffer of @p rview, which holds @p receivers. While a decal keeps to
     * some Receiver Layers, @p masks first draws the receivers' layers into a mask of the surfaces the view shows, which
     * each decal pixel tests.
     */
    void run(gfx::render_view& rview,
             const camera& cam,
             const feature_limits& limits,
             depth_copy_pass& depth_copy,
             mask_pass& masks,
             hpp::span<const rendering::visibility_data> receivers);

    auto get_stats() const -> const stats&;

    /// While on, run() also counts every fragment the decals' boxes shade into DECAL_OVERDRAW (R16F), the decal
    /// overdraw debug view's input.
    void set_overdraw_view(bool is_on);

private:
    /// One decal as it draws: its box, its fades, the material its draw binds and its per-instance registers.
    struct decal_item
    {
        /// The material whose program and values the draw binds: the shared one for decals that share its draw, the
        /// decal's resolved material otherwise.
        std::shared_ptr<material> keep_alive;
        const graph_material* program_material{};
        math::mat4 box_to_world{1.0f};
        math::mat4 world_to_box{1.0f};
        /// x: the fade it is drawn with; y: its age; z: its normalized age; w: its lifetime.
        math::vec4 fade{};
        /// x, y: the cosines where the angle fade starts and ends; z, w: the low and high 16 bits of its Receiver
        /// Layers.
        math::vec4 angle{};
        std::array<math::vec4, 4> registers{};
        int sort_order{};
        uint32_t entity{};
        /// The camera is in (or near) the box: it draws its back faces.
        bool is_camera_inside{};
    };

    void draw_decals(gfx::render_view& rview,
                     const camera& cam,
                     const gfx::frame_buffer* target,
                     bool is_full_path,
                     const gfx::texture::ptr& depth,
                     const gfx::texture::ptr& gbuffer0,
                     const gfx::texture::ptr& gbuffer1,
                     const gfx::texture::ptr& layers);
    /// The layers of each receiver the view shows inside a decal that keeps to some layers, a byte per channel
    /// (DECAL_LAYERS); null while no decal keeps to some layers.
    auto draw_layers(gfx::render_view& rview,
                     const camera& cam,
                     mask_pass& masks,
                     hpp::span<const rendering::visibility_data> receivers,
                     const gfx::texture::ptr& depth) -> gfx::texture::ptr;
    void resolve(gfx::render_view& rview, const gfx::frame_buffer::ptr& gbuffer);

    struct resolve_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_decal_a, "s_decal_a", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_decal_b, "s_decal_b", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_decal_c, "s_decal_c", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_decal_surface, "s_decal_surface", bgfx::UniformType::Sampler);
        }

        gfx::program::uniform_ptr s_decal_a;
        gfx::program::uniform_ptr s_decal_b;
        gfx::program::uniform_ptr s_decal_c;
        gfx::program::uniform_ptr s_decal_surface;
        std::unique_ptr<gpu_program> program;
    };

    /// What every decal program reads besides its material: the view and its depth and G-buffer.
    struct decal_uniforms : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(nullptr, u_decal_inv_view_proj, "u_decal_inv_view_proj", bgfx::UniformType::Mat4);
            cache_uniform(nullptr, u_decal_view, "u_decal_view", bgfx::UniformType::Vec4);
            cache_uniform(nullptr, s_decal_depth, "s_decal_depth", bgfx::UniformType::Sampler);
            cache_uniform(nullptr, s_decal_gbuffer0, "s_decal_gbuffer0", bgfx::UniformType::Sampler);
            cache_uniform(nullptr, s_decal_gbuffer1, "s_decal_gbuffer1", bgfx::UniformType::Sampler);
            cache_uniform(nullptr, s_decal_layers, "s_decal_layers", bgfx::UniformType::Sampler);
        }

        gfx::program::uniform_ptr u_decal_inv_view_proj;
        gfx::program::uniform_ptr u_decal_view;
        gfx::program::uniform_ptr s_decal_depth;
        gfx::program::uniform_ptr s_decal_gbuffer0;
        gfx::program::uniform_ptr s_decal_gbuffer1;
        gfx::program::uniform_ptr s_decal_layers;
    } uniforms_;

    graph_material_programs programs_;
    resolve_program copy_program_;
    resolve_program color_program_;
    resolve_program surface_program_;
    /// The decal overdraw view's program: the box (vs_decal) counting its fragments (fs_decal_overdraw).
    std::unique_ptr<gpu_program> overdraw_program_;
    bool is_overdraw_view_{};
    bgfx::VertexBufferHandle box_vertices_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle box_indices_ = BGFX_INVALID_HANDLE;

    std::vector<decal_item> items_;
    /// A collected decal writes a normal, metallic or roughness, or reads the surface: the full path draws them.
    bool wants_full_path_{};
    /// The world bounds of the collected decals that keep to some Receiver Layers: the receivers they touch have
    /// their layers drawn for those decals' pixels to test.
    std::vector<math::bbox> layer_bounds_;
    /// The view's rectangle the collected decals cover, in pixels: what the resolve passes draw.
    irect32_t screen_rect_{};
    stats stats_;
};

} // namespace unravel
