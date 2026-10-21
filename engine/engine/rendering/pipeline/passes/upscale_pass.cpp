#include "upscale_pass.h"

#include <engine/assets/asset_manager.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{
namespace
{
/// The film grain uniform takes a quarter of the tonemapping settings' amount, as the tone curve's does.
constexpr float UPSCALE_GRAIN_SCALE = 0.25f;
/// The grain's per-frame seed cycles through this many frames.
constexpr std::uint32_t UPSCALE_GRAIN_SEED_PERIOD = 1024;
} // namespace


auto upscale_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_upscale = am.get_asset<gfx::shader>("engine:/data/shaders/fs_upscale.sc");
    auto fs_upscale_depth = am.get_asset<gfx::shader>("engine:/data/shaders/fs_upscale_depth.sc");
    // Uniforms are cached before the program is created (GL >= 400 contract).
    upscale_program_.cache_uniforms();
    upscale_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_upscale);
    depth_program_.cache_uniforms();
    depth_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_upscale_depth);
    return upscale_program_.program->is_valid() && depth_program_.program->is_valid();
}

void upscale_pass::run(const run_params& params)
{
    if(!params.input || !params.output || !upscale_program_.program->begin())
    {
        return;
    }
    const bool has_depth = params.output_depth && params.input_depth;
    gfx::render_pass pass("Upscale/Pass");
    pass.bind(has_depth ? params.output_depth.get() : params.output.get());
    // Every pixel is written: a clear instead of a load spares a tile-based GPU reading the old contents.
    pass.clear(has_depth ? BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH : BGFX_CLEAR_COLOR, 0, 1.0f, 0);
    const auto input_size = params.input->get_size();
    const float input_size_uniform[4] = {float(input_size.width),
                                         float(input_size.height),
                                         1.0f / float(input_size.width),
                                         1.0f / float(input_size.height)};
    const float output_noise[4] = {params.grain_intensity * UPSCALE_GRAIN_SCALE,
                                   float(gfx::get_render_frame() % UPSCALE_GRAIN_SEED_PERIOD),
                                   params.dithering ? 1.0f : 0.0f,
                                   0.0f};
    gfx::set_texture(upscale_program_.s_input,
                     0,
                     params.input->get_texture(),
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIP_POINT);
    gfx::set_uniform(upscale_program_.u_upscale_input_size, input_size_uniform);
    gfx::set_uniform(upscale_program_.u_output_noise, output_noise);
    bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, upscale_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    upscale_program_.program->end();
    if(has_depth)
    {
        resample_depth(pass.id, params.input_depth);
    }
}

void upscale_pass::resample_depth(bgfx::ViewId view, const gfx::texture::ptr& input_depth)
{
    if(!depth_program_.program->begin())
    {
        return;
    }
    gfx::set_texture(depth_program_.s_depth, 0, input_depth, BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
    bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS);
    bgfx::submit(view, depth_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    depth_program_.program->end();
}

} // namespace unravel
