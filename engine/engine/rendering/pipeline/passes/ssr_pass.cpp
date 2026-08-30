#include "ssr_pass.h"
#include <algorithm>
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{

auto ssr_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    // Load shaders
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");

    // Load FidelityFX SSR shader (trace pass)
    auto fs_ssr_fidelityfx = am.get_asset<gfx::shader>("engine:/data/shaders/ssr/fs_ssr_fidelityfx.sc");

    // Load temporal resolve shader
    auto fs_ssr_temporal_resolve = am.get_asset<gfx::shader>("engine:/data/shaders/ssr/fs_ssr_temporal_resolve.sc");

    // Load composite shader
    auto fs_ssr_composite = am.get_asset<gfx::shader>("engine:/data/shaders/ssr/fs_ssr_composite.sc");

    // Load unified blur compute shader for cone tracing
    auto cs_ssr_blur = am.get_asset<gfx::shader>("engine:/data/shaders/ssr/cs_ssr_blur.sc");

    // Load spatial denoise compute shader
    auto cs_ssr_spatial_denoise = am.get_asset<gfx::shader>("engine:/data/shaders/ssr/cs_ssr_spatial_denoise.sc");

    // Create FidelityFX SSR programs
    fidelityfx_pixel_program_.cache_uniforms();
    fidelityfx_pixel_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssr_fidelityfx);

    // Create temporal resolve program
    temporal_resolve_program_.cache_uniforms();
    temporal_resolve_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssr_temporal_resolve);

    // Create composite program
    composite_program_.cache_uniforms();
    composite_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssr_composite);

    // Create unified blur compute program for cone tracing
    blur_compute_program_.cache_uniforms();
    blur_compute_program_.program = std::make_unique<gpu_program>(cs_ssr_blur);

    // Create spatial denoise compute program
    spatial_denoise_compute_program_.cache_uniforms();
    spatial_denoise_compute_program_.program = std::make_unique<gpu_program>(cs_ssr_spatial_denoise);

    // Validate all programs
    bool all_valid = fidelityfx_pixel_program_.is_valid() && temporal_resolve_program_.is_valid() &&
                     composite_program_.is_valid() && blur_compute_program_.is_valid() &&
                     spatial_denoise_compute_program_.is_valid();

    return all_valid;
}

