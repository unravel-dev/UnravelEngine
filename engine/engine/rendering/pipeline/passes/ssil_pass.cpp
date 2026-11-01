#include "ssil_pass.h"
#include "pass_helpers.h"
#include <algorithm>
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{
namespace
{
/// Compute group edge of the spatial denoise and of the downsample.
constexpr uint32_t SSIL_DENOISE_GROUP_SIZE = 8;

/// Which ping-pong history a render view reads next.
struct history_state
{
    static constexpr const char* view_key = "SSIL_HISTORY_STATE";

    bool reads_a = true;
};
} // namespace

auto ssil_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.eshader");

    auto fs_ssil_trace = am.get_asset<gfx::shader>("engine:/data/shaders/ssil/fs_ssil_trace.eshader");
    auto fs_ssil_temporal = am.get_asset<gfx::shader>("engine:/data/shaders/ssil/fs_ssil_temporal_resolve.eshader");
    auto cs_ssil_denoise = am.get_asset<gfx::shader>("engine:/data/shaders/ssil/cs_ssil_spatial_denoise.eshader");
    auto cs_ssil_downsample = am.get_asset<gfx::shader>("engine:/data/shaders/ssil/cs_ssil_downsample.eshader");
    auto fs_ssil_upsample = am.get_asset<gfx::shader>("engine:/data/shaders/ssil/fs_ssil_upsample.eshader");

    trace_program_.cache_uniforms();
    trace_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssil_trace);

    temporal_program_.cache_uniforms();
    temporal_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssil_temporal);

    denoise_program_.cache_uniforms();
    denoise_program_.program = std::make_unique<gpu_program>(cs_ssil_denoise);

    downsample_program_.cache_uniforms();
    downsample_program_.program = std::make_unique<gpu_program>(cs_ssil_downsample);

    upsample_program_.cache_uniforms();
    upsample_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssil_upsample);

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
    const auto& tex = rview.tex_acquire(name,
                                        {.size = compute_trace_size(reference->get_size(), res),
                                         .format = bgfx::TextureFormat::RGBA16F,
                                         .flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                                  extra_flags});

    return acquire_frame_buffer(rview, name, {tex});
}

auto ssil_pass::create_or_update_ssil_tex(gfx::render_view& rview,
                                          const std::string& name,
                                          const gfx::frame_buffer::ptr& reference,
                                          trace_resolution res,
                                          uint64_t extra_flags) -> gfx::texture::ptr
{
    return rview.tex_acquire(name,
                             {.size = compute_trace_size(reference->get_size(), res),
                              .format = bgfx::TextureFormat::RGBA16F,
                              .flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP |
                                       BGFX_SAMPLER_V_CLAMP | extra_flags});
}

auto ssil_pass::create_or_update_ssil_fb_mrt(gfx::render_view& rview,
                                             const std::string& fbo_name,
                                             const std::string& color_tex_name,
                                             const std::string& moments_tex_name,
                                             const gfx::frame_buffer::ptr& reference,
                                             trace_resolution res,
                                             uint64_t extra_flags) -> gfx::frame_buffer::ptr
{
    auto color_tex = create_or_update_ssil_tex(rview, color_tex_name, reference, res, extra_flags);
    auto moments_tex = create_or_update_ssil_tex(rview, moments_tex_name, reference, res, extra_flags);

    return acquire_frame_buffer(rview, fbo_name, {color_tex, moments_tex});
}

