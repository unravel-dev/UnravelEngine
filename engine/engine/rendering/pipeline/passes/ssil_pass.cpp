#include "ssil_pass.h"
#include <algorithm>
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{

auto ssil_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");

    auto fs_ssil_trace = am.get_asset<gfx::shader>("engine:/data/shaders/ssil/fs_ssil_trace.sc");
    auto fs_ssil_temporal = am.get_asset<gfx::shader>("engine:/data/shaders/ssil/fs_ssil_temporal_resolve.sc");
    auto cs_ssil_denoise = am.get_asset<gfx::shader>("engine:/data/shaders/ssil/cs_ssil_spatial_denoise.sc");

    trace_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssil_trace);
    trace_program_.cache_uniforms();

    temporal_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssil_temporal);
    temporal_program_.cache_uniforms();

    denoise_program_.program = std::make_unique<gpu_program>(cs_ssil_denoise);
    denoise_program_.cache_uniforms();

    return trace_program_.is_valid() && temporal_program_.is_valid() && denoise_program_.is_valid();
}

auto ssil_pass::create_or_update_ssil_fb(gfx::render_view& rview,
                                         const std::string& name,
                                         const gfx::frame_buffer::ptr& reference,
                                         bool half_res,
                                         uint64_t extra_flags) -> gfx::frame_buffer::ptr
{
    auto ref_sz = reference->get_size();
    uint32_t w = half_res ? std::max(1u, ref_sz.width / 2) : ref_sz.width;
    uint32_t h = half_res ? std::max(1u, ref_sz.height / 2) : ref_sz.height;
    usize32_t target_size{w, h};

    auto& tex = rview.tex_get_or_emplace(name);
    if(!tex || tex->info.width != w || tex->info.height != h)
    {
        tex = std::make_shared<gfx::texture>(w, h, false, 1,
                                             gfx::texture_format::RGBA16F,
                                             BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                 BGFX_SAMPLER_V_CLAMP | extra_flags);
    }

    auto& fbo = rview.fbo_get_or_emplace(name);
    if(!fbo || fbo->get_size() != target_size)
    {
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }

    return fbo;
}

auto ssil_pass::create_or_update_ssil_tex(gfx::render_view& rview,
                                          const std::string& name,
                                          const gfx::frame_buffer::ptr& reference,
                                          bool half_res,
                                          uint64_t extra_flags) -> gfx::texture::ptr
{
    auto ref_sz = reference->get_size();
    uint32_t w = half_res ? std::max(1u, ref_sz.width / 2) : ref_sz.width;
    uint32_t h = half_res ? std::max(1u, ref_sz.height / 2) : ref_sz.height;

    auto& tex = rview.tex_get_or_emplace(name);
    if(!tex || tex->info.width != w || tex->info.height != h)
    {
        tex = std::make_shared<gfx::texture>(w, h, false, 1,
                                             gfx::texture_format::RGBA16F,
                                             BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST |
                                                 BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | extra_flags);
    }

    return tex;
}

auto ssil_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr
{
    if(!params.g_buffer || !trace_program_.is_valid())
    {
        return nullptr;
    }

    gfx::render_pass::push_scope("SSIL");
    auto ssil_curr_fb = run_trace(rview, params);
    if(!ssil_curr_fb)
    {
        gfx::render_pass::pop_scope();
        return nullptr;
    }

    auto result_fb = ssil_curr_fb;

    if(params.settings.enable_temporal_accumulation && temporal_program_.is_valid())
    {
        run_temporal_resolve(rview, result_fb, params.g_buffer, params.prev_depth, params.cam, params.settings);
        result_fb = rview.fbo_get_or_emplace("SSIL_HISTORY_TEMP");
    }
    else
    {
        rview.tex_remove("SSIL_HISTORY");
        rview.fbo_remove("SSIL_HISTORY_TEMP");
        rview.tex_remove("SSIL_HISTORY_TEMP");
    }

    if(params.settings.enable_spatial_denoise && denoise_program_.is_valid())
    {
        result_fb = run_spatial_denoise(rview, result_fb, params.g_buffer, params.settings);
    }
    else
    {
        rview.fbo_remove("SSIL_DENOISED_A");
        rview.tex_remove("SSIL_DENOISED_A");
        rview.fbo_remove("SSIL_DENOISED_B");
        rview.tex_remove("SSIL_DENOISED_B");
    }
    gfx::render_pass::pop_scope();

    return result_fb->get_texture();
}

auto ssil_pass::run_trace(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/SSIL/Trace Pass");

    auto ssil_curr_fb = create_or_update_ssil_fb(rview, "SSIL_CURR", params.g_buffer, params.settings.enable_half_res);

    gfx::render_pass pass("Trace Pass");
    pass.bind(ssil_curr_fb.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());

    trace_program_.program->begin();

    gfx::set_texture(trace_program_.s_color, 0, params.direct_lighting);
    gfx::set_texture(trace_program_.s_normal, 1, params.g_buffer->get_texture(1));
    gfx::set_texture(trace_program_.s_hiz, 2, params.hiz_buffer);
    gfx::set_texture(trace_program_.s_emissive, 3, params.g_buffer->get_texture(2));

    float ssil_params[4] = {
        float(params.settings.max_steps),
        float(params.settings.max_rays),
        params.settings.depth_tolerance,
        params.settings.brightness};
    gfx::set_uniform(trace_program_.u_ssil_params, ssil_params);

    float ssil_params2[4] = {
        params.settings.max_distance,
        float(gfx::get_render_frame() % params.settings.temporal.max_accum_frames),
        0.0f,
        0.0f};
    gfx::set_uniform(trace_program_.u_ssil_params2, ssil_params2);

    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, trace_program_.program->native_handle());

    gfx::set_state(BGFX_STATE_DEFAULT);
    trace_program_.program->end();
    gfx::discard();

    return ssil_curr_fb;
}

