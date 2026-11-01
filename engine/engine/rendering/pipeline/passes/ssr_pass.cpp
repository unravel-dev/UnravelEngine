#include "ssr_pass.h"
#include "pass_helpers.h"
#include <algorithm>
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{
namespace
{
/// Compute group edge of the spatial denoise.
constexpr uint32_t SSR_DENOISE_GROUP_SIZE = 8;
/// Frames of the trace's temporal jitter before it repeats.
constexpr uint32_t TRACE_FRAME_INDEX_PERIOD = 4;

/// The trace resolution the settings ask for, capped at half: tracing below it breaks Hi-Z traversal, the temporal
/// clamp and the spatial denoiser.
auto get_capped_trace_resolution(trace_resolution requested) -> trace_resolution
{
    return get_divisor(requested) > get_divisor(trace_resolution::half) ? trace_resolution::half : requested;
}
} // namespace

auto ssr_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    // Load shaders
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.eshader");

    // Load SSR shader (trace pass)
    auto fs_ssr_fidelityfx = am.get_asset<gfx::shader>("engine:/data/shaders/ssr/fs_ssr_fidelityfx.eshader");

    // Load temporal resolve shader
    auto fs_ssr_temporal_resolve = am.get_asset<gfx::shader>("engine:/data/shaders/ssr/fs_ssr_temporal_resolve.eshader");

    // Load composite shader
    auto fs_ssr_composite = am.get_asset<gfx::shader>("engine:/data/shaders/ssr/fs_ssr_composite.eshader");

    // Load spatial denoise compute shader
    auto cs_ssr_spatial_denoise = am.get_asset<gfx::shader>("engine:/data/shaders/ssr/cs_ssr_spatial_denoise.eshader");

    // Create SSR trace program
    fidelityfx_pixel_program_.cache_uniforms();
    fidelityfx_pixel_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssr_fidelityfx);

    // Create temporal resolve program
    temporal_resolve_program_.cache_uniforms();
    temporal_resolve_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssr_temporal_resolve);

    // Create composite program
    composite_program_.cache_uniforms();
    composite_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssr_composite);

    // Create spatial denoise compute program
    spatial_denoise_compute_program_.cache_uniforms();
    spatial_denoise_compute_program_.program = std::make_unique<gpu_program>(cs_ssr_spatial_denoise);

    // Validate all programs
    bool all_valid = fidelityfx_pixel_program_.is_valid() && temporal_resolve_program_.is_valid() &&
                     composite_program_.is_valid() && spatial_denoise_compute_program_.is_valid();

    return all_valid;
}

auto ssr_pass::create_or_update_ssr_curr_fb(gfx::render_view& rview,
                                            const gfx::frame_buffer::ptr& reference,
                                            trace_resolution res) -> gfx::frame_buffer::ptr
{
    const gfx::texture_desc color_desc{.size = compute_trace_size(reference->get_size(), res),
                                       .format = bgfx::TextureFormat::RGBA16F,
                                       .flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP};
    const auto& ssr_curr_tex = rview.tex_acquire("SSR_CURR", color_desc);

    // Attachment 1: confidence-weighted mean hit distance (view-space metres). The
    // temporal's per-pixel content validation - hit-point velocity read + hit-distance
    // history compare - lives on this lane; R16F covers the trace range at ~0.1%
    // relative, plenty for a compare threshold.
    auto distance_desc = color_desc;
    distance_desc.format = bgfx::TextureFormat::R16F;
    const auto& ssr_curr_t_tex = rview.tex_acquire("SSR_CURR_T", distance_desc);

    return acquire_frame_buffer(rview, "SSR_CURR", {ssr_curr_tex, ssr_curr_t_tex});
}

