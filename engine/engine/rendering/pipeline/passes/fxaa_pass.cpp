#include "fxaa_pass.h"

// These includes match your engine’s style. Adapt as needed:
#include <bx/math.h>
#include <engine/assets/asset_manager.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{

auto fxaa_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    // Load your FXAA shaders (compiled from vs_fxaa.sc / fs_fxaa.sc).
    // Adjust these paths to where your shaders actually reside.
    auto vs_clip_quad_ex = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_fxaa = am.get_asset<gfx::shader>("engine:/data/shaders/fxaa/fs_fxaa.sc");

    // Create the GPU program
    fxaa_program_.program = std::make_unique<gpu_program>(vs_clip_quad_ex, fs_fxaa);

    // Cache the uniforms (only "s_input" for FXAA).
    fxaa_program_.cache_uniforms();

    // Return true if all is valid.
    return fxaa_program_.program->is_valid();
}

auto fxaa_pass::create_or_update_output_fb(const gfx::frame_buffer::ptr& input, const gfx::frame_buffer::ptr& output)
    -> gfx::frame_buffer::ptr
{
    if(output)
    {
        return output;
    }
    // 1) Get the size & format from the input
    auto input_sz = input->get_size();
    auto input_format = input->get_texture(0)->info.format;
    // This is presumably your engine’s method to get the bgfx::TextureFormat.
    // If your engine uses something else, adapt accordingly.

    // 2) Compare with our stored (lastWidth_, lastHeight_, lastFormat_)
    bool needs_recreate = false;

    if(!output_ || input_sz != output_->get_size() || input_format != output_->get_texture()->info.format)
    {
        needs_recreate = true;
    }

    // 3) If no changes, do nothing
    if(!needs_recreate)
        return output_;

    // 4) Otherwise, destroy old outputFb_ if it exists
    if(output_)
    {
        output_.reset(); // or however your engine releases a frame_buffer
    }

    // 5) Create a new texture with the same size/format
    //    Then create a new framebuffer that wraps it.
    //    Pseudocode: your engine might differ in how it creates textures/FBs.
    auto output_tex =
        std::make_shared<gfx::texture>(input_sz.width, input_sz.height, false, 1, input_format, BGFX_TEXTURE_RT);

    // Potentially also create a depth buffer, if needed.
    // If you just need color output, that might be optional.

    output_ = std::make_shared<gfx::frame_buffer>();
    output_->populate({output_tex});

    return output_;
}

auto fxaa_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    const auto& input = params.input;
    auto output = create_or_update_output_fb(params.input, params.output);

    const auto output_size = output->get_size();

    gfx::render_pass pass("fxaa_pass");
    pass.bind(output.get());

    // For a typical post-processing pass, we do a full-screen quad in clip space.
    // So set view & projection to identity (no camera transform).
    pass.set_view_proj({}, {});

    // Begin the FXAA program
    if(fxaa_program_.program->begin())
    {
        // The texture we want to sample in FS is the color texture from our input buffer.
        // Often you have something like input->get_color_texture(0).
        // If your engine uses a different method, adapt accordingly.
        const auto& color_tex = input->get_texture(0);

        // Bind "s_input" sampler to slot 0
        gfx::set_texture(fxaa_program_.s_input, 0, color_tex);

        // Set scissor to the entire output area
        irect32_t rect(0, 0, output_size.width, output_size.height);
        gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());

        // Draw a full-screen quad
        // Typically, your engine might provide `clip_quad()` or something similar.
        // We'll do the same approach as your atmospheric_pass example.
        auto topology = gfx::clip_quad(1.0f);

        // State: write RGBA. Depth test is optional;
        // for a post pass, we usually don't need depth testing at all.
        gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

        // Submit the draw call using the pass ID
        gfx::submit(pass.id, fxaa_program_.program->native_handle());

        // Reset state (optional cleanup)
        gfx::set_state(BGFX_STATE_DEFAULT);

        // End usage of our GPU program
        fxaa_program_.program->end();
    }

    // Discard is typically called at the end of the pass if your engine requires it.
    gfx::discard();

    return output;
}

} // namespace unravel
