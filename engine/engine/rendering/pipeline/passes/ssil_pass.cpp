#include "ssil_pass.h"
#include <algorithm>
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
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
    auto cs_ssil_downsample = am.get_asset<gfx::shader>("engine:/data/shaders/ssil/cs_ssil_downsample.sc");
    auto fs_ssil_upsample = am.get_asset<gfx::shader>("engine:/data/shaders/ssil/fs_ssil_upsample.sc");

    trace_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssil_trace);
    trace_program_.cache_uniforms();

    temporal_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssil_temporal);
    temporal_program_.cache_uniforms();

    denoise_program_.program = std::make_unique<gpu_program>(cs_ssil_denoise);
    denoise_program_.cache_uniforms();

    downsample_program_.program = std::make_unique<gpu_program>(cs_ssil_downsample);
    downsample_program_.cache_uniforms();

    upsample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssil_upsample);
    upsample_program_.cache_uniforms();

    // The upsample and downsample programs are optional: SSIL still works at full res (and
    // falls back to a hardware-bilinear consume at reduced res / all-full-res denoise) if
    // they fail to build.
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
        rview.fbo_remove("SSIL_DENOISED_HALF_A");
        rview.tex_remove("SSIL_DENOISED_HALF_A");
        rview.fbo_remove("SSIL_DENOISED_HALF_B");
        rview.tex_remove("SSIL_DENOISED_HALF_B");
        rview.tex_remove("SSIL_VARIANCE_HALF_A");
        rview.tex_remove("SSIL_VARIANCE_HALF_B");
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

    // Environment SH for the per-ray miss fallback. Null on the first frame (the SH is
    // computed later in the indirect pass and persists for the next frame), so bind black
    // and signal the shader to disable the fallback (env_intensity = 0) until it exists.
    const bool env_fallback_active = static_cast<bool>(params.irradiance_sh);
    gfx::set_texture(trace_program_.s_irradiance, 6,
                     env_fallback_active ? params.irradiance_sh : default_textures::get().black_texture());

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
        env_fallback_active ? 1.0f : 0.0f};
    gfx::set_uniform(trace_program_.u_ssil_params2, ssil_params2);

    float ssil_params3[4] = {params.settings.thickness, 0.0f, 0.0f, 0.0f};
    gfx::set_uniform(trace_program_.u_ssil_params3, ssil_params3);

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
    const int max_step = std::max(settings.spatial_denoise.max_step, 1);
    const bool has_moments = static_cast<bool>(moments);
    // Fall back to the colour buffer for the moments sampler when temporal moments are
    // unavailable; u_denoise_params2.x = 0 makes the shader ignore it (spatial only).
    auto moments_tex = has_moments ? moments : ssil_curr->get_texture();

    const float depth_sigma = settings.spatial_denoise.depth_sigma;
    const float normal_power = settings.spatial_denoise.normal_power;
    const float luma_sigma = settings.spatial_denoise.luma_sigma;

    // Mixed resolution: keep `full_passes` narrow passes at the trace resolution (preserving
    // local detail + feeding a clean edge-aware signal into the downsample), then run the
    // remaining wide passes at HALF that resolution where their large dilation is cache-
    // coherent and ~4x cheaper, and finally bilateral-upsample back to trace res (which
    // restores sharp silhouettes). Falls back to all-full-res if the helper programs are
    // unavailable or there are no passes to push down.
    int full_passes = std::clamp(settings.spatial_denoise.full_res_passes, 0, num_passes);
    if(has_moments)
    {
        // The temporal resolve writes moments at the trace resolution. Run at least one
        // trace-resolution pass so the denoiser can consume that stable variance before
        // the mixed half-res tier falls back to propagated spatial variance.
        full_passes = std::max(full_passes, 1);
    }
    const bool mixed = downsample_program_.is_valid() && upsample_program_.is_valid() &&
                       (num_passes - full_passes) > 0;
    if(!mixed)
    {
        full_passes = num_passes;
    }

    // ssil_curr already carries the trace resolution; the full-res tier buffers match it 1:1.
    auto fb_a = create_or_update_ssil_fb(rview, "SSIL_DENOISED_A", ssil_curr, trace_resolution::full, BGFX_TEXTURE_COMPUTE_WRITE);
    auto fb_b = create_or_update_ssil_fb(rview, "SSIL_DENOISED_B", ssil_curr, trace_resolution::full, BGFX_TEXTURE_COMPUTE_WRITE);
    auto sz = fb_a->get_size();
    uint32_t gx = (sz.width + 7) / 8;
    uint32_t gy = (sz.height + 7) / 8;

    // Variance ping-pong (SVGF). Single-channel R16F, compute-written and sampled. The
    // first pass integrates variance in-shader; each subsequent pass refilters the prior
    // pass's variance with the kernel weights squared, so the luminance sigma converges.
    auto make_variance_tex = [&](const std::string& name, const usize32_t& size) -> gfx::texture::ptr
    {
        auto& tex = rview.tex_get_or_emplace(name);
        if(gfx::needs_recreate(tex, size))
        {
            tex.reset();
            tex = std::make_shared<gfx::texture>(size.width, size.height, false, 1, gfx::texture_format::R16F,
                                                 BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP |
                                                     BGFX_SAMPLER_V_CLAMP);
        }
        return tex;
    };
    auto var_a = make_variance_tex("SSIL_VARIANCE_A", sz);
    auto var_b = make_variance_tex("SSIL_VARIANCE_B", sz);

    // Single a-trous dispatch. The shader is resolution-agnostic (derives every position from
    // its output image size vs the full-res G-buffer), so the same call drives both tiers.
    auto run_atrous = [&](const gfx::texture::ptr& in_tex,
                          const gfx::frame_buffer::ptr& out_fb,
                          const gfx::texture::ptr& v_src,
                          const gfx::texture::ptr& v_dst,
                          uint32_t dgx,
                          uint32_t dgy,
                          int step,
                          bool first_pass,
                          bool use_moments,
                          int kernel_radius,
                          const std::string& label) -> void
    {
        gfx::render_pass pass(label.c_str());
        // Bind the camera transforms so the plane-distance edge-stop can reconstruct view-
        // space positions (computeViewSpacePosition -> u_invProj) and rotate the centre
        // normal into view space (u_view).
        pass.set_view_proj(cam->get_view(), cam->get_projection());

        denoise_program_.program->begin();

        gfx::set_texture(denoise_program_.s_ssil_input, 0, in_tex);
        gfx::set_image(1, out_fb->get_texture()->native_handle(), 0, bgfx::Access::Write);
        gfx::set_texture(denoise_program_.s_normal, 2, g_buffer->get_texture(1));
        gfx::set_texture(denoise_program_.s_depth, 3, g_buffer->get_texture(4));
        gfx::set_texture(denoise_program_.s_ssil_moments, 4, moments_tex);
        gfx::set_texture(denoise_program_.s_ssil_variance, 5, v_src);
        gfx::set_image(6, v_dst->native_handle(), 0, bgfx::Access::Write);

        float denoise_params[4] = {float(step), depth_sigma, normal_power, luma_sigma};
        gfx::set_uniform(denoise_program_.u_denoise_params, denoise_params);

        // .w = kernel radius (2 => 5x5 full-res tier, 1 => 3x3 wide half-res tier).
        float denoise_params2[4] = {use_moments ? 1.0f : 0.0f, first_pass ? 1.0f : 0.0f, 0.0f,
                                    float(kernel_radius)};
        gfx::set_uniform(denoise_program_.u_denoise_params2, denoise_params2);

        gfx::dispatch(pass.id, denoise_program_.program->native_handle(), dgx, dgy, 1);

        denoise_program_.program->end();
    };

    // --- Full-resolution tier ---
    // Seed the variance read slot to the buffer NOT written on pass 0 so a texture is never
    // bound as both sampler and write-image in one dispatch (pass 0 integrates variance in-
    // shader and does not sample it anyway).
    auto src_tex = ssil_curr->get_texture();
    gfx::frame_buffer::ptr dst_fb = fb_a;
    gfx::texture::ptr var_src = var_b;
    gfx::texture::ptr var_dst = var_a;

    for(int i = 0; i < full_passes; ++i)
    {
        // Full-res tier preserves local detail -> full 5x5 (radius 2) kernel.
        run_atrous(src_tex, dst_fb, var_src, var_dst, gx, gy, std::min(1 << i, max_step), i == 0, has_moments, 2,
                   fmt::format("Spatial Denoise/Full Pass {}", i));

        src_tex = dst_fb->get_texture();
        dst_fb = (dst_fb == fb_a) ? fb_b : fb_a;
        var_src = var_dst;
        var_dst = (var_dst == var_a) ? var_b : var_a;
    }

    if(!mixed)
    {
        // src_tex holds the final colour; its framebuffer is the one NOT pointed at by dst_fb.
        return (dst_fb == fb_a) ? fb_b : fb_a;
    }

    // After the full tier, `src_tex` holds the latest result and `dst_fb` is the free full-res
    // framebuffer (used below as the upsample target). If full_passes == 0, `src_tex` is the
    // raw trace buffer and both full-res buffers are free.

    // --- Half-resolution wide tier ---
    auto half_a = create_or_update_ssil_fb(rview, "SSIL_DENOISED_HALF_A", ssil_curr, trace_resolution::half, BGFX_TEXTURE_COMPUTE_WRITE);
    auto half_b = create_or_update_ssil_fb(rview, "SSIL_DENOISED_HALF_B", ssil_curr, trace_resolution::half, BGFX_TEXTURE_COMPUTE_WRITE);
    auto half_sz = half_a->get_size();
    uint32_t hgx = (half_sz.width + 7) / 8;
    uint32_t hgy = (half_sz.height + 7) / 8;
    auto var_ha = make_variance_tex("SSIL_VARIANCE_HALF_A", half_sz);
    auto var_hb = make_variance_tex("SSIL_VARIANCE_HALF_B", half_sz);
    const bool has_downsampled_variance = full_passes > 0;

    // Geometry-aware downsample of the full-res tier result into the half-res input.
    // When a full-res tier ran, carry its variance with the colour so the wide half-res
    // passes keep the temporal/spatial variance guidance instead of recomputing it.
    {
        gfx::render_pass ds_pass("Spatial Denoise/Downsample Pass");
        ds_pass.set_view_proj(cam->get_view(), cam->get_projection());

        downsample_program_.program->begin();
        gfx::set_texture(downsample_program_.s_ssil_input, 0, src_tex);
        gfx::set_image(1, half_a->get_texture()->native_handle(), 0, bgfx::Access::Write);
        gfx::set_texture(downsample_program_.s_normal, 2, g_buffer->get_texture(1));
        gfx::set_texture(downsample_program_.s_depth, 3, g_buffer->get_texture(4));
        gfx::set_texture(downsample_program_.s_ssil_variance, 4, var_src);
        gfx::set_image(5, var_ha->native_handle(), 0, bgfx::Access::Write);

        float ds_params[4] = {depth_sigma, normal_power, has_downsampled_variance ? 1.0f : 0.0f, 0.0f};
        gfx::set_uniform(downsample_program_.u_downsample_params, ds_params);

        gfx::dispatch(ds_pass.id, downsample_program_.program->native_handle(), hgx, hgy, 1);
        downsample_program_.program->end();
    }

    const int half_passes = num_passes - full_passes;
    auto half_src = half_a->get_texture();
    gfx::frame_buffer::ptr half_dst = half_b;
    gfx::texture::ptr hvar_src = has_downsampled_variance ? var_ha : var_hb;
    gfx::texture::ptr hvar_dst = has_downsampled_variance ? var_hb : var_ha;

    for(int j = 0; j < half_passes; ++j)
    {
        // If the downsample carried full-res variance, propagate it; otherwise the first
        // half-res pass computes a fresh spatial estimate.
        const bool first_half_pass = !has_downsampled_variance && j == 0;
        // The wide tier uses the narrow 3x3 (radius 1) kernel -- indirect diffuse is low-
        // frequency, so the outer ring adds little -- with the dilation step doubled to keep
        // its reach.
        run_atrous(half_src, half_dst, hvar_src, hvar_dst, hgx, hgy, std::min(1 << (j + 1), max_step), first_half_pass, false, 1,
                   fmt::format("Spatial Denoise/Half Pass {}", j));

        half_src = half_dst->get_texture();
        half_dst = (half_dst == half_a) ? half_b : half_a;
        hvar_src = hvar_dst;
        hvar_dst = (hvar_dst == var_ha) ? var_hb : var_ha;
    }
    auto half_result_fb = (half_dst == half_a) ? half_b : half_a;

    // --- Bilateral upsample half-res wide result back to trace res ---
    // Render into the free full-res framebuffer (`dst_fb`); silhouettes are reconstructed
    // sharply because the upsample rejects cross-edge taps using the full-res G-buffer.
    auto out_fb = dst_fb;
    {
        gfx::render_pass up_pass("Spatial Denoise/Upsample To Trace Resolution Pass");
        up_pass.bind(out_fb.get());
        up_pass.set_view_proj(cam->get_view(), cam->get_projection());

        upsample_program_.program->begin();
        gfx::set_texture(upsample_program_.s_ssil_input, 0, half_result_fb->get_texture());
        gfx::set_texture(upsample_program_.s_normal, 1, g_buffer->get_texture(1));
        gfx::set_texture(upsample_program_.s_depth, 2, g_buffer->get_texture(4));

        float upsample_params[4] = {depth_sigma, normal_power, 0.0f, 0.0f};
        gfx::set_uniform(upsample_program_.u_upsample_params, upsample_params);

        auto topology = gfx::clip_quad(1.0f);
        gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::submit(up_pass.id, upsample_program_.program->native_handle());

        gfx::set_state(BGFX_STATE_DEFAULT);
        upsample_program_.program->end();
        gfx::discard();
    }

    return out_fb;
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
    const auto gbuf_sz = g_buffer->get_size();
    const auto temporal_sz = ssil_input->get_size();
    const float temporal_resolution[4] = {
        static_cast<float>(gbuf_sz.width),
        static_cast<float>(gbuf_sz.height),
        static_cast<float>(gbuf_sz.width) / static_cast<float>(temporal_sz.width),
        static_cast<float>(gbuf_sz.height) / static_cast<float>(temporal_sz.height)};

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
        gfx::set_uniform(temporal_program_.u_temporal_resolution, temporal_resolution);

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
    gfx::set_uniform(temporal_program_.u_temporal_resolution, temporal_resolution);

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

    gfx::render_pass pass("Output/Upsample To Full Resolution Pass");
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
    rview.fbo_remove("SSIL_DENOISED_HALF_A");
    rview.tex_remove("SSIL_DENOISED_HALF_A");
    rview.fbo_remove("SSIL_DENOISED_HALF_B");
    rview.tex_remove("SSIL_DENOISED_HALF_B");
    rview.tex_remove("SSIL_VARIANCE_HALF_A");
    rview.tex_remove("SSIL_VARIANCE_HALF_B");
    rview.tex_remove("SSIL_HISTORY");
    rview.fbo_remove("SSIL_HISTORY_TEMP");
    rview.tex_remove("SSIL_HISTORY_TEMP");
    rview.tex_remove("SSIL_MOMENTS_HISTORY");
    rview.tex_remove("SSIL_MOMENTS_TEMP");
    rview.fbo_remove("SSIL_UPSAMPLED");
    rview.tex_remove("SSIL_UPSAMPLED");
}

} // namespace unravel
