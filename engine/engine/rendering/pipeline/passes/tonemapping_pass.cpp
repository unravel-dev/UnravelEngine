#include "tonemapping_pass.h"
#include <engine/assets/asset_manager.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{

auto tonemapping_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto vs_clip_quad_ex = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_atmospherics = am.get_asset<gfx::shader>("engine:/data/shaders/tonemapping/fs_tonemapping.sc");

    tonemapping_program_.program = std::make_unique<gpu_program>(vs_clip_quad_ex, fs_atmospherics);
    tonemapping_program_.cache_uniforms();

    return true;
}

auto tonemapping_pass::create_or_update_output_fb(const gfx::frame_buffer::ptr& input,
                                                  const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr
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
    auto output_tex = std::make_shared<gfx::texture>(input_sz.width,
                                                     input_sz.height,
                                                     false,
                                                     1,
                                                     gfx::texture_format::RGBA8,
                                                     BGFX_TEXTURE_RT);

    // Potentially also create a depth buffer, if needed.
    // If you just need color output, that might be optional.

    output_ = std::make_shared<gfx::frame_buffer>();
    output_->populate({output_tex});

    return output_;
}

auto tonemapping_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    // {
    //     {
    //         // Calculate luminance.
    //         setOffsets2x2Lum(u_offset, 128, 128);
    //         bgfx::setTexture(0, s_texColor, m_fbtextures[0]);
    //         bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
    //         screenSpaceQuad(m_caps->originBottomLeft);
    //         bgfx::submit(hdrLuminance, m_lumProgram);

    //                // Downscale luminance 0.
    //         setOffsets4x4Lum(u_offset, 128, 128);
    //         bgfx::setTexture(0, s_texColor, bgfx::getTexture(m_lum[0]) );
    //         bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
    //         screenSpaceQuad(m_caps->originBottomLeft);
    //         bgfx::submit(hdrLumScale0, m_lumAvgProgram);

    //                // Downscale luminance 1.
    //         setOffsets4x4Lum(u_offset, 64, 64);
    //         bgfx::setTexture(0, s_texColor, bgfx::getTexture(m_lum[1]) );
    //         bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
    //         screenSpaceQuad(m_caps->originBottomLeft);
    //         bgfx::submit(hdrLumScale1, m_lumAvgProgram);

    //                // Downscale luminance 2.
    //         setOffsets4x4Lum(u_offset, 16, 16);
    //         bgfx::setTexture(0, s_texColor, bgfx::getTexture(m_lum[2]) );
    //         bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
    //         screenSpaceQuad(m_caps->originBottomLeft);
    //         bgfx::submit(hdrLumScale2, m_lumAvgProgram);

    //                // Downscale luminance 3.
    //         setOffsets4x4Lum(u_offset, 4, 4);
    //         bgfx::setTexture(0, s_texColor, bgfx::getTexture(m_lum[3]) );
    //         bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
    //         screenSpaceQuad(m_caps->originBottomLeft);
    //         bgfx::submit(hdrLumScale3, m_lumAvgProgram);

    //                // m_bright pass m_threshold is tonemap[3].
    //         setOffsets4x4Lum(u_offset, m_width/2, m_height/2);
    //         bgfx::setTexture(0, s_texColor, m_fbtextures[0]);
    //         bgfx::setTexture(1, s_texLum, bgfx::getTexture(m_lum[4]) );
    //         bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
    //         bgfx::setUniform(u_tonemap, tonemap);
    //         screenSpaceQuad(m_caps->originBottomLeft);
    //         bgfx::submit(hdrBrightness, m_brightProgram);

    //                // m_blur m_bright pass vertically.
    //         bgfx::setTexture(0, s_texColor, bgfx::getTexture(m_bright) );
    //         bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
    //         bgfx::setUniform(u_tonemap, tonemap);
    //         screenSpaceQuad(m_caps->originBottomLeft);
    //         bgfx::submit(hdrVBlur, m_blurProgram);
    //     }
    // }
    const auto& input = params.input;
    auto output = create_or_update_output_fb(params.input, params.output);

    gfx::render_pass pass("output_buffer_fill");
    pass.bind(output.get());

    const auto output_size = output->get_size();

    tonemapping_program_.program->begin();

    float tonemap[4] = {params.config.exposure, static_cast<float>(params.config.method), 0.0f, 0.0f};

    tonemapping_program_.program->set_uniform("u_tonemap", tonemap);
    gfx::set_texture(tonemapping_program_.s_input, 0, input->get_texture());
    irect32_t rect(0, 0, irect32_t::value_type(output_size.width), irect32_t::value_type(output_size.height));
    gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, tonemapping_program_.program->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);
    tonemapping_program_.program->end();

    gfx::discard();

    return output;
}
} // namespace unravel
