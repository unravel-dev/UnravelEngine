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

auto blit_pass::create_or_update_output_fb(gfx::render_view& rview,
                                           const gfx::frame_buffer::ptr& input,
                                           const gfx::frame_buffer::ptr& output)
    -> gfx::frame_buffer::ptr
{
    if(output)
    {
        return output;
    }
    auto input_sz = input->get_size();
    auto input_tex = input->get_texture();
    auto input_format = input_tex->info.format;
    auto& output_tex = rview.tex_get_or_emplace("BLIT_OUTPUT");
    if(gfx::needs_recreate(output_tex, input_sz, input_format))
    {
        output_tex.reset();
        output_tex = std::make_shared<gfx::texture>(input_sz.width,
                                                    input_sz.height,
                                                    false,
                                                    1,
                                                    input_format,
                                                    BGFX_TEXTURE_RT);
    }
    auto& output_fbo = rview.fbo_get_or_emplace("BLIT_OUTPUT");
    if(gfx::needs_recreate(output_fbo, input_sz))
    {
        output_fbo.reset();
        output_fbo = std::make_shared<gfx::frame_buffer>();
        output_fbo->populate({output_tex});
    }
    return output_fbo;
}

auto blit_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    const auto& input_fb = params.input;
    if(!input_fb)
    {
        return nullptr;
    }
    auto actual_output = create_or_update_output_fb(rview, input_fb, params.output);

    // 3) Begin a named render pass for clarity/debug (optional)
    gfx::render_pass pass("Blit Pass");
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