auto ssr_pass::create_or_update_ssr_history_tex(gfx::render_view& rview,
                                                const gfx::frame_buffer::ptr& reference,
                                                trace_resolution res) -> gfx::texture::ptr
{
    const gfx::texture_desc color_desc{.size = compute_trace_size(reference->get_size(), res),
                                       .format = bgfx::TextureFormat::RGBA16F,
                                       .flags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                BGFX_SAMPLER_V_CLAMP};
    const auto& history_tex = rview.tex_acquire("SSR_HISTORY", color_desc);

    // Hit-distance history rides alongside the colour history (same size, same blits).
    auto distance_desc = color_desc;
    distance_desc.format = bgfx::TextureFormat::R16F;
    rview.tex_acquire("SSR_HISTORY_T", distance_desc);

    return history_tex;
}

auto ssr_pass::create_or_update_ssr_history_temp_fb(gfx::render_view& rview,
                                                    const gfx::frame_buffer::ptr& reference,
                                                    trace_resolution res) -> gfx::frame_buffer::ptr
{
    const gfx::texture_desc color_desc{.size = compute_trace_size(reference->get_size(), res),
                                       .format = bgfx::TextureFormat::RGBA16F,
                                       .flags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                BGFX_SAMPLER_V_CLAMP};
    const auto& temp_tex = rview.tex_acquire("SSR_HISTORY_TEMP", color_desc);

    // MRT lane for the resolved hit-distance history (blitted into SSR_HISTORY_T).
    auto distance_desc = color_desc;
    distance_desc.format = bgfx::TextureFormat::R16F;
    const auto& temp_t_tex = rview.tex_acquire("SSR_HISTORY_T_TEMP", distance_desc);

    return acquire_frame_buffer(rview, "SSR_HISTORY_TEMP", {temp_tex, temp_t_tex});
}

auto ssr_pass::create_or_update_ssr_denoise_fb(gfx::render_view& rview,
                                               const std::string& name,
                                               const gfx::frame_buffer::ptr& reference,
                                               trace_resolution res) -> gfx::frame_buffer::ptr
{
    const auto& denoised_tex = rview.tex_acquire(name,
                                                 {.size = compute_trace_size(reference->get_size(), res),
                                                  .format = bgfx::TextureFormat::RGBA16F,
                                                  .flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_RT |
                                                           BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP});

    return acquire_frame_buffer(rview, name, {denoised_tex});
}

auto ssr_pass::run_spatial_denoise(gfx::render_view& rview,
                                   const gfx::frame_buffer::ptr& ssr_curr,
                                   const gfx::frame_buffer::ptr& g_buffer,
                                   const fidelityfx_ssr_settings& settings) -> gfx::frame_buffer::ptr
{
    if(!begin_program(spatial_denoise_compute_program_.program))
    {
        return ssr_curr;
    }

    APP_SCOPE_PERF("Rendering/SSR/Spatial Denoise Pass");

    const int num_passes = std::clamp(settings.spatial_denoise.passes, 1, 5);

    // ssr_curr already carries the trace resolution; denoise buffers match it 1:1.
    // Two ping-pong framebuffers so the a-trous step doubles each iteration
    // (1, 2, 4, ...) without aliasing reads against writes.
    auto fb_a = create_or_update_ssr_denoise_fb(rview, "SSR_DENOISED_A", ssr_curr, trace_resolution::full);
    auto fb_b = create_or_update_ssr_denoise_fb(rview, "SSR_DENOISED_B", ssr_curr, trace_resolution::full);
    auto sz = fb_a->get_size();
    const uint32_t gx = compute_group_count(sz.width, SSR_DENOISE_GROUP_SIZE);
    const uint32_t gy = compute_group_count(sz.height, SSR_DENOISE_GROUP_SIZE);

    auto src_tex = ssr_curr->get_texture();
    gfx::frame_buffer::ptr dst_fb = fb_a;

    for(int i = 0; i < num_passes; ++i)
    {
        gfx::render_pass pass(fmt::format("Spatial Denoise Pass {}", i).c_str());

        gfx::set_texture(spatial_denoise_compute_program_.s_ssr_input, 0, src_tex);
        bgfx::setImage(1, dst_fb->get_texture()->native_handle(), 0, bgfx::Access::Write);
        gfx::set_texture(spatial_denoise_compute_program_.s_normal, 2, g_buffer->get_texture(1));
        gfx::set_texture(spatial_denoise_compute_program_.s_depth, 3, g_buffer->get_texture(4));

        float denoise_params[4] = {
            float(1 << i),
            settings.spatial_denoise.depth_sigma,
            settings.spatial_denoise.normal_power,
            settings.spatial_denoise.luma_sigma};
        gfx::set_uniform(spatial_denoise_compute_program_.u_denoise_params, denoise_params);

        gfx::dispatch(pass.id, spatial_denoise_compute_program_.program->native_handle(), gx, gy, 1);

        spatial_denoise_compute_program_.program->end();

        src_tex = dst_fb->get_texture();
        dst_fb = (dst_fb == fb_a) ? fb_b : fb_a;
    }

    // The final result lives in whichever fb we last *wrote* to, which is the
    // one NOT pointed at by dst_fb (we flipped at the end of the loop).
    return (dst_fb == fb_a) ? fb_b : fb_a;
}

