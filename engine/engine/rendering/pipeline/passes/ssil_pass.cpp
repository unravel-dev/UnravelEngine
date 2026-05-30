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
    auto fs_ssil_upsample = am.get_asset<gfx::shader>("engine:/data/shaders/ssil/fs_ssil_upsample.sc");

    trace_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssil_trace);
    trace_program_.cache_uniforms();

    temporal_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssil_temporal);
    temporal_program_.cache_uniforms();

    denoise_program_.program = std::make_unique<gpu_program>(cs_ssil_denoise);
    denoise_program_.cache_uniforms();

    upsample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssil_upsample);
    upsample_program_.cache_uniforms();

    // The upsample program is optional: SSIL still works at full res (and falls
    // back to a hardware-bilinear consume at reduced res) if it fails to build.
    return trace_program_.is_valid() && temporal_program_.is_valid() && denoise_program_.is_valid();
}

auto ssil_pass::create_or_update_ssil_fb(gfx::render_view& rview,
                                         const std::string& name,
                                         const gfx::frame_buffer::ptr& reference,
                                         trace_resolution res,
                                         uint64_t extra_flags) -> gfx::frame_buffer::ptr
{
    const auto target_size = compute_trace_size(reference->get_size(), res);

    auto& tex = rview.tex_get_or_emplace(name);
    if(gfx::needs_recreate(tex, target_size))
    {
        tex.reset();
        tex = std::make_shared<gfx::texture>(target_size.width, target_size.height, false, 1,
                                             gfx::texture_format::RGBA16F,
                                             BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                 BGFX_SAMPLER_V_CLAMP | extra_flags);
    }

    auto& fbo = rview.fbo_get_or_emplace(name);
    if(gfx::needs_recreate(fbo, target_size))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }

    return fbo;
}

auto ssil_pass::create_or_update_ssil_tex(gfx::render_view& rview,
                                          const std::string& name,
                                          const gfx::frame_buffer::ptr& reference,
                                          trace_resolution res,
                                          uint64_t extra_flags) -> gfx::texture::ptr
{
    const auto target_size = compute_trace_size(reference->get_size(), res);

    auto& tex = rview.tex_get_or_emplace(name);
    if(gfx::needs_recreate(tex, target_size))
    {
        tex.reset();
        tex = std::make_shared<gfx::texture>(target_size.width, target_size.height, false, 1,
                                             gfx::texture_format::RGBA16F,
                                             BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST |
                                                 BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | extra_flags);
    }

    return tex;
}

auto ssil_pass::create_or_update_ssil_fb_mrt(gfx::render_view& rview,
                                             const std::string& fbo_name,
                                             const std::string& color_tex_name,
                                             const std::string& moments_tex_name,
                                             const gfx::frame_buffer::ptr& reference,
                                             trace_resolution res,
                                             uint64_t extra_flags) -> gfx::frame_buffer::ptr
{
    const auto target_size = compute_trace_size(reference->get_size(), res);

    auto color_tex = create_or_update_ssil_tex(rview, color_tex_name, reference, res, extra_flags);
    auto moments_tex = create_or_update_ssil_tex(rview, moments_tex_name, reference, res, extra_flags);

    auto& fbo = rview.fbo_get_or_emplace(fbo_name);
    if(gfx::needs_recreate(fbo, target_size))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({color_tex, moments_tex});
    }

    return fbo;
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

    bool moments_valid = false;
    if(params.settings.enable_temporal_accumulation && temporal_program_.is_valid())
    {
        moments_valid =
            run_temporal_resolve(rview, result_fb, params.g_buffer, params.prev_depth, params.cam, params.settings);
        result_fb = rview.fbo_get_or_emplace("SSIL_HISTORY_TEMP");
    }
    else
    {
        rview.tex_remove("SSIL_HISTORY");
        rview.fbo_remove("SSIL_HISTORY_TEMP");
        rview.tex_remove("SSIL_HISTORY_TEMP");
        rview.tex_remove("SSIL_MOMENTS_HISTORY");
        rview.tex_remove("SSIL_MOMENTS_TEMP");
    }

    if(params.settings.enable_spatial_denoise && denoise_program_.is_valid())
    {
        // The temporal pass produces per-pixel luminance moments (E[L], E[L^2]); the
        // denoiser uses them for true spatiotemporal variance. Only valid when the
        // resolve shader actually ran this frame.
        auto moments_tex = moments_valid ? rview.tex_safe_get("SSIL_MOMENTS_TEMP") : gfx::texture::ptr{};
        result_fb = run_spatial_denoise(rview, result_fb, params.g_buffer, moments_tex, params.cam, params.settings);
    }
    else
    {
        rview.fbo_remove("SSIL_DENOISED_A");
        rview.tex_remove("SSIL_DENOISED_A");
        rview.fbo_remove("SSIL_DENOISED_B");
        rview.tex_remove("SSIL_DENOISED_B");
        rview.tex_remove("SSIL_VARIANCE_A");
        rview.tex_remove("SSIL_VARIANCE_B");
    }

    // Joint-bilateral upsample to full res when the trace ran below full res so the
    // indirect-lighting consumer samples a 1:1 full-res buffer instead of bleeding a
    // reduced-res buffer across depth/normal edges with hardware bilinear.
    const bool reduced_res = params.settings.resolution != trace_resolution::full;
    if(reduced_res && upsample_program_.is_valid())
    {
        result_fb = run_upsample(rview, result_fb, params.g_buffer, params.cam, params.settings);
    }
    else
    {
        rview.fbo_remove("SSIL_UPSAMPLED");
        rview.tex_remove("SSIL_UPSAMPLED");
    }
    gfx::render_pass::pop_scope();

    return result_fb->get_texture();
}