auto ssr_pass::create_or_update_output_fb(gfx::render_view& rview,
                                          const gfx::frame_buffer::ptr& reference,
                                          const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr
{
    // If the caller provided an output framebuffer, just return it.
    if(output)
    {
        return output;
    }

    // Otherwise, use the render_view to get or create the SSR output framebuffer
    auto ref_sz = reference->get_size();
    auto ref_format = gfx::texture_format::RGBA16F;

    auto& ssr_output_tex = rview.tex_get_or_emplace("SSR_OUTPUT");
    if(gfx::needs_recreate(ssr_output_tex, ref_sz, ref_format))
    {
        ssr_output_tex.reset();
        ssr_output_tex = std::make_shared<gfx::texture>(ref_sz.width,
                                                        ref_sz.height,
                                                        false,
                                                        1,
                                                        ref_format,
                                                        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                            BGFX_SAMPLER_V_CLAMP);
    }

    auto& ssr_output_fbo = rview.fbo_get_or_emplace("SSR_OUTPUT");
    if(gfx::needs_recreate(ssr_output_fbo, ref_sz))
    {
        ssr_output_fbo.reset();
        ssr_output_fbo = std::make_shared<gfx::frame_buffer>();
        ssr_output_fbo->populate({ssr_output_tex});
    }

    return ssr_output_fbo;
}

auto ssr_pass::create_or_update_ssr_curr_fb(gfx::render_view& rview,
                                            const gfx::frame_buffer::ptr& reference,
                                            trace_resolution res) -> gfx::frame_buffer::ptr
{
    const auto target_size = compute_trace_size(reference->get_size(), res);
    const auto ref_format = gfx::texture_format::RGBA16F;

    auto& ssr_curr_tex = rview.tex_get_or_emplace("SSR_CURR");
    if(gfx::needs_recreate(ssr_curr_tex, target_size, ref_format))
    {
        ssr_curr_tex.reset();
        ssr_curr_tex = std::make_shared<gfx::texture>(target_size.width,
                                                      target_size.height,
                                                      false,
                                                      1,
                                                      ref_format,
                                                      BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                          BGFX_SAMPLER_V_CLAMP);
    }

    // Attachment 1: confidence-weighted mean hit distance (view-space metres). The
    // temporal's per-pixel content validation - hit-point velocity read + hit-distance
    // history compare - lives on this lane; R16F covers the trace range at ~0.1%
    // relative, plenty for a compare threshold.
    auto& ssr_curr_t_tex = rview.tex_get_or_emplace("SSR_CURR_T");
    if(gfx::needs_recreate(ssr_curr_t_tex, target_size, gfx::texture_format::R16F))
    {
        ssr_curr_t_tex.reset();
        ssr_curr_t_tex = std::make_shared<gfx::texture>(target_size.width,
                                                        target_size.height,
                                                        false,
                                                        1,
                                                        gfx::texture_format::R16F,
                                                        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                            BGFX_SAMPLER_V_CLAMP);
    }

    auto& ssr_curr_fbo = rview.fbo_get_or_emplace("SSR_CURR");
    if(gfx::needs_recreate(ssr_curr_fbo, target_size))
    {
        ssr_curr_fbo.reset();
        ssr_curr_fbo = std::make_shared<gfx::frame_buffer>();
        ssr_curr_fbo->populate({ssr_curr_tex, ssr_curr_t_tex});
    }

    return ssr_curr_fbo;
}

auto ssr_pass::create_or_update_ssr_history_tex(gfx::render_view& rview,
                                                const gfx::frame_buffer::ptr& reference,
                                                trace_resolution res) -> gfx::texture::ptr
{
    const auto target_size = compute_trace_size(reference->get_size(), res);
    const auto ref_format = gfx::texture_format::RGBA16F;

    auto& history_tex = rview.tex_get_or_emplace("SSR_HISTORY");
    if(gfx::needs_recreate(history_tex, target_size, ref_format))
    {
        history_tex.reset();
        history_tex = std::make_shared<gfx::texture>(target_size.width,
                                                     target_size.height,
                                                     false,
                                                     1,
                                                     ref_format,
                                                     BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                         BGFX_SAMPLER_V_CLAMP);
    }

    // Hit-distance history rides alongside the colour history (same size, same blits).
    auto& history_t_tex = rview.tex_get_or_emplace("SSR_HISTORY_T");
    if(gfx::needs_recreate(history_t_tex, target_size, gfx::texture_format::R16F))
    {
        history_t_tex.reset();
        history_t_tex = std::make_shared<gfx::texture>(target_size.width,
                                                       target_size.height,
                                                       false,
                                                       1,
                                                       gfx::texture_format::R16F,
                                                       BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                           BGFX_SAMPLER_V_CLAMP);
    }

    return history_tex;
}

auto ssr_pass::create_or_update_ssr_history_temp_fb(gfx::render_view& rview,
                                                    const gfx::frame_buffer::ptr& reference,
                                                    trace_resolution res) -> gfx::frame_buffer::ptr
{
    const auto target_size = compute_trace_size(reference->get_size(), res);
    const auto ref_format = gfx::texture_format::RGBA16F;

    auto& temp_tex = rview.tex_get_or_emplace("SSR_HISTORY_TEMP");
    if(gfx::needs_recreate(temp_tex, target_size, ref_format))
    {
        temp_tex.reset();
        temp_tex = std::make_shared<gfx::texture>(target_size.width,
                                                  target_size.height,
                                                  false,
                                                  1,
                                                  ref_format,
                                                  BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                      BGFX_SAMPLER_V_CLAMP);
    }

    // MRT lane for the resolved hit-distance history (blitted into SSR_HISTORY_T).
    auto& temp_t_tex = rview.tex_get_or_emplace("SSR_HISTORY_T_TEMP");
    if(gfx::needs_recreate(temp_t_tex, target_size, gfx::texture_format::R16F))
    {
        temp_t_tex.reset();
        temp_t_tex = std::make_shared<gfx::texture>(target_size.width,
                                                    target_size.height,
                                                    false,
                                                    1,
                                                    gfx::texture_format::R16F,
                                                    BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                        BGFX_SAMPLER_V_CLAMP);
    }

    auto& temp_fbo = rview.fbo_get_or_emplace("SSR_HISTORY_TEMP");
    if(gfx::needs_recreate(temp_fbo, target_size))
    {
        temp_fbo.reset();
        temp_fbo = std::make_shared<gfx::frame_buffer>();
        temp_fbo->populate({temp_tex, temp_t_tex});
    }

    return temp_fbo;
}

auto ssr_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    // Ensure we have valid input
    if(!params.g_buffer)
    {
        return nullptr;
    }

    // Dispatch to appropriate implementation based on settings
    return run_fidelityfx(rview, params);
}

