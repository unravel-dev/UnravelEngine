// blit_pass.cpp
#include "blit_pass.h"
#include <engine/assets/asset_manager.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{

auto blit_pass::init(rtti::context& ctx) -> bool
{
    // 1) Retrieve the asset manager (holds shader assets)
    auto& am = ctx.get_cached<asset_manager>();

    // 2) Load a fullscreen‐quad vertex shader and a simple blit fragment shader
    //    vs_clip_quad.sc is assumed to output a full‐screen triangle/quad.
    //    fs_blit.sc should sample a 2D texture “s_input” and output it unchanged.
    auto vs_fullscreen = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_blit = am.get_asset<gfx::shader>("engine:/data/shaders/fs_blit.sc");

    // 3) Create our GPU program
    blit_program_.program = std::make_unique<gpu_program>(vs_fullscreen, fs_blit);
    blit_program_.cache_uniforms();

    return true;
}

auto blit_pass::create_or_update_output_fb(const gfx::frame_buffer::ptr& input, const gfx::frame_buffer::ptr& output)
    -> gfx::frame_buffer::ptr
{
    // If the caller provided an output framebuffer, just return it.
    if(output)
    {
        return output;
    }

    // Otherwise, we need to create or recreate our own internal FB to match `input`.
    auto input_sz = input->get_size();
    auto input_tex = input->get_texture();      // returns a gfx::texture::ptr
    auto input_format = input_tex->info.format; // gfx::texture_format

    bool needs_recreate = false;
    if(!output_)
    {
        needs_recreate = true;
    }
    else
    {
        auto out_sz = output_->get_size();
        auto out_format = output_->get_texture()->info.format;
        if(out_sz != input_sz || out_format != input_format)
        {
            needs_recreate = true;
        }
    }

    if(!needs_recreate)
    {
        return output_;
    }

    // Destroy old output_ if it existed:
    if(output_)
    {
        output_.reset();
    }

    // Create a new texture with same size/format as input
    // Note: we pass BGFX_TEXTURE_RT to mark it as a render target.
    auto output_tex = std::make_shared<gfx::texture>(input_sz.width,
                                                     input_sz.height,
                                                     false,          // no generate mips
                                                     1,              // one layer
                                                     input_format,   // same format as input
                                                     BGFX_TEXTURE_RT // render‐target flag
    );

    // Create a new framebuffer wrapping that texture.
    output_ = std::make_shared<gfx::frame_buffer>();
    output_->populate({output_tex});

    return output_;
}

auto blit_pass::run(const run_params& params) -> gfx::frame_buffer::ptr
{
    // 1) Ensure we have a valid input FB
    const auto& input_fb = params.input;
    if(!input_fb)
    {
        return nullptr;
    }

    // 2) Either use the provided output FB, or create/match one internally
    auto actual_output = create_or_update_output_fb(input_fb, params.output);

    // 3) Begin a named render pass for clarity/debug (optional)
    gfx::render_pass pass("blit_pass");
    pass.bind(actual_output.get());

    // 4) Bind our GPU program and set the source texture
    blit_program_.program->begin();
    gfx::set_texture(blit_program_.s_input, 0, input_fb->get_texture());

    // 5) Draw a fullscreen quad. The helper `gfx::clip_quad()` returns
    //    the appropriate “topology” for a single‐triangle fullscreen quad,
    //    taking care of originBottomLeft if needed.
    //    (We pass 1.0f as the depth—meaning no depth test is used.)
    auto topology = gfx::clip_quad(1.0f);

    // 6) Configure render state: write RGB + A, no depth, no blending.
    gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

    // 7) Submit to the current view (render_pass::bind will have set the view ID)
    gfx::submit(pass.id, blit_program_.program->native_handle());

    // 8) Reset to default state (optional but good practice)
    gfx::set_state(BGFX_STATE_DEFAULT);

    blit_program_.program->end();

    // 9) Unbind/discard any transient state (optional).
    gfx::discard();

    return actual_output;
}

} // namespace unravel