auto ssil_pass::run_trace(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/SSIL/Trace Pass");

    auto ssil_curr_fb = create_or_update_ssil_fb(rview, "SSIL_CURR", params.g_buffer, params.settings.resolution);

    gfx::render_pass pass("Trace Pass");
    pass.bind(ssil_curr_fb.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());

    trace_program_.program->begin();

    gfx::set_texture(trace_program_.s_color, 0, params.direct_lighting);
    gfx::set_texture(trace_program_.s_normal, 1, params.g_buffer->get_texture(1));
    gfx::set_texture(trace_program_.s_hiz, 2, params.hiz_buffer);
    gfx::set_texture(trace_program_.s_emissive, 3, params.g_buffer->get_texture(2));
    gfx::set_texture(trace_program_.s_albedo, 4, params.g_buffer->get_texture(0));

    bool multi_bounce_active = params.settings.enable_multi_bounce && params.prev_ssil && trace_program_.s_prev_ssil;
    if(multi_bounce_active)
    {
        gfx::set_texture(trace_program_.s_prev_ssil, 5, params.prev_ssil);
    }

    float ssil_params[4] = {
        float(params.settings.max_steps),
        float(params.settings.max_rays),
        params.settings.depth_tolerance,
        params.settings.brightness};
    gfx::set_uniform(trace_program_.u_ssil_params, ssil_params);

    float multi_bounce_val = multi_bounce_active ? params.settings.multi_bounce_intensity : 0.0f;
    // The seed is fed straight into Rand3DPCG16 per pixel. Using the raw
    // render frame (rather than render_frame % max_accum_frames) gives every
    // frame a unique ray pattern, so the temporal accumulator averages over
    // many distinct Monte Carlo samples instead of revisiting the same N
    // patterns forever. Wrap to 16 bits to keep the float-precision domain
    // ample.
    float ssil_params2[4] = {
        params.settings.max_distance,
        float(gfx::get_render_frame() & 0xFFFFu),
        multi_bounce_val,
        0.0f};
    gfx::set_uniform(trace_program_.u_ssil_params2, ssil_params2);

    // u_ssil_resolution: xy = full G-buffer size, zw = PER-AXIS (full / trace) scale.
    // Per-axis is required: at odd full-res W with even full-res H (e.g. 1233 x 900)
    // the X and Y ratios disagree (2.00162 vs 2.0) and applying a scalar across both
    // axes shifts the bottom-row gbuffer fetch ~0.7 pixels off, producing an out-of-
    // frustum view-space ray origin and a visible noise band at the viewport bottom.
    // SSIL hid this under its smooth hemisphere kernel; SSR exposed it sharply.
    const auto gbuf_sz = params.g_buffer->get_size();
    const auto trace_sz = ssil_curr_fb->get_size();
    const float ssil_resolution[4] = {static_cast<float>(gbuf_sz.width),
                                      static_cast<float>(gbuf_sz.height),
                                      static_cast<float>(gbuf_sz.width) / static_cast<float>(trace_sz.width),
                                      static_cast<float>(gbuf_sz.height) / static_cast<float>(trace_sz.height)};
    gfx::set_uniform(trace_program_.u_ssil_resolution, ssil_resolution);

    uint64_t topology = gfx::clip_fullscreen_triangle(1.0f);
    if(topology == 0)
    {
        topology = gfx::clip_quad(1.0f);
    }
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
                                    const gfx::texture::ptr& moments,
                                    const camera* cam,
                                    const ssil_settings& settings) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/SSIL/Spatial Denoise Pass");

    const int num_passes = std::clamp(settings.spatial_denoise.passes, 1, 5);
    const bool has_moments = static_cast<bool>(moments);
    // Fall back to the colour buffer for the moments sampler when temporal moments are
    // unavailable; u_denoise_params2.x = 0 makes the shader ignore it (spatial only).
    auto moments_tex = has_moments ? moments : ssil_curr->get_texture();

    // ssil_curr already carries the trace resolution; denoise buffers match it 1:1.
    auto fb_a = create_or_update_ssil_fb(rview, "SSIL_DENOISED_A", ssil_curr, trace_resolution::full, BGFX_TEXTURE_COMPUTE_WRITE);
    auto fb_b = create_or_update_ssil_fb(rview, "SSIL_DENOISED_B", ssil_curr, trace_resolution::full, BGFX_TEXTURE_COMPUTE_WRITE);
    auto sz = fb_a->get_size();
    uint32_t gx = (sz.width + 7) / 8;
    uint32_t gy = (sz.height + 7) / 8;

    // Variance ping-pong (SVGF). Single-channel R16F, compute-written and sampled. The
    // first pass integrates variance in-shader; each subsequent pass refilters the prior
    // pass's variance with the kernel weights squared, so the luminance sigma converges.
    auto make_variance_tex = [&](const std::string& name) -> gfx::texture::ptr
    {
        auto& tex = rview.tex_get_or_emplace(name);
        if(gfx::needs_recreate(tex, sz))
        {
            tex.reset();
            tex = std::make_shared<gfx::texture>(sz.width, sz.height, false, 1, gfx::texture_format::R16F,
                                                 BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP |
                                                     BGFX_SAMPLER_V_CLAMP);
        }
        return tex;
    };
    auto var_a = make_variance_tex("SSIL_VARIANCE_A");
    auto var_b = make_variance_tex("SSIL_VARIANCE_B");

    auto src_tex = ssil_curr->get_texture();
    gfx::frame_buffer::ptr dst_fb = fb_a;
    // Kept in lockstep with the colour ping-pong so a pass reads the variance paired with
    // the colour it reads and writes the variance paired with the colour it writes. Seed
    // the read slot to the buffer NOT written on pass 0 (var_a) so the same texture is
    // never bound as both sampler and write-image in one dispatch; pass 0 integrates
    // variance in-shader and does not sample it anyway.
    gfx::texture::ptr var_src = var_b;
    gfx::texture::ptr var_dst = var_a;

    for(int i = 0; i < num_passes; ++i)
    {
        gfx::render_pass pass(fmt::format("Spatial Denoise Pass {}", i).c_str());
        // Bind the camera transforms so the plane-distance edge-stop can reconstruct
        // view-space positions (computeViewSpacePosition -> u_invProj) and rotate the
        // centre normal into view space (u_view).
        pass.set_view_proj(cam->get_view(), cam->get_projection());

        denoise_program_.program->begin();

        gfx::set_texture(denoise_program_.s_ssil_input, 0, src_tex);
        gfx::set_image(1, dst_fb->get_texture()->native_handle(), 0, bgfx::Access::Write);
        gfx::set_texture(denoise_program_.s_normal, 2, g_buffer->get_texture(1));
        gfx::set_texture(denoise_program_.s_depth, 3, g_buffer->get_texture(4));
        gfx::set_texture(denoise_program_.s_ssil_moments, 4, moments_tex);
        gfx::set_texture(denoise_program_.s_ssil_variance, 5, var_src);
        gfx::set_image(6, var_dst->native_handle(), 0, bgfx::Access::Write);

        float denoise_params[4] = {
            float(1 << i),
            settings.spatial_denoise.depth_sigma,
            settings.spatial_denoise.normal_power,
            settings.spatial_denoise.luma_sigma};
        gfx::set_uniform(denoise_program_.u_denoise_params, denoise_params);

        // .z = per-frame rotation seed so the a-trous kernel orientation differs each
        // frame; temporal accumulation then averages over many orientations, converging
        // the spatial filter far faster than a static per-pixel rotation alone.
        float denoise_params2[4] = {has_moments ? 1.0f : 0.0f,
                                    (i == 0) ? 1.0f : 0.0f,
                                    float(gfx::get_render_frame() & 0xFFFFu),
                                    0.0f};
        gfx::set_uniform(denoise_program_.u_denoise_params2, denoise_params2);

        gfx::dispatch(pass.id, denoise_program_.program->native_handle(), gx, gy, 1);

        denoise_program_.program->end();

        src_tex = dst_fb->get_texture();
        dst_fb = (dst_fb == fb_a) ? fb_b : fb_a;
        var_src = var_dst;
        var_dst = (var_dst == var_a) ? var_b : var_a;
    }

    return (dst_fb == fb_a) ? fb_b : fb_a;
}

