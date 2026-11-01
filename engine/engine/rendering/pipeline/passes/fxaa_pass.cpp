#include "fxaa_pass.h"
#include "pass_helpers.h"

#include <bx/math.h>
#include <engine/assets/asset_manager.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{

auto fxaa_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto vs_clip_quad_ex = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.eshader");
    auto fs_fxaa = am.get_asset<gfx::shader>("engine:/data/shaders/fxaa/fs_fxaa.eshader");

    // Cache uniforms before the program is created (GL >= 400 contract).
    fxaa_program_.cache_uniforms();

    // Create the GPU program
    fxaa_program_.program = std::make_unique<gpu_program>(vs_clip_quad_ex, fs_fxaa);

    return fxaa_program_.program->is_valid();
}

auto fxaa_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    const auto& input = params.input;
    auto output = get_output_frame_buffer(rview, "FXAA_OUTPUT", input, params.output, input->get_texture(0)->info.format);

    const auto output_size = output->get_size();

    gfx::render_pass pass("FXAA/Pass");
    pass.bind(output.get());

    // A full-screen quad in clip space: view and projection are identity.
    pass.set_view_proj({}, {});

    if(fxaa_program_.program->begin())
    {
        const auto& color_tex = input->get_texture(0);

        gfx::set_texture(fxaa_program_.s_input, 0, color_tex);

        const auto output_noise = make_output_noise(params.grain_intensity, params.dithering);
        gfx::set_uniform(fxaa_program_.u_output_noise, output_noise.data());

        irect32_t rect(0, 0, output_size.width, output_size.height);
        bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());

        auto topology = gfx::clip_quad(1.0f);

        bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

        bgfx::submit(pass.id, fxaa_program_.program->native_handle());

        bgfx::setState(BGFX_STATE_DEFAULT);

        fxaa_program_.program->end();
    }

    bgfx::discard();

    return output;
}

void fxaa_pass::release_resources(gfx::render_view& rview)
{
    rview.fbo_remove("FXAA_OUTPUT");
    rview.tex_remove("FXAA_OUTPUT");
}

} // namespace unravel
