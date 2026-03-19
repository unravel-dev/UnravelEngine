#include "auto_exposure_pass.h"
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <math/math.h>

namespace unravel
{

auto auto_exposure_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto cs_histogram = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_luminance_histogram.sc");
    auto cs_average = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_histogram_average.sc");
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_pre_exposure = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/fs_pre_exposure.sc");

    if(!cs_histogram || !cs_average || !vs_clip_quad || !fs_pre_exposure)
    {
        return false;
    }

    histogram_program_.program = std::make_shared<gpu_program>(cs_histogram);
    histogram_program_.cache_uniforms();

    average_program_.program = std::make_shared<gpu_program>(cs_average);
    average_program_.cache_uniforms();

    pre_exposure_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_pre_exposure);
    pre_exposure_program_.cache_uniforms();

    histogram_buffer_ = bgfx::createDynamicIndexBuffer(histogram_bins,
                                                       BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);

    return histogram_program_.program->is_valid() &&
           average_program_.program->is_valid() &&
           pre_exposure_program_.program->is_valid() &&
           bgfx::isValid(histogram_buffer_);
}

void auto_exposure_pass::ensure_resources(gfx::render_view& rview)
{
    auto& exposure_tex = rview.tex_get_or_emplace("AUTO_EXPOSURE");
    if(gfx::needs_recreate(exposure_tex, {1, 1}))
    {
        exposure_tex.reset();
        exposure_tex = std::make_shared<gfx::texture>(1,
                                                      1,
                                                      false,
                                                      1,
                                                      gfx::texture_format::R32F,
                                                      BGFX_TEXTURE_COMPUTE_WRITE);
        rview.data_get_or_emplace("AUTO_EXPOSURE_SNAP", 1u) = 1u;
    }
}

auto auto_exposure_pass::get_exposure_texture(gfx::render_view& rview) const -> gfx::texture::ptr
{
    return rview.tex_safe_get("AUTO_EXPOSURE");
}

void auto_exposure_pass::run_histogram(gfx::render_view& rview, const gfx::frame_buffer::ptr& input)
{
    const auto input_size = input->get_size();

    gfx::render_pass pass("Auto Exposure Histogram");

    histogram_program_.program->begin();

    gfx::set_texture(histogram_program_.s_hdr_input, 0, input->get_texture());

    gfx::set_buffer(1, histogram_buffer_, bgfx::Access::ReadWrite);

    float log_range = max_log_lum - min_log_lum;
    float inv_log_range = 1.0f / log_range;
    float params[4] = {min_log_lum, inv_log_range, float(input_size.width), float(input_size.height)};
    gfx::set_uniform(histogram_program_.u_histogram_params, params);

    uint32_t groups_x = (input_size.width + 15) / 16;
    uint32_t groups_y = (input_size.height + 15) / 16;
    bgfx::dispatch(pass.id, histogram_program_.program->native_handle(), groups_x, groups_y, 1);

    histogram_program_.program->end();
}

void auto_exposure_pass::run_average(gfx::render_view& rview, const settings& config, float dt)
{
    auto exposure_tex = rview.tex_get("AUTO_EXPOSURE");
    if(!exposure_tex)
    {
        return;
    }

    gfx::render_pass pass("Auto Exposure Average");

    average_program_.program->begin();

    gfx::set_buffer(0, histogram_buffer_, bgfx::Access::ReadWrite);

    gfx::set_image(1, exposure_tex->native_handle(), 0, bgfx::Access::ReadWrite, gfx::texture_format::R32F);

    float log_range = max_log_lum - min_log_lum;
    float params0[4] = {min_log_lum, log_range, config.low_percentile, config.high_percentile};
    gfx::set_uniform(average_program_.u_average_params0, params0);

    float params1[4] = {config.min_ev, config.max_ev, config.compensation, 0.0f};
    gfx::set_uniform(average_program_.u_average_params1, params1);

    float effective_dt = dt;
    auto& snap = rview.data_get_or_emplace("AUTO_EXPOSURE_SNAP", 0u);
    if(snap != 0u)
    {
        effective_dt = 100.0f;
        snap = 0u;
    }

    float params2[4] = {effective_dt, config.adaptation_speed_up, config.adaptation_speed_down, 0.0f};
    gfx::set_uniform(average_program_.u_average_params2, params2);

    bgfx::dispatch(pass.id, average_program_.program->native_handle(), 1, 1, 1);

    average_program_.program->end();
}

auto auto_exposure_pass::create_or_update_output_fb(gfx::render_view& rview,
                                                    const gfx::frame_buffer::ptr& input,
                                                    const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr
{
    if(output)
    {
        return output;
    }
    auto input_sz = input->get_size();
    auto& output_tex = rview.tex_get_or_emplace("AUTO_EXPOSURE_OUTPUT");
    if(gfx::needs_recreate(output_tex, input_sz))
    {
        output_tex.reset();
        output_tex = std::make_shared<gfx::texture>(input_sz.width,
                                                    input_sz.height,
                                                    false,
                                                    1,
                                                    gfx::texture_format::RGBA16F,
                                                    BGFX_TEXTURE_RT);
    }
    auto& output_fbo = rview.fbo_get_or_emplace("AUTO_EXPOSURE_OUTPUT");
    if(gfx::needs_recreate(output_fbo, input_sz))
    {
        output_fbo.reset();
        output_fbo = std::make_shared<gfx::frame_buffer>();
        gfx::fbo_attachment att;
        att.texture = output_tex;
        att.generate_mips = false;
        output_fbo->populate({att});
    }
    return output_fbo;
}

auto auto_exposure_pass::run_pre_exposure(gfx::render_view& rview,
                                          const gfx::frame_buffer::ptr& input,
                                          const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr
{
    auto exposure_tex = rview.tex_get("AUTO_EXPOSURE");
    if(!exposure_tex)
    {
        return input;
    }

    auto result = create_or_update_output_fb(rview, input, output);

    gfx::render_pass pass("Pre-Exposure Pass");
    pass.bind(result.get());
    pass.set_view_proj({}, {});

    pre_exposure_program_.program->begin();

    gfx::set_texture(pre_exposure_program_.s_scene, 0, input->get_texture());
    gfx::set_texture(pre_exposure_program_.s_exposure, 1, exposure_tex);

    const auto output_size = result->get_size();
    irect32_t rect(0, 0, irect32_t::value_type(output_size.width), irect32_t::value_type(output_size.height));
    gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, pre_exposure_program_.program->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);
    pre_exposure_program_.program->end();

    return result;
}

auto auto_exposure_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/Auto Exposure Pass");

    ensure_resources(rview);

    run_histogram(rview, params.input);
    run_average(rview, params.config, params.delta_time);

    // Exposure is applied in tonemapping (after bloom) so that bloom
    // contribution is also scaled by the adapted exposure.
    return params.input;
}

void auto_exposure_pass::release_resources(gfx::render_view& rview)
{
    rview.tex_remove("AUTO_EXPOSURE");
    rview.tex_remove("AUTO_EXPOSURE_OUTPUT");
    rview.fbo_remove("AUTO_EXPOSURE_OUTPUT");
    rview.data_get_or_emplace("AUTO_EXPOSURE_SNAP", 1u) = 1u;
}

} // namespace unravel