auto ssil_pass::run_temporal_resolve(gfx::render_view& rview,
                                     const gfx::frame_buffer::ptr& ssil_input,
                                     const gfx::frame_buffer::ptr& g_buffer,
                                     const gfx::texture::ptr& prev_depth,
                                     const camera* cam,
                                     const ssil_settings& settings) -> bool
{
    APP_SCOPE_PERF("Rendering/SSIL/Temporal Resolve Pass");

    // ssil_input already carries the trace resolution; history buffers match it 1:1.
    auto old_history = rview.tex_safe_get("SSIL_HISTORY");
    auto history_tex = create_or_update_ssil_tex(rview, "SSIL_HISTORY", ssil_input, trace_resolution::full, BGFX_TEXTURE_BLIT_DST);
    auto moments_history = create_or_update_ssil_tex(rview, "SSIL_MOMENTS_HISTORY", ssil_input, trace_resolution::full, BGFX_TEXTURE_BLIT_DST);
    // Two-attachment target: attachment 0 = colour + normalized weight (consumed
    // downstream as before), attachment 1 = luminance moments (E[L], E[L^2]).
    auto temp_fb = create_or_update_ssil_fb_mrt(rview, "SSIL_HISTORY_TEMP", "SSIL_HISTORY_TEMP", "SSIL_MOMENTS_TEMP",
                                                ssil_input, trace_resolution::full, BGFX_TEXTURE_BLIT_DST);
    auto moments_temp = rview.tex_safe_get("SSIL_MOMENTS_TEMP");

    // History was just allocated -- RGBA16F contains undefined data (possibly NaN).
    // Seed both outputs by running the temporal shader in "disabled" mode. That writes
    // colour to attachment 0 and real luminance moments (L, L^2) to attachment 1; a raw
    // blit from colour into the moments texture would make the next frame's variance
    // start from invalid (R, G) data.
    if(history_tex != old_history || !prev_depth)
    {
        gfx::render_pass init_pass("Temporal Init Pass");
        init_pass.bind(temp_fb.get());
        init_pass.set_view_proj(cam->get_view(), cam->get_projection());

        temporal_program_.program->begin();

        gfx::set_texture(temporal_program_.s_ssil_curr, 0, ssil_input->get_texture());
        gfx::set_texture(temporal_program_.s_ssil_history, 1, history_tex);
        gfx::set_texture(temporal_program_.s_depth, 2, g_buffer->get_texture(4));
        gfx::set_texture(temporal_program_.s_prev_depth, 3, g_buffer->get_texture(4));
        gfx::set_texture(temporal_program_.s_ssil_moments_history, 4, moments_history);

        float temporal_params[4] = {0.0f, settings.temporal.history_strength, settings.temporal.depth_threshold,
                                    float(settings.temporal.max_accum_frames)};
        gfx::set_uniform(temporal_program_.u_temporal_params, temporal_params);

        auto prev_vp = cam->get_prev_view_projection();
        gfx::set_uniform(temporal_program_.u_prev_view_proj, prev_vp.get_matrix());

        auto topology = gfx::clip_quad(1.0f);
        gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::submit(init_pass.id, temporal_program_.program->native_handle());

        gfx::set_state(BGFX_STATE_DEFAULT);
        temporal_program_.program->end();
        gfx::discard();

        gfx::render_pass hist_blit_pass("History Init Blit Pass");
        gfx::blit(hist_blit_pass.id, history_tex->native_handle(), 0, 0, temp_fb->get_texture()->native_handle(), 0, 0);

        gfx::render_pass moments_hist_blit_pass("Moments History Init Blit Pass");
        gfx::blit(moments_hist_blit_pass.id, moments_history->native_handle(), 0, 0, moments_temp->native_handle(), 0, 0);
        return false;
    }

    gfx::render_pass pass("Temporal Resolve Pass");
    pass.bind(temp_fb.get());
    pass.set_view_proj(cam->get_view(), cam->get_projection());

    temporal_program_.program->begin();

    gfx::set_texture(temporal_program_.s_ssil_curr, 0, ssil_input->get_texture());
    gfx::set_texture(temporal_program_.s_ssil_history, 1, history_tex);
    gfx::set_texture(temporal_program_.s_depth, 2, g_buffer->get_texture(4));
    gfx::set_texture(temporal_program_.s_prev_depth, 3, prev_depth);
    gfx::set_texture(temporal_program_.s_ssil_moments_history, 4, moments_history);

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

    // Blit temporal result into the persistent history textures (colour + moments).
    gfx::render_pass blit_pass("History Blit Pass");
    gfx::blit(blit_pass.id, history_tex->native_handle(), 0, 0, temp_fb->get_texture()->native_handle(), 0, 0);

    gfx::render_pass moments_blit_pass("Moments History Blit Pass");
    gfx::blit(moments_blit_pass.id, moments_history->native_handle(), 0, 0, moments_temp->native_handle(), 0, 0);

    return true;
}