auto ssr_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    // Ensure we have valid input
    if(!params.g_buffer || !params.output)
    {
        return nullptr;
    }

    pre_exposure_ = params.pre_exposure;

    gfx::render_pass::push_scope("SSR");
    // Pass 1: SSR Trace - generates SSR_CURR
    auto ssr_curr_fb = run_ssr_trace(rview, params);
    if(!ssr_curr_fb)
    {
        gfx::render_pass::pop_scope();
        return nullptr;
    }

    // Pass 1.5: Spatial Denoise (optional) - filters SSR_CURR before temporal resolve
    auto temporal_input_fb = ssr_curr_fb;
    if(params.settings.fidelityfx.enable_spatial_denoise)
    {
        temporal_input_fb = run_spatial_denoise(rview, ssr_curr_fb, params.g_buffer, params.settings.fidelityfx);
    }
    else
    {
        rview.fbo_remove("SSR_DENOISED_A");
        rview.tex_remove("SSR_DENOISED_A");
        rview.fbo_remove("SSR_DENOISED_B");
        rview.tex_remove("SSR_DENOISED_B");
    }

    // Pass 2: Temporal Resolve - reads (denoised) SSR_CURR + SSR_HIST, writes new SSR_HIST.
    // The hit-distance lane always comes from the TRACE target (validation data, never
    // filtered), regardless of whether the colour input was spatially denoised.
    auto ssr_history_fb = run_temporal_resolve(rview,
                                               temporal_input_fb,
                                               ssr_curr_fb->get_texture(1),
                                               params.g_buffer,
                                               params.velocity,
                                               params.velocity_movers_recent,
                                               params.cam,
                                               params.settings.fidelityfx);
    // No history this frame (just allocated, or no temporal program): the composite shows the temporal's input, the
    // image a resolve with accumulation off would have written.
    const auto& composite_color_fb = ssr_history_fb ? ssr_history_fb : temporal_input_fb;

    // Pass 3: Composite - blends the resolved color by SSR_CURR's confidence into the output
    auto composite_fb = run_composite(composite_color_fb, ssr_curr_fb, params.g_buffer, params.output);
    gfx::render_pass::pop_scope();
    return composite_fb;
}

