// blit_pass.h
#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <engine/rendering/camera.h>
#include <graphics/texture.h>
#include <graphics/render_pass.h>

namespace unravel
{

class blit_pass
{
public:
    struct run_params
    {
        gfx::frame_buffer::ptr input;   ///< Source framebuffer (must have a color texture).
        gfx::frame_buffer::ptr output;  ///< Optional destination framebuffer. If null, will be created to match input.
    };

           /// Must be called once (after bgfx::init() and after `asset_manager` is registered in context).
    auto init(rtti::context& ctx) -> bool;

           /// Executes the blit: copies `params.input` → `params.output`. Returns the actual output framebuffer.
    auto run(const run_params& params) -> gfx::frame_buffer::ptr;

private:
    /// If `output` is non-null and matches size/format of `input`, returns `output`.
    /// Otherwise creates (or recreates) a matching framebuffer in `output_` and returns that.
    auto create_or_update_output_fb(const gfx::frame_buffer::ptr& input,
                                    const gfx::frame_buffer::ptr& output)
        -> gfx::frame_buffer::ptr;

    struct blit_program : uniforms_cache
    {
        /// Cache the “s_input” sampler uniform from the GPU program
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_input, "s_input", gfx::uniform_type::Sampler);
        }

        gfx::program::uniform_ptr s_input;            ///< sampler2D uniform name = "s_input"
        std::unique_ptr<gpu_program> program;         ///< The compiled VS/FS program
    } blit_program_;

    gfx::frame_buffer::ptr output_;  ///< Internally cached output FB if none supplied
};

} // namespace unravel