auto ssil_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr
{
    if(!params.g_buffer || !begin_program(trace_program_.program))
    {
        return nullptr;
    }

    pre_exposure_ = params.pre_exposure;

    gfx::render_pass::push_scope("SSIL");
    auto ssil_curr_fb = run_trace(rview, params);
    if(!ssil_curr_fb)
    {
        gfx::render_pass::pop_scope();
        return nullptr;
    }

    auto result_fb = ssil_curr_fb;

    bool moments_valid = false;
    gfx::texture::ptr moments_tex_for_denoise;
    if(params.settings.enable_temporal_accumulation && begin_program(temporal_program_.program))
    {
        gfx::frame_buffer::ptr temporal_result_fb;
        moments_valid = run_temporal_resolve(rview,
                                             result_fb,
                                             params.g_buffer,
                                             params.prev_depth,
                                             params.velocity,
                                             params.cam,
                                             params.settings,
                                             temporal_result_fb,
                                             moments_tex_for_denoise);
        result_fb = temporal_result_fb;
    }
    else
    {
        release_history_resources(rview);
    }

    if(params.settings.enable_spatial_denoise && begin_program(denoise_program_.program))
    {
        // The temporal pass produces per-pixel luminance moments (E[L], E[L^2]); the
        // denoiser uses them for true spatiotemporal variance. Only valid when the
        // resolve shader actually ran this frame.
        auto moments_tex = moments_valid ? moments_tex_for_denoise : gfx::texture::ptr{};
        result_fb = run_spatial_denoise(rview, result_fb, params.g_buffer, moments_tex, params.cam, params.settings);
    }
    else
    {
        release_denoise_resources(rview);
    }

    // Joint-bilateral upsample to full res when the trace ran below full res so the
    // indirect-lighting consumer samples a 1:1 full-res buffer instead of bleeding a
    // reduced-res buffer across depth/normal edges with hardware bilinear. When the
    // mixed-resolution denoiser also ran, run_spatial_denoise returned its half-of-trace
    // result directly (skipping the intermediate trace-res upsample) and this single
    // pass reconstructs all the way to full -- one less bilateral resample, sharper.
    const bool reduced_res = params.settings.resolution != trace_resolution::full;
    if(reduced_res && begin_program(upsample_program_.program))
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

    gfx::set_texture(trace_program_.s_color, 0, params.direct_lighting);
    gfx::set_texture(trace_program_.s_normal, 1, params.g_buffer->get_texture(1));
    gfx::set_texture(trace_program_.s_hiz, 2, params.hiz_buffer);
    gfx::set_texture(trace_program_.s_emissive, 3, params.g_buffer->get_texture(2));
    gfx::set_texture(trace_program_.s_albedo, 4, params.g_buffer->get_texture(0));

    // The slot stays bound with multi bounce off; u_multi_bounce = 0 keeps the shader from reading it.
    bool multi_bounce_active = params.settings.enable_multi_bounce && params.prev_ssil && trace_program_.s_prev_ssil;
    gfx::set_texture(trace_program_.s_prev_ssil,
                     5,
                     multi_bounce_active ? params.prev_ssil : default_textures::get().black_texture());

    // Environment SH for the per-ray miss fallback (the irradiance pass runs before SSIL). Null when the view has none,
    // so bind black and signal the shader to disable the fallback (env_intensity = 0).
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
    // The seed is fed to InterleavedGradientNoise as the temporal axis (via
    // Hammersley16_IGN) -- it drives per-pixel scramble decorrelation across
    // frames so the temporal accumulator averages over many distinct Monte
    // Carlo samples instead of revisiting the same pattern forever. Using the
    // raw render frame (rather than render_frame % max_accum_frames) keeps
    // every frame's scramble unique within the accumulator's window. Wrap to
    // 16 bits to keep the float-precision domain ample.
    float ssil_params2[4] = {
        params.settings.max_distance,
        float(gfx::get_render_frame() & 0xFFFFu),
        multi_bounce_val,
        env_fallback_active ? 1.0f : 0.0f};
    gfx::set_uniform(trace_program_.u_ssil_params2, ssil_params2);

    float ssil_params3[4] = {params.settings.thickness, 0.0f, 0.0f, 0.0f};
    gfx::set_uniform(trace_program_.u_ssil_params3, ssil_params3);
    gfx::set_uniform(trace_program_.u_pre_exposure, pre_exposure_.to_uniform().data());
    pre_exposure_.bind();

    // u_ssil_resolution: xy = full G-buffer size, zw = per-axis (full / trace) scale.
    const auto gbuf_sz = params.g_buffer->get_size();
    const auto trace_scale = compute_trace_scale(gbuf_sz, ssil_curr_fb->get_size());
    const float ssil_resolution[4] = {static_cast<float>(gbuf_sz.width),
                                      static_cast<float>(gbuf_sz.height),
                                      trace_scale.x,
                                      trace_scale.y};
    gfx::set_uniform(trace_program_.u_ssil_resolution, ssil_resolution);

    uint64_t topology = gfx::clip_fullscreen_triangle(1.0f);
    if(topology == 0)
    {
        topology = gfx::clip_quad(1.0f);
    }
    bgfx::setState(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, trace_program_.program->native_handle());

    bgfx::setState(BGFX_STATE_DEFAULT);
    trace_program_.program->end();
    bgfx::discard();

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
    const bool mixed = begin_program(downsample_program_.program) && begin_program(upsample_program_.program) &&
                       (num_passes - full_passes) > 0;
    if(!mixed)
    {
        full_passes = num_passes;
    }

    // ssil_curr already carries the trace resolution; the full-res tier buffers match it 1:1.
    auto fb_a = create_or_update_ssil_fb(rview, "SSIL_DENOISED_A", ssil_curr, trace_resolution::full, BGFX_TEXTURE_COMPUTE_WRITE);
    auto fb_b = create_or_update_ssil_fb(rview, "SSIL_DENOISED_B", ssil_curr, trace_resolution::full, BGFX_TEXTURE_COMPUTE_WRITE);
    auto sz = fb_a->get_size();
    uint32_t gx = compute_group_count(sz.width, SSIL_DENOISE_GROUP_SIZE);
    uint32_t gy = compute_group_count(sz.height, SSIL_DENOISE_GROUP_SIZE);

    // Variance ping-pong (SVGF). Single-channel R16F, compute-written and sampled. The
    // first pass integrates variance in-shader; each subsequent pass refilters the prior
    // pass's variance with the kernel weights squared, so the luminance sigma converges.
    auto make_variance_tex = [&](const std::string& name, const usize32_t& size) -> gfx::texture::ptr
    {
        return rview.tex_acquire(name,
                                 {.size = size,
                                  .format = bgfx::TextureFormat::R16F,
                                  .flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP});
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

        gfx::set_texture(denoise_program_.s_ssil_input, 0, in_tex);
        bgfx::setImage(1, out_fb->get_texture()->native_handle(), 0, bgfx::Access::Write);
        gfx::set_texture(denoise_program_.s_normal, 2, g_buffer->get_texture(1));
        gfx::set_texture(denoise_program_.s_depth, 3, g_buffer->get_texture(4));
        gfx::set_texture(denoise_program_.s_ssil_moments, 4, moments_tex);
        gfx::set_texture(denoise_program_.s_ssil_variance, 5, v_src);
        bgfx::setImage(6, v_dst->native_handle(), 0, bgfx::Access::Write);

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

    // Trace-pixel doubling chain (1, 2, 4, 8, ...). Both tiers express their dilation in
    // their own pixel space, so the half-res tier converts the same trace-pixel step into
    // its half-res step. This keeps the wavelet pyramid contiguous across the tier change.
    const int half_divisor = static_cast<int>(get_divisor(trace_resolution::half));
    for(int i = 0; i < full_passes; ++i)
    {
        const int step = std::min(1 << i, max_step);
        // Full-res tier preserves local detail -> full 5x5 (radius 2) kernel.
        run_atrous(src_tex, dst_fb, var_src, var_dst, gx, gy, step, i == 0, has_moments, 2,
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
    uint32_t hgx = compute_group_count(half_sz.width, SSIL_DENOISE_GROUP_SIZE);
    uint32_t hgy = compute_group_count(half_sz.height, SSIL_DENOISE_GROUP_SIZE);
    auto var_ha = make_variance_tex("SSIL_VARIANCE_HALF_A", half_sz);
    auto var_hb = make_variance_tex("SSIL_VARIANCE_HALF_B", half_sz);
    const bool has_downsampled_variance = full_passes > 0;

    // Geometry-aware downsample of the full-res tier result into the half-res input.
    // When a full-res tier ran, carry its variance with the colour so the wide half-res
    // passes keep the temporal/spatial variance guidance instead of recomputing it.
    {
        gfx::render_pass ds_pass("Spatial Denoise/Downsample Pass");
        ds_pass.set_view_proj(cam->get_view(), cam->get_projection());

        gfx::set_texture(downsample_program_.s_ssil_input, 0, src_tex);
        bgfx::setImage(1, half_a->get_texture()->native_handle(), 0, bgfx::Access::Write);
        gfx::set_texture(downsample_program_.s_normal, 2, g_buffer->get_texture(1));
        gfx::set_texture(downsample_program_.s_depth, 3, g_buffer->get_texture(4));
        gfx::set_texture(downsample_program_.s_ssil_variance, 4, var_src);
        bgfx::setImage(5, var_ha->native_handle(), 0, bgfx::Access::Write);

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
        // Continue the trace-pixel power-of-two chain across the tier boundary, then
        // express it in the half-res tier's own pixel space. With full_passes = 1 this
        // gives trace_step (1, 2, 4, ...) -> half_step (1, 1, 2, 4, ...), with no missing
        // octave.
        const int trace_step = 1 << (full_passes + j);
        const int capped_trace_step = std::min(trace_step, max_step);
        const int half_step = std::max(1, capped_trace_step / half_divisor);
        // The wide tier uses the narrow 3x3 (radius 1) kernel -- indirect diffuse is low-
        // frequency, so the outer ring adds little.
        run_atrous(half_src, half_dst, hvar_src, hvar_dst, hgx, hgy, half_step, first_half_pass, false, 1,
                   fmt::format("Spatial Denoise/Half Pass {}", j));

        half_src = half_dst->get_texture();
        half_dst = (half_dst == half_a) ? half_b : half_a;
        hvar_src = hvar_dst;
        hvar_dst = (hvar_dst == var_ha) ? var_hb : var_ha;
    }
    auto half_result_fb = (half_dst == half_a) ? half_b : half_a;

    // --- Bilateral upsample half-res wide result ---
    // Two cases:
    //
    // 1. trace == full: there is no outer upsample (run() skips it because reduced_res is
    //    false), so we MUST do the half->trace(=full) reconstruction here. Render into
    //    the free full-res framebuffer (`dst_fb`); silhouettes are reconstructed sharply
    //    because the upsample rejects cross-edge taps using the full-res G-buffer.
    //
    // 2. trace != full: run()'s outer upsample WILL run unconditionally, and it can
    //    reconstruct directly from half-of-trace to full in a single bilateral pass.
    //    Skipping the intermediate trace-res reconstruction here saves a whole pass per
    //    frame (no allocation, no bilateral resample, no full-bandwidth write) and the
    //    fused output is sharper because we don't introduce an intermediate softening
    //    bilateral resample.
    if(settings.resolution != trace_resolution::full)
    {
        return half_result_fb;
    }

    auto out_fb = dst_fb;
    {
        gfx::render_pass up_pass("Spatial Denoise/Upsample To Trace Resolution Pass");
        up_pass.bind(out_fb.get());
        up_pass.set_view_proj(cam->get_view(), cam->get_projection());

        gfx::set_texture(upsample_program_.s_ssil_input, 0, half_result_fb->get_texture());
        gfx::set_texture(upsample_program_.s_normal, 1, g_buffer->get_texture(1));
        gfx::set_texture(upsample_program_.s_depth, 2, g_buffer->get_texture(4));

        // Target dim = the framebuffer we render INTO (trace res). The shader uses it to
        // size its reconstruction footprint; without an explicit target the shader would
        // infer from s_depth (full-res G-buffer) and over-widen the kernel at reduced
        // trace resolutions.
        const auto out_sz = out_fb->get_size();
        float upsample_params[4] = {depth_sigma, normal_power, static_cast<float>(out_sz.width),
                                    static_cast<float>(out_sz.height)};
        gfx::set_uniform(upsample_program_.u_upsample_params, upsample_params);

        auto topology = gfx::clip_quad(1.0f);
        bgfx::setState(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(up_pass.id, upsample_program_.program->native_handle());

        bgfx::setState(BGFX_STATE_DEFAULT);
        upsample_program_.program->end();
        bgfx::discard();
    }

    return out_fb;
}

auto ssil_pass::run_temporal_resolve(gfx::render_view& rview,
                                     // By value: the caller's variable may also be out_result_fb,
                                     // which this function writes before it binds the input.
                                     gfx::frame_buffer::ptr ssil_input,
                                     const gfx::frame_buffer::ptr& g_buffer,
                                     const gfx::texture::ptr& prev_depth,
                                     const gfx::texture::ptr& velocity,
                                     const camera* cam,
                                     const ssil_settings& settings,
                                     gfx::frame_buffer::ptr& out_result_fb,
                                     gfx::texture::ptr& out_moments_tex) -> bool
{
    APP_SCOPE_PERF("Rendering/SSIL/Temporal Resolve Pass");

    // Ping-pong history. Two persistent MRT framebuffers (colour + moments) alternate
    // read/write on each run of the view, so the temporal shader writes DIRECTLY into the
    // next run's history texture, with no blit. The parity is the view's own: a view that
    // skips frames or runs twice in one still reads what its last run wrote.
    auto& history = rview.data().get_or_emplace<history_state>(history_state::view_key);
    const bool read_is_a = history.reads_a;
    history.reads_a = !read_is_a;

    const char* read_color_name = read_is_a ? "SSIL_HISTORY_A_COLOR" : "SSIL_HISTORY_B_COLOR";
    const char* read_moments_name = read_is_a ? "SSIL_HISTORY_A_MOMENTS" : "SSIL_HISTORY_B_MOMENTS";
    const char* write_fb_name = read_is_a ? "SSIL_HISTORY_B_FB" : "SSIL_HISTORY_A_FB";
    const char* write_color_name = read_is_a ? "SSIL_HISTORY_B_COLOR" : "SSIL_HISTORY_A_COLOR";
    const char* write_moments_name = read_is_a ? "SSIL_HISTORY_B_MOMENTS" : "SSIL_HISTORY_A_MOMENTS";

    // Detect "history was just allocated" by capturing the READ texture pointers BEFORE
    // create_or_update may replace them. If either differs after the call, we have no
    // valid history this frame and must seed (init mode). Both textures are tracked so
    // either-only recreation (e.g. resize that only hits one mip path) still triggers.
    auto old_read_color = rview.tex_find(read_color_name);
    auto old_read_moments = rview.tex_find(read_moments_name);
    auto read_color = create_or_update_ssil_tex(rview, read_color_name, ssil_input, trace_resolution::full);
    auto read_moments = create_or_update_ssil_tex(rview, read_moments_name, ssil_input, trace_resolution::full);
    const bool history_just_allocated =
        (read_color != old_read_color) || (read_moments != old_read_moments);

    auto write_fb = create_or_update_ssil_fb_mrt(rview, write_fb_name, write_color_name, write_moments_name,
                                                 ssil_input, trace_resolution::full);
    auto write_moments = rview.tex_find(write_moments_name);

    // The temporal result goes back to the caller through the out parameters; the
    // indirect-lighting consumer samples the texture run() returns.
    out_result_fb = write_fb;
    out_moments_tex = write_moments;

    const auto gbuf_sz = g_buffer->get_size();
    const auto temporal_scale = compute_trace_scale(gbuf_sz, ssil_input->get_size());
    const float temporal_resolution[4] = {
        static_cast<float>(gbuf_sz.width),
        static_cast<float>(gbuf_sz.height),
        temporal_scale.x,
        temporal_scale.y};

    // The velocity buffer arrives explicitly from the pipeline; a valid texture IS the
    // enable. Depth stands in as an inert placeholder so the sampler slot is always bound.
    const auto& velocity_tex = velocity;
    const bool use_velocity = velocity_tex != nullptr;

    const float temporal_params2[4] = {settings.temporal.normal_dot_threshold,
                                       use_velocity ? 1.0f : 0.0f,
                                       0.0f,
                                       0.0f};

    auto bind_common = [&](float enable_temporal,
                           const gfx::texture::ptr& prev_depth_tex)
    {
        gfx::set_texture(temporal_program_.s_ssil_curr, 0, ssil_input->get_texture());
        gfx::set_texture(temporal_program_.s_ssil_history, 1, read_color);
        gfx::set_texture(temporal_program_.s_depth, 2, g_buffer->get_texture(4));
        gfx::set_texture(temporal_program_.s_prev_depth, 3, prev_depth_tex);
        gfx::set_texture(temporal_program_.s_ssil_moments_history, 4, read_moments);
        gfx::set_texture(temporal_program_.s_normal, 5, g_buffer->get_texture(1));
        gfx::set_texture(temporal_program_.s_velocity,
                         6,
                         use_velocity ? velocity_tex : g_buffer->get_texture(4),
                         BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

        float temporal_params[4] = {enable_temporal,
                                    settings.temporal.history_strength,
                                    settings.temporal.depth_threshold,
                                    float(settings.temporal.max_accum_frames)};
        gfx::set_uniform(temporal_program_.u_temporal_params, temporal_params);
        gfx::set_uniform(temporal_program_.u_temporal_params2, temporal_params2);
        gfx::set_uniform(temporal_program_.u_temporal_resolution, temporal_resolution);
        gfx::set_uniform(temporal_program_.u_pre_exposure, pre_exposure_.to_uniform().data());
        pre_exposure_.bind();

        // The TAA-unjittered previous pair, never get_prev_view_projection(): the jittered
        // prev misaligns a still camera's reprojection by the jitter delta every frame.
        auto prev_vp = cam->get_prev_view_projection_unjittered();
        gfx::set_uniform(temporal_program_.u_prev_view_proj, prev_vp.get_matrix());
    };

    // Init mode: history was just allocated -- RGBA16F contains undefined data (possibly
    // NaN). Run the temporal shader with `u_enable_temporal = 0`, which takes the early
    // path that writes colour = curr and seeds the moments texture with valid (L, L^2)
    // values. The shader writes DIRECTLY into the write fb -- no blit needed because the
    // ping-pong picks this fb up as the READ next frame automatically.
    if(history_just_allocated || !prev_depth)
    {
        gfx::render_pass init_pass("Temporal Init Pass");
        init_pass.bind(write_fb.get());
        init_pass.set_view_proj(cam->get_view(), cam->get_projection());

        bind_common(0.0f, g_buffer->get_texture(4));

        auto topology = gfx::clip_quad(1.0f);
        bgfx::setState(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(init_pass.id, temporal_program_.program->native_handle());

        bgfx::setState(BGFX_STATE_DEFAULT);
        temporal_program_.program->end();
        bgfx::discard();
        return false;
    }

    gfx::render_pass pass("Temporal Resolve Pass");
    pass.bind(write_fb.get());
    pass.set_view_proj(cam->get_view(), cam->get_projection());

    bind_common(settings.enable_temporal_accumulation ? 1.0f : 0.0f, prev_depth);

    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, temporal_program_.program->native_handle());

    bgfx::setState(BGFX_STATE_DEFAULT);
    temporal_program_.program->end();
    bgfx::discard();

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

    gfx::set_texture(upsample_program_.s_ssil_input, 0, ssil_input->get_texture());
    gfx::set_texture(upsample_program_.s_normal, 1, g_buffer->get_texture(1));
    gfx::set_texture(upsample_program_.s_depth, 2, g_buffer->get_texture(4));

    // Reuse the spatial-denoise edge-stopping sigmas for upsample tap rejection. Target
    // dim = full G-buffer dim (the output framebuffer we render INTO); see the in-shader
    // u_target_dim docs for why the shader cannot infer it from s_depth.
    const auto out_sz = out_fb->get_size();
    float upsample_params[4] = {
        settings.spatial_denoise.depth_sigma,
        settings.spatial_denoise.normal_power,
        static_cast<float>(out_sz.width),
        static_cast<float>(out_sz.height)};
    gfx::set_uniform(upsample_program_.u_upsample_params, upsample_params);

    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, upsample_program_.program->native_handle());

    bgfx::setState(BGFX_STATE_DEFAULT);
    upsample_program_.program->end();
    bgfx::discard();

    return out_fb;
}

void ssil_pass::release_history_resources(gfx::render_view& rview)
{
    rview.fbo_remove("SSIL_HISTORY_A_FB");
    rview.fbo_remove("SSIL_HISTORY_B_FB");
    rview.tex_remove("SSIL_HISTORY_A_COLOR");
    rview.tex_remove("SSIL_HISTORY_A_MOMENTS");
    rview.tex_remove("SSIL_HISTORY_B_COLOR");
    rview.tex_remove("SSIL_HISTORY_B_MOMENTS");
}

void ssil_pass::release_denoise_resources(gfx::render_view& rview)
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

void ssil_pass::release_resources(gfx::render_view& rview)
{
    rview.fbo_remove("SSIL_CURR");
    rview.tex_remove("SSIL_CURR");
    release_denoise_resources(rview);
    release_history_resources(rview);
    rview.fbo_remove("SSIL_UPSAMPLED");
    rview.tex_remove("SSIL_UPSAMPLED");
}

} // namespace unravel