auto ssr_pass::run_ssr_trace(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    if(!begin_program(fidelityfx_pixel_program_.program))
    {
        return nullptr;
    }
    const auto trace_res = get_capped_trace_resolution(params.settings.fidelityfx.resolution);
    auto ssr_curr_fbo = create_or_update_ssr_curr_fb(rview, params.g_buffer, trace_res);

    // ============================================================================
    // SSR Trace Pass
    // ============================================================================
    APP_SCOPE_PERF("Rendering/SSR/Trace Pass");

    gfx::render_pass pass("Trace Pass");
    pass.bind(ssr_curr_fbo.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());

    // Set input textures
    gfx::set_texture(fidelityfx_pixel_program_.s_color, 0, params.previous_frame);
    gfx::set_texture(fidelityfx_pixel_program_.s_normal, 1, params.g_buffer->get_texture(1));
    gfx::set_texture(fidelityfx_pixel_program_.s_depth, 2, params.g_buffer->get_texture(4));
    gfx::set_texture(fidelityfx_pixel_program_.s_hiz, 3, params.hiz_buffer);

    // Set SSR parameters (max_steps, depth_tolerance, max_rays, brightness)
    float ssr_params[4] = {float(params.settings.fidelityfx.max_steps),
                           params.settings.fidelityfx.depth_tolerance,
                           float(params.settings.fidelityfx.max_rays),
                           params.settings.fidelityfx.brightness};
    gfx::set_uniform(fidelityfx_pixel_program_.u_ssr_params, ssr_params);
    gfx::set_uniform(fidelityfx_pixel_program_.u_pre_exposure, pre_exposure_.to_uniform().data());
    pre_exposure_.bind();

    const auto ssr_scale = compute_trace_scale(params.g_buffer->get_size(), ssr_curr_fbo->get_size());
    // u_hiz_params layout: (hiz_width, hiz_height, scale_x, scale_y).
    float hiz_params[4] = {0.0f, 0.0f, ssr_scale.x, ssr_scale.y};
    if(params.hiz_buffer)
    {
        hiz_params[0] = float(params.hiz_buffer->info.width);
        hiz_params[1] = float(params.hiz_buffer->info.height);
    }
    gfx::set_uniform(fidelityfx_pixel_program_.u_hiz_params, hiz_params);

    // Set fade parameters (screen_edge_fade, unused, roughness_depth_tolerance, facing_reflections_fading)
    float fade_params[4] = {params.settings.fidelityfx.screen_edge_fade,
                            0.0f,
                            params.settings.fidelityfx.roughness_depth_tolerance,
                            params.settings.fidelityfx.facing_reflections_fading};
    gfx::set_uniform(fidelityfx_pixel_program_.u_fade_params, fade_params);

    // x = frame number for temporal jitter.
    float frame_params[4] = {float(gfx::get_render_frame() % TRACE_FRAME_INDEX_PERIOD), 0.0f, 0.0f, 0.0f};
    gfx::set_uniform(fidelityfx_pixel_program_.u_ssr_frame_params, frame_params);

    // The TAA-unjittered previous pair, never get_prev_view_projection(): the jittered prev
    // misaligns a still camera's reprojection by the jitter delta every frame.
    auto prev_view_proj = params.cam->get_prev_view_projection_unjittered();
    gfx::set_uniform(fidelityfx_pixel_program_.u_prev_view_proj, prev_view_proj.get_matrix());

    uint64_t topology = gfx::clip_fullscreen_triangle(1.0f);
    if(topology == 0)
    {
        topology = gfx::clip_quad(1.0f);
    }
    bgfx::setState(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, fidelityfx_pixel_program_.program->native_handle());

    // Reset state
    bgfx::setState(BGFX_STATE_DEFAULT);
    fidelityfx_pixel_program_.program->end();
    bgfx::discard();

    return ssr_curr_fbo;
}