auto ssil_pass::run_spatial_denoise(gfx::render_view& rview,
                                    const gfx::frame_buffer::ptr& ssil_curr,
                                    const gfx::frame_buffer::ptr& g_buffer,
                                    const ssil_settings& settings) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/SSIL/Spatial Denoise Pass");

    const int num_passes = std::clamp(settings.spatial_denoise.passes, 1, 5);

    auto fb_a = create_or_update_ssil_fb(rview, "SSIL_DENOISED_A", ssil_curr, false, BGFX_TEXTURE_COMPUTE_WRITE);
    auto fb_b = create_or_update_ssil_fb(rview, "SSIL_DENOISED_B", ssil_curr, false, BGFX_TEXTURE_COMPUTE_WRITE);
    auto sz = fb_a->get_size();
    uint32_t gx = (sz.width + 7) / 8;
    uint32_t gy = (sz.height + 7) / 8;

    auto src_tex = ssil_curr->get_texture();
    gfx::frame_buffer::ptr dst_fb = fb_a;

    for(int i = 0; i < num_passes; ++i)
    {
        gfx::render_pass pass(fmt::format("Spatial Denoise Pass {}", i).c_str());

        denoise_program_.program->begin();

        gfx::set_texture(denoise_program_.s_ssil_input, 0, src_tex);
        gfx::set_image(1, dst_fb->get_texture()->native_handle(), 0, bgfx::Access::Write);
        gfx::set_texture(denoise_program_.s_normal, 2, g_buffer->get_texture(1));
        gfx::set_texture(denoise_program_.s_depth, 3, g_buffer->get_texture(4));

        float denoise_params[4] = {
            float(1 << i),
            settings.spatial_denoise.depth_sigma,
            settings.spatial_denoise.normal_power,
            settings.spatial_denoise.luma_sigma};
        gfx::set_uniform(denoise_program_.u_denoise_params, denoise_params);

        gfx::dispatch(pass.id, denoise_program_.program->native_handle(), gx, gy, 1);

        denoise_program_.program->end();

        src_tex = dst_fb->get_texture();
        dst_fb = (dst_fb == fb_a) ? fb_b : fb_a;
    }

    return (dst_fb == fb_a) ? fb_b : fb_a;
}

auto ssil_pass::run_temporal_resolve(gfx::render_view& rview,
                                     const gfx::frame_buffer::ptr& ssil_input,
                                     const gfx::frame_buffer::ptr& g_buffer,
                                     const gfx::texture::ptr& prev_depth,
                                     const camera* cam,
                                     const ssil_settings& settings) -> gfx::texture::ptr
{
    APP_SCOPE_PERF("Rendering/SSIL/Temporal Resolve Pass");

    auto old_history = rview.tex_safe_get("SSIL_HISTORY");
    auto history_tex = create_or_update_ssil_tex(rview, "SSIL_HISTORY", ssil_input, false);
    auto temp_fb = create_or_update_ssil_fb(rview, "SSIL_HISTORY_TEMP", ssil_input, false);

    // History was just allocated -- RGBA16F contains undefined data (possibly NaN).
    // Seed it with the current frame and skip temporal this frame.
    // Also skip if previous-frame depth is not yet available (first frame).
    if(history_tex != old_history || !prev_depth)
    {
        gfx::render_pass blit_pass("History Init Blit Pass");
        gfx::blit(blit_pass.id, history_tex->native_handle(), 0, 0, ssil_input->get_texture()->native_handle(), 0, 0);
        return temp_fb->get_texture();
    }

    gfx::render_pass pass("Temporal Resolve Pass");
    pass.bind(temp_fb.get());
    pass.set_view_proj(cam->get_view(), cam->get_projection());

    temporal_program_.program->begin();

    gfx::set_texture(temporal_program_.s_ssil_curr, 0, ssil_input->get_texture());
    gfx::set_texture(temporal_program_.s_ssil_history, 1, history_tex);
    gfx::set_texture(temporal_program_.s_depth, 2, g_buffer->get_texture(4));
    gfx::set_texture(temporal_program_.s_prev_depth, 3, prev_depth);

    float temporal_params[4] = {
        settings.enable_temporal_accumulation ? 1.0f : 0.0f,
        settings.temporal.history_strength,
        settings.temporal.depth_threshold,
        float(settings.temporal.max_accum_frames)};
    gfx::set_uniform(temporal_program_.u_temporal_params, temporal_params);

    auto prev_vp = cam->get_prev_view_projection();
    gfx::set_uniform(temporal_program_.u_prev_view_proj, prev_vp.get_matrix());

    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, temporal_program_.program->native_handle());

    gfx::set_state(BGFX_STATE_DEFAULT);
    temporal_program_.program->end();
    gfx::discard();

    // Blit temporal result into persistent history texture
    gfx::render_pass blit_pass("History Blit Pass");
    gfx::blit(blit_pass.id, history_tex->native_handle(), 0, 0, temp_fb->get_texture()->native_handle(), 0, 0);

    return temp_fb->get_texture();
}

void ssil_pass::release_resources(gfx::render_view& rview)
{
    rview.fbo_remove("SSIL_CURR");
    rview.tex_remove("SSIL_CURR");
    rview.fbo_remove("SSIL_DENOISED_A");
    rview.tex_remove("SSIL_DENOISED_A");
    rview.fbo_remove("SSIL_DENOISED_B");
    rview.tex_remove("SSIL_DENOISED_B");
    rview.tex_remove("SSIL_HISTORY");
    rview.fbo_remove("SSIL_HISTORY_TEMP");
    rview.tex_remove("SSIL_HISTORY_TEMP");
}

} // namespace unravel
