#include "ssr_pass.h"
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

    // Create FidelityFX SSR programs
    fidelityfx_pixel_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssr_fidelityfx);
    fidelityfx_pixel_program_.cache_uniforms();

    // Create temporal resolve program
    temporal_resolve_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssr_temporal_resolve);
    temporal_resolve_program_.cache_uniforms();

    // Create composite program
    composite_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_ssr_composite);
    composite_program_.cache_uniforms();

    // Create unified blur compute program for cone tracing
    blur_compute_program_.program = std::make_unique<gpu_program>(cs_ssr_blur);
    blur_compute_program_.cache_uniforms();

    // Validate all programs
    bool all_valid = fidelityfx_pixel_program_.is_valid() && temporal_resolve_program_.is_valid() &&
                     composite_program_.is_valid() && blur_compute_program_.program &&
                     blur_compute_program_.program->is_valid();

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
    auto ref_tex = reference->get_texture();
    auto ref_format = ref_tex->info.format;

    auto& ssr_output_tex = rview.tex_get_or_emplace("SSR_OUTPUT");
    if(!ssr_output_tex || (ssr_output_tex && ssr_output_tex->get_size() != ref_sz) ||
       (ssr_output_tex && ssr_output_tex->info.format != ref_format))
    {
        ssr_output_tex = std::make_shared<gfx::texture>(ref_sz.width,
                                                        ref_sz.height,
                                                        false,          // no generate mips
                                                        1,              // one layer
                                                        ref_format,     // same format as reference
                                                        BGFX_TEXTURE_RT // render target flag
        );
    }

    auto& ssr_output_fbo = rview.fbo_get_or_emplace("SSR_OUTPUT");
    if(!ssr_output_fbo || (ssr_output_fbo && ssr_output_fbo->get_size() != ref_sz))
    {
        ssr_output_fbo = std::make_shared<gfx::frame_buffer>();
        ssr_output_fbo->populate({ssr_output_tex});
    }

    return ssr_output_fbo;
}

auto ssr_pass::create_or_update_ssr_curr_fb(gfx::render_view& rview, 
                                            const gfx::frame_buffer::ptr& reference, 
                                            bool enable_half_res) -> gfx::frame_buffer::ptr
{
    auto ref_sz = reference->get_size();
    auto ref_tex = reference->get_texture();
    auto ref_format = ref_tex->info.format;

    // Calculate target size with multiplier
    uint32_t target_width = static_cast<uint32_t>(ref_sz.width * (enable_half_res ? 0.5f : 1.0f));
    uint32_t target_height = static_cast<uint32_t>(ref_sz.height * (enable_half_res ? 0.5f : 1.0f));
    
    // Ensure minimum size of 1x1
    target_width = target_width > 0 ? target_width : 1;
    target_height = target_height > 0 ? target_height : 1;

    auto& ssr_curr_tex = rview.tex_get_or_emplace("SSR_CURR");
    if(!ssr_curr_tex || 
       (ssr_curr_tex && (ssr_curr_tex->info.width != target_width || ssr_curr_tex->info.height != target_height)) ||
       (ssr_curr_tex && ssr_curr_tex->info.format != ref_format))
    {
        ssr_curr_tex = std::make_shared<gfx::texture>(target_width,
                                                      target_height,
                                                      false,          // no generate mips
                                                      1,              // one layer
                                                      ref_format,     // same format as reference
                                                      BGFX_TEXTURE_RT // render target flag
        );
    }

    auto& ssr_curr_fbo = rview.fbo_get_or_emplace("SSR_CURR");
    usize32_t target_size{target_width, target_height};
    if(!ssr_curr_fbo || (ssr_curr_fbo && ssr_curr_fbo->get_size() != target_size))
    {
        ssr_curr_fbo = std::make_shared<gfx::frame_buffer>();
        ssr_curr_fbo->populate({ssr_curr_tex});
    }

    return ssr_curr_fbo;
}

auto ssr_pass::create_or_update_ssr_history_tex(gfx::render_view& rview, 
                                                const gfx::frame_buffer::ptr& reference, 
                                                bool enable_half_res) -> gfx::texture::ptr
{
    auto ref_sz = reference->get_size();
    auto ref_tex = reference->get_texture();
    auto ref_format = ref_tex->info.format;

    // Calculate target size with multiplier
    uint32_t target_width = static_cast<uint32_t>(ref_sz.width * (enable_half_res ? 0.5f : 1.0f));
    uint32_t target_height = static_cast<uint32_t>(ref_sz.height * (enable_half_res ? 0.5f : 1.0f));
    
    // Ensure minimum size of 1x1
    target_width = target_width > 0 ? target_width : 1;
    target_height = target_height > 0 ? target_height : 1;

    auto& history_tex = rview.tex_get_or_emplace("SSR_HISTORY");
    if(!history_tex || 
       (history_tex && (history_tex->info.width != target_width || history_tex->info.height != target_height)) ||
       (history_tex && history_tex->info.format != ref_format))
    {
        history_tex = std::make_shared<gfx::texture>(target_width,
                                                     target_height,
                                                     false,      // no generate mips
                                                     1,          // one layer
                                                     ref_format, // same format as reference
                                                     BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                         BGFX_SAMPLER_V_CLAMP);
    }

    return history_tex;
}