auto ssr_pass::run_temporal_resolve(gfx::render_view& rview,
                                    const gfx::frame_buffer::ptr& ssr_curr,
                                    const gfx::texture::ptr& curr_hit_t,
                                    const gfx::frame_buffer::ptr& g_buffer,
                                    const gfx::texture::ptr& velocity,
                                    bool velocity_movers_recent,
                                    const camera* cam,
                                    const fidelityfx_ssr_settings& settings) -> gfx::frame_buffer::ptr
{
    if(!begin_program(temporal_resolve_program_.program))
    {
        return nullptr;
    }

    // History buffers match ssr_curr's size exactly; ssr_curr already carries the trace
    // resolution, so we ask the helpers not to downscale any further.
    auto old_history = rview.tex_find("SSR_HISTORY");
    auto history_tex = create_or_update_ssr_history_tex(rview, ssr_curr, trace_resolution::full);
    auto history_t_tex = rview.tex_get("SSR_HISTORY_T");
    auto temp_fbo = create_or_update_ssr_history_temp_fb(rview, ssr_curr, trace_resolution::full);

    // History was just allocated -- RGBA16F contains undefined data (possibly NaN).
    // Seed it with the current frame and skip temporal this frame.
    if(history_tex != old_history)
    {
        gfx::render_pass blit_pass("History Init Blit Pass");
        bgfx::blit(blit_pass.id,
                   bgfx::TextureRegion{.handle = history_tex->native_handle()},
                   bgfx::TextureRegion{.handle = ssr_curr->get_texture()->native_handle()});
        bgfx::blit(blit_pass.id,
                   bgfx::TextureRegion{.handle = history_t_tex->native_handle()},
                   bgfx::TextureRegion{.handle = curr_hit_t->native_handle()});
        return nullptr;
    }

    // ============================================================================
    // Temporal Resolve Pass
    // ============================================================================
    APP_SCOPE_PERF("Rendering/SSR/Temporal Resolve Pass");

    gfx::render_pass pass("Temporal Resolve Pass");
    pass.bind(temp_fbo.get());
    pass.set_view_proj(cam->get_view(), cam->get_projection());

    // Set input textures
    gfx::set_texture(temporal_resolve_program_.s_ssr_curr, 0, ssr_curr->get_texture());
    gfx::set_texture(temporal_resolve_program_.s_ssr_history, 1, history_tex);
    gfx::set_texture(temporal_resolve_program_.s_normal, 2, g_buffer->get_texture(1));
    gfx::set_texture(temporal_resolve_program_.s_depth, 3, g_buffer->get_texture(4));

    // The velocity buffer arrives explicitly from the pipeline; a valid texture IS the
    // enable. Depth stands in as an inert placeholder so the sampler slot is always bound.
    const bool use_velocity = velocity != nullptr;
    gfx::set_texture(temporal_resolve_program_.s_velocity,
                     4,
                     use_velocity ? velocity : g_buffer->get_texture(4),
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    // The per-pixel content-validation lanes: this frame's mean hit distance and the
    // accumulated hit-distance history (both view-space metres, 0 = no confident data).
    gfx::set_texture(temporal_resolve_program_.s_ssr_curr_hit_t, 5, curr_hit_t);
    gfx::set_texture(temporal_resolve_program_.s_ssr_hist_hit_t, 6, history_t_tex);

    // Set temporal parameters (enable_temporal, history_strength, depth_threshold; w unused)
    float temporal_params[4] = {settings.enable_temporal_accumulation ? 1.0f : 0.0f,
                                settings.temporal.history_strength,
                                settings.temporal.depth_threshold,
                                0.0f};
    gfx::set_uniform(temporal_resolve_program_.u_temporal_params, temporal_params);

    // Set motion parameters (motion_scale_pixels, normal_dot_threshold, max_accum_frames, velocity flag)
    float motion_params[4] = {
        settings.temporal.motion_scale_pixels,
        settings.temporal.normal_dot_threshold,
        float(settings.temporal.max_accum_frames),
        use_velocity ? 1.0f : 0.0f
    };
    gfx::set_uniform(temporal_resolve_program_.u_motion_params, motion_params);
    gfx::set_uniform(temporal_resolve_program_.u_pre_exposure, pre_exposure_.to_uniform().data());
    pre_exposure_.bind();

    const auto ssr_scale = compute_trace_scale(g_buffer->get_size(), history_tex->get_size());

    // x = the CONTENT-LAG release ceiling: the trace samples PREV_SCENE_HDR, so a moving
    // emitter's radiance sweeps every glossy surface one frame late while geometry at the
    // hit stays static and t-confirmed - invisible to the per-pixel guards. While any
    // mover was drawn within one accumulation window the release is capped screen-wide:
    // a shallow window is the CORRECT window for radiance that changes every frame.
    // y unused; zw = the resolution scale.
    const float release_ceiling = velocity_movers_recent ? 0.125f : 1.0f;
    float fade_params[4] = {release_ceiling, 0.0f, ssr_scale.x, ssr_scale.y};
    gfx::set_uniform(temporal_resolve_program_.u_fade_params, fade_params);

    // The TAA-unjittered previous pair (see the trace-side comment); keeps the camera-only
    // reprojection jitter-aligned with the velocity buffer's convention.
    auto prev_view_proj = cam->get_prev_view_projection_unjittered();
    gfx::set_uniform(temporal_resolve_program_.u_prev_view_proj, prev_view_proj.get_matrix());

    // Draw fullscreen quad
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, temporal_resolve_program_.program->native_handle());

    // Reset state
    bgfx::setState(BGFX_STATE_DEFAULT);
    temporal_resolve_program_.program->end();
    bgfx::discard();

    // ============================================================================
    // Blit temp_fbo texture into persistent history_tex for next frame
    // ============================================================================
    gfx::render_pass blit_pass("History Blit Pass");
    bgfx::blit(blit_pass.id,
               bgfx::TextureRegion{.handle = history_tex->native_handle()},
               bgfx::TextureRegion{.handle = temp_fbo->get_texture()->native_handle()});
    bgfx::blit(blit_pass.id,
               bgfx::TextureRegion{.handle = history_t_tex->native_handle()},
               bgfx::TextureRegion{.handle = temp_fbo->get_texture(1)->native_handle()});

    return temp_fbo;
}

