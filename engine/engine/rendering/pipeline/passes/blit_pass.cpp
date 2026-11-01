#include "blit_pass.h"
#include "pass_helpers.h"
#include <engine/assets/asset_manager.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{

auto blit_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto vs_fullscreen = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.eshader");
    auto fs_blit = am.get_asset<gfx::shader>("engine:/data/shaders/fs_blit.eshader");

    blit_program_.cache_uniforms();
    blit_program_.program = std::make_unique<gpu_program>(vs_fullscreen, "window", fs_blit);

    return blit_program_.program->is_valid();
}

auto blit_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    const auto& input_fb = params.input;
    if(!input_fb)
    {
        return nullptr;
    }
    auto actual_output =
        get_output_frame_buffer(rview, "BLIT_OUTPUT", input_fb, params.output, input_fb->get_texture()->info.format);

    gfx::render_pass pass("Blit/Pass");
    pass.bind(actual_output.get());
    // Without blending every pixel is drawn below: a clear instead of a load spares a tile-based GPU
    // reading the old contents.
    if(!params.alpha_blend)
    {
        pass.clear(BGFX_CLEAR_COLOR, 0, 1.0f, 0);
    }

    if(!blit_program_.program->begin())
    {
        bgfx::discard();
        return actual_output;
    }
    gfx::set_texture(blit_program_.s_input, 0, input_fb->get_texture());

    auto topology = gfx::clip_quad(1.0f);

    // Render state: write RGB + A, no depth.
    uint64_t state = topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    if(params.alpha_blend)
    {
        state |= BGFX_STATE_BLEND_ALPHA;
    }
    bgfx::setState(state);

    bgfx::submit(pass.id, blit_program_.program->native_handle());

    bgfx::setState(BGFX_STATE_DEFAULT);

    blit_program_.program->end();

    bgfx::discard();

    return actual_output;
}

} // namespace unravel