auto ssr_pass::run_fidelityfx(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    // Use the new three-pass pipeline by default
    return run_fidelityfx_three_pass(rview, params);
}


auto ssr_pass::generate_blurred_color_buffer(gfx::render_view& rview,
                                             const gfx::texture::ptr& input_color,
                                             const gfx::frame_buffer::ptr& g_buffer,
                                             const fidelityfx_ssr_settings& settings) -> gfx::texture::ptr
{
    APP_SCOPE_PERF("Rendering/SSR/Blur Color Pass");
    // Early validation
    if(!input_color)
    {
        return nullptr;
    }

    if(!blur_compute_program_.program || !blur_compute_program_.program->is_valid())
    {
        return input_color; // Fallback to input texture
    }

    auto input_size = input_color->get_size();

    // Get or create blurred color texture with mip chain
    auto& blurred_tex = rview.tex_get_or_emplace("SSR_BLURRED_COLOR");
    if(gfx::needs_recreate(blurred_tex, input_size))
    {
        blurred_tex.reset();
        blurred_tex = std::make_shared<gfx::texture>(input_size.width,
                                                     input_size.height,
                                                     true,                       // has mips
                                                     1,                          // num layers
                                                     gfx::texture_format::RGBA16F,
                                                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                                         BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_RT);
    }

    const uint32_t num_mips = settings.cone_tracing.max_mip_level + 1;
    gfx::render_pass pass("Blur Compute Pass");

    // Process each mip level using unified blur shader
    for(int mip = 0; mip < num_mips; ++mip)
    {
        // Calculate mip size
        int mip_width = (input_size.width >> mip) > 1 ? (input_size.width >> mip) : 1;
        int mip_height = (input_size.height >> mip) > 1 ? (input_size.height >> mip) : 1;

        // Calculate sigma based on mip level and base sigma
        float sigma = settings.cone_tracing.blur_base_sigma; // * (1.0f + float(mip));

        // Use unified blur compute shader
        blur_compute_program_.program->begin();

        // Set blur parameters: mip_level, sigma, base_width, base_height
        if(mip == 0)
        {
            // Bind input color texture as read-only image
            gfx::set_image(1, input_color->native_handle(), 0, bgfx::Access::Read);
        }
        else
        {
            // Bind previous mip level as input read-only image
            gfx::set_image(1, blurred_tex->native_handle(), mip - 1, bgfx::Access::Read);
        }

        float blur_params[4] = {float(mip), sigma, 0.0f, 0.0f};
        gfx::set_uniform(blur_compute_program_.u_blur_params, blur_params);

        // Bind output image (current mip level of blurred texture)
        gfx::set_image(0, blurred_tex->native_handle(), mip, bgfx::Access::Write);

        gfx::set_texture(blur_compute_program_.s_normal, 2, g_buffer->get_texture(1));

        // Dispatch compute shader
        uint32_t num_groups_x = (mip_width + 7) / 8;
        uint32_t num_groups_y = (mip_height + 7) / 8;
        gfx::dispatch(pass.id, blur_compute_program_.program->native_handle(), num_groups_x, num_groups_y, 1);

        blur_compute_program_.program->end();
    }

    return blurred_tex;
}

