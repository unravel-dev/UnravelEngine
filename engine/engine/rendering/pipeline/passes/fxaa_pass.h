#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>

namespace unravel
{

class fxaa_pass
{
public:
    // Optional extra parameters could be added here if you want
    // to tweak FXAA settings or pass other data in the future.
    struct run_params
    {
        gfx::frame_buffer::ptr input;
        gfx::frame_buffer::ptr output;
    };

    // Initialize the pass (load shaders, create GPU program).
    auto init(rtti::context& ctx) -> bool;

    // Run the pass on the given input frame buffer.
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

private:
    auto create_or_update_output_fb(const gfx::frame_buffer::ptr& input, const gfx::frame_buffer::ptr& output)
        -> gfx::frame_buffer::ptr;
    // Encapsulate the GPU program + any uniforms we might need.
    struct fxaa_program : uniforms_cache
    {
        // Cache the uniforms.
        // For FXAA we really only need to set up the sampler uniform (s_input).
        void cache_uniforms()
        {
            // 'u_viewTexel' is built-in. We do NOT need to manually cache it.
            // We just need the sampler2D "s_input".
            cache_uniform(program.get(), s_input, "s_input", gfx::uniform_type::Sampler);
        }

        gfx::program::uniform_ptr s_input;
        std::unique_ptr<gpu_program> program;
    } fxaa_program_;

    gfx::frame_buffer::ptr output_;
};

} // namespace unravel