auto ssr_pass::run_composite(const gfx::frame_buffer::ptr& ssr_history,
                             const gfx::frame_buffer::ptr& ssr_curr,
                             const gfx::frame_buffer::ptr& g_buffer,
                             const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr
{
    if(!begin_program(composite_program_.program))
    {
        return nullptr;
    }

    // ============================================================================
    // Composite Pass
    // ============================================================================
    APP_SCOPE_PERF("Rendering/SSR/Composite Pass");

    gfx::render_pass pass("Composite Pass");
    pass.bind(output.get());

    // Set input textures
    gfx::set_texture(composite_program_.s_ssr_history, 0, ssr_history->get_texture());
    gfx::set_texture(composite_program_.s_ssr_curr, 1, ssr_curr->get_texture());
    gfx::set_texture(composite_program_.s_normal, 2, g_buffer->get_texture(1));
    gfx::set_texture(composite_program_.s_depth, 3, g_buffer->get_texture(4));

    // Draw fullscreen quad with alpha blending over the traced layers in RBUFFER. Alpha is
    // multiplied by 1 - confidence, so it keeps the share the traced layers leave to the probe
    // layer (ComposeIndirectSpecular in lighting.sh).
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
                                                  BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                                  BGFX_STATE_BLEND_ZERO,
                                                  BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(pass.id, composite_program_.program->native_handle());

    // Reset state
    bgfx::setState(BGFX_STATE_DEFAULT);
    composite_program_.program->end();
    bgfx::discard();

    return output;
}

void ssr_pass::release_resources(gfx::render_view& rview)
{
    rview.fbo_remove("SSR_CURR");
    rview.tex_remove("SSR_CURR");
    rview.tex_remove("SSR_HISTORY");
    rview.fbo_remove("SSR_HISTORY_TEMP");
    rview.tex_remove("SSR_HISTORY_TEMP");
    rview.fbo_remove("SSR_DENOISED_A");
    rview.tex_remove("SSR_DENOISED_A");
    rview.fbo_remove("SSR_DENOISED_B");
    rview.tex_remove("SSR_DENOISED_B");
}

} // namespace unravel
