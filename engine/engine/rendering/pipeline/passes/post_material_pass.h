#pragma once

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/material_graph/graph_material_programs.h>
#include <engine/rendering/material_graph/material_graph.h>
#include <graphics/render_view.h>

#include <vector>

namespace unravel
{
class camera;
struct pre_exposure_state;
struct post_process_material_draw;
class blit_pass;

/**
 * @brief Runs post-process materials over a view at one of their Locations: one full-screen pass each, in their order,
 * from the location's colour into a target of its own and on to the next, the last one back into the location's colour.
 *
 * Each pass reads the colour (s_post_color) and binds what the material's Scene Texture nodes read: the depth, the
 * G-buffer, the velocity. Scene-referred locations bind the view's pre-exposure, which the material's main takes out on
 * read and puts back on write (mg_post.sh).
 */
class post_material_pass
{
public:
    /// What one location's run reads besides the colour.
    struct run_params
    {
        material_post_process_location location{};
        /// The location's colour: read first, written by the last material.
        gfx::frame_buffer::ptr color;
        gfx::texture::ptr depth;
        /// The G-buffer's targets 0 to 2, and the velocity buffer (null without one).
        gfx::texture::ptr gbuffer0;
        gfx::texture::ptr gbuffer1;
        gfx::texture::ptr gbuffer2;
        gfx::texture::ptr velocity;
        /// The post-process mask and its depth (mask_pass.cpp), while a material reads them.
        gfx::texture::ptr custom_mask;
        gfx::texture::ptr custom_depth;
        const pre_exposure_state* pre_exposure{};
    };

    /// Loads the programs; called once, after bgfx and the asset manager are up.
    auto init(rtti::context& ctx) -> bool;

    /// Runs the draws of @p params.location among @p draws, in order. @return How many ran.
    auto run(gfx::render_view& rview,
             const camera& cam,
             const std::vector<post_process_material_draw>& draws,
             const run_params& params,
             blit_pass& blit) -> uint32_t;

private:
    struct post_uniforms : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(nullptr, u_post_inv_view_proj, "u_post_inv_view_proj", bgfx::UniformType::Mat4);
            cache_uniform(nullptr, u_post_params, "u_post_params", bgfx::UniformType::Vec4);
            cache_uniform(nullptr, u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
            cache_uniform(nullptr, s_post_color, "s_post_color", bgfx::UniformType::Sampler);
            cache_uniform(nullptr, s_post_depth, "s_post_depth", bgfx::UniformType::Sampler);
            cache_uniform(nullptr, s_post_gbuffer0, "s_post_gbuffer0", bgfx::UniformType::Sampler);
            cache_uniform(nullptr, s_post_gbuffer1, "s_post_gbuffer1", bgfx::UniformType::Sampler);
            cache_uniform(nullptr, s_post_gbuffer2, "s_post_gbuffer2", bgfx::UniformType::Sampler);
            cache_uniform(nullptr, s_post_velocity, "s_post_velocity", bgfx::UniformType::Sampler);
            cache_uniform(nullptr, s_post_custom_mask, "s_post_custom_mask", bgfx::UniformType::Sampler);
            cache_uniform(nullptr, s_post_custom_depth, "s_post_custom_depth", bgfx::UniformType::Sampler);
            cache_uniform(nullptr, s_pre_exposure, "s_pre_exposure", bgfx::UniformType::Sampler);
        }

        gfx::program::uniform_ptr u_post_inv_view_proj;
        gfx::program::uniform_ptr u_post_params;
        gfx::program::uniform_ptr u_pre_exposure;
        gfx::program::uniform_ptr s_post_color;
        gfx::program::uniform_ptr s_post_depth;
        gfx::program::uniform_ptr s_post_gbuffer0;
        gfx::program::uniform_ptr s_post_gbuffer1;
        gfx::program::uniform_ptr s_post_gbuffer2;
        gfx::program::uniform_ptr s_post_velocity;
        gfx::program::uniform_ptr s_post_custom_mask;
        gfx::program::uniform_ptr s_post_custom_depth;
        gfx::program::uniform_ptr s_pre_exposure;
    } uniforms_;

    graph_material_programs programs_;
};

} // namespace unravel
