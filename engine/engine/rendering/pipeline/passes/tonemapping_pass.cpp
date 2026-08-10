#include "tonemapping_pass.h"
#include <engine/assets/asset_manager.h>
#include <engine/rendering/default_textures.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <math/math.h>

namespace unravel
{

namespace
{

// CIE xy chromaticity to CAT02 LMS cone response (Y = 1).
auto cie_xy_to_lms(float x, float y) -> math::vec3
{
    const float Y = 1.0f;
    const float X = Y * x / y;
    const float Z = Y * (1.0f - x - y) / y;
    return {0.7328f * X + 0.4296f * Y - 0.1624f * Z,
            -0.7036f * X + 1.6975f * Y + 0.0061f * Z,
            0.0030f * X + 0.0136f * Y + 0.9834f * Z};
}

// Von Kries white balance: temperature/tint in [-1, 1] move the assumed white
// point along the Planckian locus (and orthogonally for tint); the returned LMS
// scale re-adapts to D65. Same construction as Unity's ColorBalanceToLMSCoeffs.
auto compute_white_balance_lms(float temperature, float tint) -> math::vec3
{
    const float t1 = temperature * 10.0f / 6.0f;
    const float t2 = tint * 10.0f / 6.0f;
    const float x = 0.31271f - t1 * (t1 < 0.0f ? 0.1f : 0.05f);
    const float standard_y = 2.87f * x - 3.0f * x * x - 0.27509507f;
    const float y = standard_y + t2 * 0.05f;
    const math::vec3 w1 = cie_xy_to_lms(0.31271f, 0.32902f); // D65
    const math::vec3 w2 = cie_xy_to_lms(x, y);
    return {w1.x / w2.x, w1.y / w2.y, w1.z / w2.z};
}

} // namespace

auto tonemapping_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto vs_clip_quad_ex = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_atmospherics = am.get_asset<gfx::shader>("engine:/data/shaders/tonemapping/fs_tonemapping.sc");

    tonemapping_program_.cache_uniforms();
    tonemapping_program_.program = std::make_unique<gpu_program>(vs_clip_quad_ex, fs_atmospherics);

    return true;
}

auto tonemapping_pass::create_or_update_output_fb(gfx::render_view& rview,
                                                  const gfx::frame_buffer::ptr& input,
                                                  const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr
{
    if(output)
    {
        return output;
    }
    auto input_sz = input->get_size();
    // This is presumably your engine’s method to get the bgfx::TextureFormat.
    // If your engine uses something else, adapt accordingly.

    auto& output_tex = rview.tex_get_or_emplace("TONEMAPPING_OUTPUT");
    if(gfx::needs_recreate(output_tex, input_sz))
    {
        output_tex.reset();
        output_tex = std::make_shared<gfx::texture>(input_sz.width,
                                                    input_sz.height,
                                                    false,
                                                    1,
                                                    gfx::texture_format::RGBA8,
                                                    BGFX_TEXTURE_RT);
    }
    auto& output_fbo = rview.fbo_get_or_emplace("TONEMAPPING_OUTPUT");
    if(gfx::needs_recreate(output_fbo, input_sz))
    {
        output_fbo.reset();
        output_fbo = std::make_shared<gfx::frame_buffer>();
        output_fbo->populate({output_tex});
    }
    return output_fbo;
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
    auto output = create_or_update_output_fb(rview, params.input, params.output);

    gfx::render_pass pass("Tonemapping/Pass");
    pass.bind(output.get());

    const auto output_size = output->get_size();

    tonemapping_program_.program->begin();

    float tonemap[4] = {params.config.exposure,
                        static_cast<float>(params.config.method),
                        params.config.dithering ? 1.0f : 0.0f,
                        0.0f};
    gfx::set_uniform(tonemapping_program_.u_tonemapping, tonemap);

    float grading[4] = {params.config.contrast, params.config.saturation, 0.0f, 0.0f};
    gfx::set_uniform(tonemapping_program_.u_grading, grading);

    const auto wb_lms = compute_white_balance_lms(params.config.temperature, params.config.tint);
    float wb[4] = {wb_lms.x, wb_lms.y, wb_lms.z, 0.0f};
    gfx::set_uniform(tonemapping_program_.u_wb_lms, wb);

    gfx::set_texture(tonemapping_program_.s_input, 0, input->get_texture());
    gfx::set_texture(tonemapping_program_.s_exposure, 1, params.exposure_texture ? params.exposure_texture : default_textures::get().white_texture());
    
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

void tonemapping_pass::release_resources(gfx::render_view& rview)
{
    rview.fbo_remove("TONEMAPPING_OUTPUT");
    rview.tex_remove("TONEMAPPING_OUTPUT");
}

} // namespace unravel