auto ssil_pass::run_upsample(gfx::render_view& rview,
                             const gfx::frame_buffer::ptr& ssil_input,
                             const gfx::frame_buffer::ptr& g_buffer,
                             const camera* cam,
                             const ssil_settings& settings) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/SSIL/Upsample Pass");

    // Output matches the full G-buffer resolution (reference = g_buffer, res = full).
    auto out_fb = create_or_update_ssil_fb(rview, "SSIL_UPSAMPLED", g_buffer, trace_resolution::full);

    gfx::render_pass pass("Upsample Pass");
    pass.bind(out_fb.get());
    // Required so the shader's computeViewSpacePosition (u_invProj/u_view) is valid
    // for the linear-depth edge-stopping weight.
    pass.set_view_proj(cam->get_view(), cam->get_projection());

    upsample_program_.program->begin();

    gfx::set_texture(upsample_program_.s_ssil_input, 0, ssil_input->get_texture());
    gfx::set_texture(upsample_program_.s_normal, 1, g_buffer->get_texture(1));
    gfx::set_texture(upsample_program_.s_depth, 2, g_buffer->get_texture(4));

    // Reuse the spatial-denoise edge-stopping sigmas for upsample tap rejection.
    float upsample_params[4] = {
        settings.spatial_denoise.depth_sigma,
        settings.spatial_denoise.normal_power,
        0.0f,
        0.0f};
    gfx::set_uniform(upsample_program_.u_upsample_params, upsample_params);

    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, upsample_program_.program->native_handle());

    gfx::set_state(BGFX_STATE_DEFAULT);
    upsample_program_.program->end();
    gfx::discard();

    return out_fb;
}

void ssil_pass::release_resources(gfx::render_view& rview)
{
    rview.fbo_remove("SSIL_CURR");
    rview.tex_remove("SSIL_CURR");
    rview.fbo_remove("SSIL_DENOISED_A");
    rview.tex_remove("SSIL_DENOISED_A");
    rview.fbo_remove("SSIL_DENOISED_B");
    rview.tex_remove("SSIL_DENOISED_B");
    rview.tex_remove("SSIL_VARIANCE_A");
    rview.tex_remove("SSIL_VARIANCE_B");
    rview.tex_remove("SSIL_HISTORY");
    rview.fbo_remove("SSIL_HISTORY_TEMP");
    rview.tex_remove("SSIL_HISTORY_TEMP");
    rview.tex_remove("SSIL_MOMENTS_HISTORY");
    rview.tex_remove("SSIL_MOMENTS_TEMP");
    rview.fbo_remove("SSIL_UPSAMPLED");
    rview.tex_remove("SSIL_UPSAMPLED");
}

} // namespace unravel