auto ssr_pass::create_or_update_ssr_history_temp_fb(gfx::render_view& rview, 
                                                    const gfx::frame_buffer::ptr& reference, 
                                                    bool enable_half_res) -> gfx::frame_buffer::ptr
{
    auto ref_sz = reference->get_size();
    auto ref_tex = reference->get_texture();
    auto ref_format = ref_tex->info.format;

    // Calculate target size with multiplier
    uint32_t target_width = static_cast<uint32_t>(ref_sz.width * (enable_half_res ? 0.5f : 1.0f));
    uint32_t target_height = static_cast<uint32_t>(ref_sz.height * (enable_half_res ? 0.5f : 1.0f));
    
    // Ensure minimum size of 1x1
    target_width = target_width > 0 ? target_width : 1;
    target_height = target_height > 0 ? target_height : 1;

    auto& temp_tex = rview.tex_get_or_emplace("SSR_HISTORY_TEMP");
    if(!temp_tex || 
       (temp_tex && (temp_tex->info.width != target_width || temp_tex->info.height != target_height)) ||
       (temp_tex && temp_tex->info.format != ref_format))
    {
        temp_tex = std::make_shared<gfx::texture>(target_width,
                                                  target_height,
                                                  false,      // no generate mips
                                                  1,          // one layer
                                                  ref_format, // same format as reference
                                                  BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                      BGFX_SAMPLER_V_CLAMP);
    }

    auto& temp_fbo = rview.fbo_get_or_emplace("SSR_HISTORY_TEMP");
    usize32_t target_size{target_width, target_height};
    if(!temp_fbo || (temp_fbo && temp_fbo->get_size() != target_size))
    {
        temp_fbo = std::make_shared<gfx::frame_buffer>();
        temp_fbo->populate({temp_tex});
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
    if(!blurred_tex || blurred_tex->get_size() != input_size)
    {
        blurred_tex = std::make_shared<gfx::texture>(input_size.width,
                                                     input_size.height,
                                                     true,                       // has mips
                                                     1,                          // num layers
                                                     gfx::texture_format::RGBA8, // format for HDR content
                                                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                                         BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_RT);
    }

    const uint32_t num_mips = settings.cone_tracing.max_mip_level + 1;
    gfx::render_pass pass("blur_compute_ssr_pass");

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

auto ssr_pass::run_fidelityfx_three_pass(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    // Pass 1: SSR Trace - generates SSR_CURR
    auto ssr_curr_fb = run_ssr_trace(rview, params);
    if(!ssr_curr_fb)
    {
        return nullptr;
    }

    // Pass 2: Temporal Resolve - reads SSR_CURR + SSR_HIST, writes new SSR_HIST
    auto ssr_history_fb =
        run_temporal_resolve(rview, ssr_curr_fb, params.g_buffer, params.cam, params.settings.fidelityfx);
    if(!ssr_history_fb)
    {
        return ssr_curr_fb; // Fallback to current frame
    }

    // Pass 3: Composite - blends SSR_HIST + SSR_CURR + probe, writes to output
    auto composite_fb =
        run_composite(rview, ssr_history_fb, ssr_curr_fb, params.output, params.g_buffer, params.output);

    return composite_fb;
}

auto ssr_pass::run_ssr_trace(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    // Get or create SSR current frame buffer using helper function (1.0f = full resolution)
    auto ssr_curr_fbo = create_or_update_ssr_curr_fb(rview, params.g_buffer, params.settings.fidelityfx.enable_half_res);

    // Generate blurred color buffer for cone tracing if enabled
    gfx::texture::ptr blurred_color_buffer = nullptr;
    if(params.settings.fidelityfx.enable_cone_tracing && params.previous_frame)
    {
        blurred_color_buffer =
            generate_blurred_color_buffer(rview, params.previous_frame, params.g_buffer, params.settings.fidelityfx);
    }

    // ============================================================================
    // SSR Trace Pass
    // ============================================================================
    APP_SCOPE_PERF("Rendering/SSR/Trace Pass");

    gfx::render_pass pass("ssr_trace_pass");
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

            
    // Calculate resolution scale: SSR buffer size / full resolution size
    auto ssr_size = ssr_curr_fbo->get_size();
    auto g_buffer_size = params.g_buffer->get_size();
    float ssr_resolution_scale = float(g_buffer_size.width) / float(ssr_size.width); // resolution scale factor
    // Set Hi-Z parameters (buffer_width, buffer_height, num_depth_mips, ssr_resolution_scale)
    float hiz_params[4] = {
        0.0f,
        0.0f,
        0.0f,
        ssr_resolution_scale // SSR resolution scale (1.0 = full res, 0.5 = half res, etc.)
    };
    if(params.hiz_buffer)
    {
        hiz_params[0] = float(params.hiz_buffer->info.width);
        hiz_params[1] = float(params.hiz_buffer->info.height);
        hiz_params[2] = float(params.hiz_buffer->info.numMips); // Number of mips

    }
    gfx::set_uniform(fidelityfx_pixel_program_.u_hiz_params, hiz_params);

    // Set fade parameters (fade_in_start, fade_in_end, roughness_depth_tolerance, facing_reflections_fading)
    float fade_params[4] = {params.settings.fidelityfx.fade_in_start,
                            params.settings.fidelityfx.fade_in_end,
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

    // Set previous frame view-projection matrix for temporal reprojection
    auto prev_view_proj = params.cam->get_prev_view_projection();
    gfx::set_uniform(fidelityfx_pixel_program_.u_prev_view_proj, prev_view_proj.get_matrix());

    // Draw fullscreen quad
    auto topology = gfx::clip_quad(1.0f);
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
                                    const gfx::frame_buffer::ptr& g_buffer,
                                    const camera* cam,
                                    const fidelityfx_ssr_settings& settings) -> gfx::frame_buffer::ptr
{
    if(!temporal_resolve_program_.is_valid())
    {
        return nullptr;
    }

    // Create or update SSR history texture and temp framebuffer using helper functions (1.0f = full resolution)
    auto history_tex = create_or_update_ssr_history_tex(rview, ssr_curr, settings.enable_half_res);
    auto temp_fbo = create_or_update_ssr_history_temp_fb(rview, ssr_curr, settings.enable_half_res);

    // ============================================================================
    // Temporal Resolve Pass
    // ============================================================================
    APP_SCOPE_PERF("Rendering/SSR/Temporal Resolve Pass");

    gfx::render_pass pass("ssr_temporal_resolve_pass");
    pass.bind(temp_fbo.get());
    pass.set_view_proj(cam->get_view(), cam->get_projection());

    // Bind temporal resolve program
    temporal_resolve_program_.program->begin();

    // Set input textures
    gfx::set_texture(temporal_resolve_program_.s_ssr_curr, 0, ssr_curr->get_texture());
    gfx::set_texture(temporal_resolve_program_.s_ssr_history, 1, history_tex);
    gfx::set_texture(temporal_resolve_program_.s_normal, 2, g_buffer->get_texture(1));
    gfx::set_texture(temporal_resolve_program_.s_depth, 3, g_buffer->get_texture(4));

    // Set temporal parameters (enable_temporal, history_strength, depth_threshold, roughness_sensitivity)
    float temporal_params[4] = {settings.enable_temporal_accumulation ? 1.0f : 0.0f,
                                settings.temporal.history_strength,
                                settings.temporal.depth_threshold,
                                settings.temporal.roughness_sensitivity};
    gfx::set_uniform(temporal_resolve_program_.u_temporal_params, temporal_params);

    // Set motion parameters (motion_scale_pixels, normal_dot_threshold, max_accum_frames, unused)
    float motion_params[4] = {
        settings.temporal.motion_scale_pixels,
        settings.temporal.normal_dot_threshold,
        float(settings.temporal.max_accum_frames),
        0.0f // unused
    };
    gfx::set_uniform(temporal_resolve_program_.u_motion_params, motion_params);

    // Set fade parameters (fade_in_start, fade_in_end, ssr_resolution_scale, unused)
    auto history_size = history_tex->get_size();
    auto g_buffer_size = g_buffer->get_size();
    float ssr_resolution_scale = float(g_buffer_size.width) / float(history_size.width);
    
    float fade_params[4] = {settings.fade_in_start, settings.fade_in_end, ssr_resolution_scale, 0.0f};
    gfx::set_uniform(temporal_resolve_program_.u_fade_params, fade_params);

    // Set previous frame view-projection matrix
    auto prev_view_proj = cam->get_prev_view_projection();
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
    gfx::render_pass blit_pass("ssr_history_blit_pass");
    gfx::blit(blit_pass.id, history_tex->native_handle(), 0, 0, temp_fbo->get_texture()->native_handle(), 0, 0);

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

    gfx::render_pass pass("ssr_composite_pass");
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

} // namespace unravel
