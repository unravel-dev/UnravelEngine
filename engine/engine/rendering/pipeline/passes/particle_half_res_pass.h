#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/frame_buffer.h>
#include <graphics/render_view.h>

#include <functional>

namespace unravel
{

/**
 * @brief Half-resolution particles: their depth (the farthest of each 2x2 block of the scene's), a
 * half-size target holding premultiplied light and transmittance, and a composite over the light buffer
 * with a nearest-depth upsample. A quarter of the fill cost for large, soft effects.
 */
class particle_half_res_pass
{
public:
    struct run_params
    {
        const camera* cam = nullptr;
        /// The light buffer and the scene depth (LBUFFER_DEPTH): the composite target and the occluder.
        gfx::frame_buffer::ptr lbuffer_depth;
        /// The full-resolution reactive coverage the composite raises (MAX blend); null when the view's TAA
        /// does not read one (the half-size targets then hold no coverage either).
        gfx::texture::ptr reactive;
        /// The device supports BGFX_CAPS_BLEND_INDEPENDENT (the reactive targets are coverages).
        bool has_independent_blend = false;
        /// Draws the particles into the view it is given: bound to the half-size targets (color with the
        /// transmittance, reactive coverage, depth), with the camera's view and projection. Returns true when
        /// it drew anything.
        std::function<bool(bgfx::ViewId)> draw_particles;
    };

    auto init(rtti::context& ctx) -> bool;

    /// @return True when particles were composited.
    auto run(gfx::render_view& rview, const run_params& params) -> bool;

private:
    /// The half-size light (+ reactive coverage when @p has_reactive) and depth targets.
    auto acquire_targets(gfx::render_view& rview, const usize32_t& full_size, bool has_reactive)
        -> gfx::frame_buffer::ptr;
    auto acquire_composite_target(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;
    void downsample_depth(const gfx::frame_buffer::ptr& targets, const gfx::texture::ptr& scene_depth);
    void composite(const run_params& params,
                   const gfx::frame_buffer::ptr& targets,
                   const gfx::frame_buffer::ptr& composite_target);

    struct depth_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_depth, "s_depth", bgfx::UniformType::Sampler);
        }

        gfx::program::uniform_ptr s_depth;
        std::unique_ptr<gpu_program> program;
    } depth_program_;

    struct composite_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_half_color, "s_half_color", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_half_reactive, "s_half_reactive", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_half_depth, "s_half_depth", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", bgfx::UniformType::Sampler);
        }

        gfx::program::uniform_ptr s_half_color;
        gfx::program::uniform_ptr s_half_reactive;
        gfx::program::uniform_ptr s_half_depth;
        gfx::program::uniform_ptr s_depth;
        std::unique_ptr<gpu_program> program;
    } composite_program_;
};

} // namespace unravel
