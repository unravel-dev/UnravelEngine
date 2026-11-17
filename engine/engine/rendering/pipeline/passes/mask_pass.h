#pragma once

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/material_graph/graph_material_programs.h>
#include <graphics/render_view.h>
#include <hpp/optional.hpp>
#include <hpp/span.hpp>
#include <math/math.h>

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace unravel
{
class camera;
namespace rendering
{
struct visibility_data;
}

/**
 * @brief Draws renderers into a mask: one value per renderer, written over the pixels it covers, with the renderer's
 * position alone (vs_mask.eshader, or the material's own vertex shader where its World Position Offset moves the
 * vertices).
 *
 * Two kinds: a mask with a depth of its own, where the nearest masked renderer wins whether the scene hides it or not
 * (the post-process mask: outlines and silhouettes seen through walls), and a mask of the surfaces the view shows,
 * which keeps only the fragments lying at the scene's depth (the decal layers).
 */
class mask_pass
{
public:
    /// What one run draws into.
    struct run_params
    {
        /// The mask's name in the render view; a mask with a depth of its own names it <name>_DEPTH.
        const char* name{};
        const char* pass_name{};
        bgfx::TextureFormat::Enum format{bgfx::TextureFormat::R8};
        /// What a pixel no renderer covers holds (RGBA, a byte each, R in the highest byte).
        uint32_t clear_rgba{};
        /// Null: the mask has a depth of its own. Else the depth of the surfaces the view shows: only fragments at it
        /// are kept.
        gfx::texture::ptr visible_depth;
    };

    /// The value a renderer writes, a byte per channel as 0 to 1; none leaves it out of the mask.
    using value_function = std::function<hpp::optional<math::vec4>(entt::handle)>;

    /// Loads the programs; called once, after bgfx and the asset manager are up.
    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Draws the renderers of @p models (the camera's visible set, at the LODs it chose) that @p value_of gives a
     * value into the mask of @p params, cleared first. @return The mask's frame buffer: the mask, then its depth for a
     * mask with one.
     */
    auto run(gfx::render_view& rview,
             const camera& cam,
             hpp::span<const rendering::visibility_data> models,
             const value_function& value_of,
             const run_params& params) -> gfx::frame_buffer::ptr;

private:
    struct mask_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_mask_value, "u_mask_value", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), s_mask_scene_depth, "s_mask_scene_depth", bgfx::UniformType::Sampler);
        }

        gfx::program::uniform_ptr u_mask_value;
        gfx::program::uniform_ptr s_mask_scene_depth;
        std::unique_ptr<gpu_program> program;
    };

    /// The program for a draw: skinned or not, of the visible-surface variant or not.
    auto get_program(bool is_skinned, bool is_visible_only) -> mask_program&;

    mask_program program_;
    mask_program program_skinned_;
    mask_program visible_program_;
    mask_program visible_program_skinned_;
    graph_material_programs moved_programs_;
    asset_handle<gfx::shader> fragment_shader_;
};

} // namespace unravel