auto ssr_pass::create_or_update_ssr_denoise_fb(gfx::render_view& rview,
                                               const std::string& name,
                                               const gfx::frame_buffer::ptr& reference,
                                               trace_resolution res) -> gfx::frame_buffer::ptr
{
    const auto target_size = compute_trace_size(reference->get_size(), res);

    auto& denoised_tex = rview.tex_get_or_emplace(name);
    if(gfx::needs_recreate(denoised_tex, target_size))
    {
        denoised_tex.reset();
        denoised_tex = std::make_shared<gfx::texture>(target_size.width,
                                                      target_size.height,
                                                      false,
                                                      1,
                                                      gfx::texture_format::RGBA16F,
                                                      BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_RT |
                                                          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }

    auto& denoised_fbo = rview.fbo_get_or_emplace(name);
    if(gfx::needs_recreate(denoised_fbo, target_size))
    {
        denoised_fbo.reset();
        denoised_fbo = std::make_shared<gfx::frame_buffer>();
        denoised_fbo->populate({denoised_tex});
    }

    return denoised_fbo;
}

auto ssr_pass::run_spatial_denoise(gfx::render_view& rview,
                                   const gfx::frame_buffer::ptr& ssr_curr,
                                   const gfx::frame_buffer::ptr& g_buffer,
                                   const fidelityfx_ssr_settings& settings) -> gfx::frame_buffer::ptr
{
    if(!spatial_denoise_compute_program_.is_valid())
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
    const uint32_t gx = (sz.width + 7) / 8;
    const uint32_t gy = (sz.height + 7) / 8;

    auto src_tex = ssr_curr->get_texture();
    gfx::frame_buffer::ptr dst_fb = fb_a;

    for(int i = 0; i < num_passes; ++i)
    {
        gfx::render_pass pass(fmt::format("Spatial Denoise Pass {}", i).c_str());

        spatial_denoise_compute_program_.program->begin();

        gfx::set_texture(spatial_denoise_compute_program_.s_ssr_input, 0, src_tex);
        gfx::set_image(1, dst_fb->get_texture()->native_handle(), 0, bgfx::Access::Write);
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

auto ssr_pass::run_fidelityfx_three_pass(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
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
    if(!ssr_history_fb)
    {
        gfx::render_pass::pop_scope();
        return ssr_curr_fb; // Fallback to current frame
    }

    // Pass 3: Composite - blends SSR_HIST + SSR_CURR + probe, writes to output
    auto composite_fb =
        run_composite(rview, ssr_history_fb, ssr_curr_fb, params.output, params.g_buffer, params.output);
    gfx::render_pass::pop_scope();
    return composite_fb;
}

auto ssr_pass::run_ssr_trace(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    // SSR caps at half resolution: sub-half breaks Hi-Z, temporal clamp and the denoiser.
    const auto trace_res = params.settings.fidelityfx.resolution;
    auto ssr_curr_fbo = create_or_update_ssr_curr_fb(rview, params.g_buffer, trace_res);

    // Generate blurred color buffer for cone tracing if enabled
    gfx::texture::ptr blurred_color_buffer = nullptr;
    if(params.settings.fidelityfx.enable_cone_tracing && params.previous_frame)
    {
        blurred_color_buffer =
            generate_blurred_color_buffer(rview, params.previous_frame, params.g_buffer, params.settings.fidelityfx);
    }
    else
    {
        rview.tex_remove("SSR_BLURRED_COLOR");
    }

    // ============================================================================
    // SSR Trace Pass
    // ============================================================================
    APP_SCOPE_PERF("Rendering/SSR/Trace Pass");

    gfx::render_pass pass("Trace Pass");
    pass.bind(ssr_curr_fbo.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());

    // Bind SSR trace program
    fidelityfx_pixel_program_.program->begin();

    // Set input textures
    gfx::set_texture(fidelityfx_pixel_program_.s_color, 0, params.previous_frame);
    gfx::set_texture(fidelityfx_pixel_program_.s_normal, 1, params.g_buffer->get_texture(1));
    gfx::set_texture(fidelityfx_pixel_program_.s_depth, 2, params.g_buffer->get_texture(4));
    gfx::set_texture(fidelityfx_pixel_program_.s_hiz, 3, params.hiz_buffer);

    // Set blurred color buffer for cone tracing (fallback to previous frame if not available)
    auto cone_tracing_texture = blurred_color_buffer ? blurred_color_buffer : params.previous_frame;
    gfx::set_texture(fidelityfx_pixel_program_.s_color_blurred, 4, cone_tracing_texture);

    // Set SSR parameters (max_steps, depth_tolerance, max_rays, brightness)
    float ssr_params[4] = {float(params.settings.fidelityfx.max_steps),
                           params.settings.fidelityfx.depth_tolerance,
                           float(params.settings.fidelityfx.max_rays),
                           params.settings.fidelityfx.brightness};
    gfx::set_uniform(fidelityfx_pixel_program_.u_ssr_params, ssr_params);

            
    // Resolution scale MUST be per-axis. Computing a single scalar (e.g. full_w / half_w)
    // and applying it to both axes silently breaks any case where the X and Y ratios
    // disagree, which is exactly what happens at odd full-res W with even full-res H:
    // e.g. (1233, 900) -> half (616, 450) gives X=2.00162 but Y=2.0. Reusing the X scale
    // on the Y axis shifts the bottom half-res row's gbuffer fetch ~0.7 full-res pixels
    // off, producing a garbage out-of-frustum ray origin and a visible noise band along
    // the bottom of the viewport at odd widths.
    auto ssr_size = ssr_curr_fbo->get_size();
    auto g_buffer_size = params.g_buffer->get_size();
    const float ssr_scale_x = float(g_buffer_size.width) / float(ssr_size.width);
    const float ssr_scale_y = float(g_buffer_size.height) / float(ssr_size.height);
    // u_hiz_params layout: (hiz_width, hiz_height, scale_x, scale_y).
    // num_mips was previously stored in .z but is unused by the shader.
    float hiz_params[4] = {0.0f, 0.0f, ssr_scale_x, ssr_scale_y};
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

    // Set cone tracing parameters (cone_angle_bias, max_mip_level, frame_number, enable_cone_tracing)
    float cone_params[4] = {
        params.settings.fidelityfx.cone_tracing.cone_angle_bias,
        float(params.settings.fidelityfx.cone_tracing.max_mip_level),
        float(gfx::get_render_frame() % 4),                                 // frame number for temporal jitter
        float(params.settings.fidelityfx.enable_cone_tracing ? 1.0f : 0.0f) // enable flag
    };
    gfx::set_uniform(fidelityfx_pixel_program_.u_cone_params, cone_params);

    // The TAA-unjittered previous pair, never get_prev_view_projection(): the jittered prev
    // misaligns a still camera's reprojection by the jitter delta every frame (the GI
    // reflection chain's measured lesson; every temporal consumer now shares this chain).
    auto prev_view_proj = params.cam->get_prev_view_projection_unjittered();
    gfx::set_uniform(fidelityfx_pixel_program_.u_prev_view_proj, prev_view_proj.get_matrix());

    uint64_t topology = gfx::clip_fullscreen_triangle(1.0f);
    if(topology == 0)
    {
        topology = gfx::clip_quad(1.0f);
    }
    gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, fidelityfx_pixel_program_.program->native_handle());

    // Reset state
    gfx::set_state(BGFX_STATE_DEFAULT);
    fidelityfx_pixel_program_.program->end();
    gfx::discard();

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
    if(!temporal_resolve_program_.is_valid())
    {
        return nullptr;
    }

    // History buffers match ssr_curr's size exactly; ssr_curr already carries the trace
    // resolution, so we ask the helpers not to downscale any further.
    auto old_history = rview.tex_safe_get("SSR_HISTORY");
    auto history_tex = create_or_update_ssr_history_tex(rview, ssr_curr, trace_resolution::full);
    auto history_t_tex = rview.tex_get("SSR_HISTORY_T");
    auto temp_fbo = create_or_update_ssr_history_temp_fb(rview, ssr_curr, trace_resolution::full);

    // History was just allocated -- RGBA16F contains undefined data (possibly NaN).
    // Seed it with the current frame and skip temporal this frame.
    if(history_tex != old_history)
    {
        gfx::render_pass blit_pass("History Init Blit Pass");
        gfx::blit(blit_pass.id, history_tex->native_handle(), 0, 0, ssr_curr->get_texture()->native_handle(), 0, 0);
        gfx::blit(blit_pass.id, history_t_tex->native_handle(), 0, 0, curr_hit_t->native_handle(), 0, 0);
        return nullptr;
    }

    // ============================================================================
    // Temporal Resolve Pass
    // ============================================================================
    APP_SCOPE_PERF("Rendering/SSR/Temporal Resolve Pass");

    gfx::render_pass pass("Temporal Resolve Pass");
    pass.bind(temp_fbo.get());
    pass.set_view_proj(cam->get_view(), cam->get_projection());

    // Bind temporal resolve program
    temporal_resolve_program_.program->begin();

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

    // Set temporal parameters (enable_temporal, history_strength, depth_threshold, roughness_sensitivity)
    float temporal_params[4] = {settings.enable_temporal_accumulation ? 1.0f : 0.0f,
                                settings.temporal.history_strength,
                                settings.temporal.depth_threshold,
                                settings.temporal.roughness_sensitivity};
    gfx::set_uniform(temporal_resolve_program_.u_temporal_params, temporal_params);

    // Set motion parameters (motion_scale_pixels, normal_dot_threshold, max_accum_frames, velocity flag)
    float motion_params[4] = {
        settings.temporal.motion_scale_pixels,
        settings.temporal.normal_dot_threshold,
        float(settings.temporal.max_accum_frames),
        use_velocity ? 1.0f : 0.0f
    };
    gfx::set_uniform(temporal_resolve_program_.u_motion_params, motion_params);

    // Per-axis scale; see ssr trace pass for why scalar scale is wrong at odd full-res W.
    auto history_size = history_tex->get_size();
    auto g_buffer_size = g_buffer->get_size();
    const float ssr_scale_x = float(g_buffer_size.width) / float(history_size.width);
    const float ssr_scale_y = float(g_buffer_size.height) / float(history_size.height);

    // x = the CONTENT-LAG release ceiling: the trace samples PREV_SCENE_HDR, so a moving
    // emitter's radiance sweeps every glossy surface one frame late while geometry at the
    // hit stays static and t-confirmed - invisible to the per-pixel guards. While any
    // mover was drawn within one accumulation window the release is capped screen-wide:
    // a shallow window is the CORRECT window for radiance that changes every frame.
    // y unused; zw = the resolution scale.
    const float release_ceiling = velocity_movers_recent ? 0.125f : 1.0f;
    float fade_params[4] = {release_ceiling, 0.0f, ssr_scale_x, ssr_scale_y};
    gfx::set_uniform(temporal_resolve_program_.u_fade_params, fade_params);

    // The TAA-unjittered previous pair (see the trace-side comment); keeps the legacy
    // camera-pixel path jitter-aligned with the velocity buffer's convention.
    auto prev_view_proj = cam->get_prev_view_projection_unjittered();
    gfx::set_uniform(temporal_resolve_program_.u_prev_view_proj, prev_view_proj.get_matrix());

    // Draw fullscreen quad
    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, temporal_resolve_program_.program->native_handle());

    // Reset state
    gfx::set_state(BGFX_STATE_DEFAULT);
    temporal_resolve_program_.program->end();
    gfx::discard();

    // ============================================================================
    // Blit temp_fbo texture into persistent history_tex for next frame
    // ============================================================================
    gfx::render_pass blit_pass("History Blit Pass");
    gfx::blit(blit_pass.id, history_tex->native_handle(), 0, 0, temp_fbo->get_texture()->native_handle(), 0, 0);
    gfx::blit(blit_pass.id, history_t_tex->native_handle(), 0, 0, temp_fbo->get_texture(1)->native_handle(), 0, 0);

    return temp_fbo;
}

auto ssr_pass::run_composite(gfx::render_view& rview,
                             const gfx::frame_buffer::ptr& ssr_history,
                             const gfx::frame_buffer::ptr& ssr_curr,
                             const gfx::frame_buffer::ptr& probe_buffer,
                             const gfx::frame_buffer::ptr& g_buffer,
                             const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr
{
    if(!composite_program_.is_valid())
    {
        return nullptr;
    }

    // Get or create output framebuffer using render_view
    auto actual_output = create_or_update_output_fb(rview, g_buffer, output);

    // ============================================================================
    // Composite Pass
    // ============================================================================
    APP_SCOPE_PERF("Rendering/SSR/Composite Pass");

    gfx::render_pass pass("Composite Pass");
    pass.bind(actual_output.get());

    // Bind composite program
    composite_program_.program->begin();

    // Set input textures
    gfx::set_texture(composite_program_.s_ssr_history, 0, ssr_history->get_texture());
    gfx::set_texture(composite_program_.s_ssr_curr, 1, ssr_curr->get_texture());
    gfx::set_texture(composite_program_.s_normal, 2, g_buffer->get_texture(1));
    gfx::set_texture(composite_program_.s_depth, 3, g_buffer->get_texture(4));

    // Draw fullscreen quad with alpha blending
    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    gfx::submit(pass.id, composite_program_.program->native_handle());

    // Reset state
    gfx::set_state(BGFX_STATE_DEFAULT);
    composite_program_.program->end();
    gfx::discard();

    return actual_output;
}

void ssr_pass::release_resources(gfx::render_view& rview)
{
    rview.fbo_remove("SSR_OUTPUT");
    rview.tex_remove("SSR_OUTPUT");
    rview.fbo_remove("SSR_CURR");
    rview.tex_remove("SSR_CURR");
    rview.tex_remove("SSR_HISTORY");
    rview.fbo_remove("SSR_HISTORY_TEMP");
    rview.tex_remove("SSR_HISTORY_TEMP");
    rview.tex_remove("SSR_BLURRED_COLOR");
    rview.fbo_remove("SSR_DENOISED_A");
    rview.tex_remove("SSR_DENOISED_A");
    rview.fbo_remove("SSR_DENOISED_B");
    rview.tex_remove("SSR_DENOISED_B");
}

} // namespace unravel
