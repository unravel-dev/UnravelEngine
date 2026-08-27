#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <engine/rendering/camera.h>
#include <graphics/texture.h>
#include <graphics/render_pass.h>
#include "trace_resolution.h"

namespace unravel
{

class ssil_pass
{
public:
    /// Spatial denoise parameters (a-trous wavelet filter)
    struct spatial_denoise_settings
    {
        float depth_sigma = 0.02f;
        float normal_power = 64.0f;
        /// Variance-guided luminance edge-stop multiplier (phi). Multiplies the measured
        /// luminance std-dev: lower keeps more intra-surface indirect detail (colour-bleed
        /// gradients, contact darkening) that the depth/normal stops cannot see; higher
        /// blurs flatter. Kept low because the shader floors the sigma to a tiny constant
        /// (LUMA_SIGMA_EPS) on converged pixels, so a large phi here would disable the
        /// luminance stop entirely.
        float luma_sigma = 4.0f;

        int passes = 4;
        /// Mixed-resolution split: the first `full_res_passes` a-trous passes run at the trace
        /// resolution; the REMAINING passes run at half that resolution, where their wide
        /// dilation is cache-coherent and ~4x cheaper. The bilateral upsample reconstructs
        /// sharp silhouettes from the full-res G-buffer regardless of where the blur ran.
        ///
        /// Default 0 lets the pass choose the cheapest split. When temporal moments are
        /// available, it is automatically promoted to one trace-resolution pass so the
        /// stable variance is consumed before the wide half-res tier.
        int full_res_passes = 0;
        /// Upper bound on the a-trous dilation (in texels, within each tier). The step normally
        /// doubles each pass (1,2,4,8,...); capping it turns the widest passes into same-cost
        /// repeated-step passes (which also fill holes better) at a small loss of filter reach.
        int max_step = 16;
    };

    /// Temporal accumulation parameters
    struct temporal_settings
    {
        float history_strength = 0.9f;
        /// Relative view-space depth tolerance for temporal disocclusion (fraction of
        /// linear depth). The shader rejects history when |expected - stored| / stored
        /// exceeds this, so it is scale-invariant (same value works near and far).
        float depth_threshold = 0.2f;
        /// Minimum dot(n_current, n_at_reprojected_uv) to accept history. Catches the
        /// common disocclusion case where reprojection lands on a different surface
        /// (rotated geometry, animated foliage) which the depth-only test misses.
        ///
        /// MIDPOINT of the soft-fall-off band in the temporal shader, not a hard cutoff
        /// (the shader applies a smoothstep with +/-0.1 width). 0.6 ~ 53 deg midpoint
        /// gives full history weight at <=45 deg normal mismatch (covers curved-surface
        /// reprojection drift) and zero weight at >60 deg (clearly-different surface).
        /// Higher values are stricter (more rejections, more edge speckle on motion);
        /// lower values are more lenient (occasional ghosting on rotated geometry).
        float normal_dot_threshold = 0.6f;
        int max_accum_frames = 16;
    };

    /// SSIL settings
    struct ssil_settings
    {
        int max_rays = 4;
        int max_steps = 64;
        float depth_tolerance = 0.15f;
        /// Extra view-space hit-acceptance band ("thickness"), added on top of
        /// depth_tolerance and scaled by hit distance. Far hits resolve against coarser
        /// Hi-Z depth, so a fixed band over-rejects them and leaks the environment fallback
        /// through occluders; raising thickness closes that leak (too high over-occludes).
        float thickness = 0.5f;
        /// Gain on the on-screen indirect BOUNCE only (the environment-miss fallback stays
        /// at probe intensity). 1.0 is the physically-matched single-bounce strength, but
        /// that reads strong with saturated albedos (e.g. a red floor bleeding onto walls),
        /// so the default is a tamer 0.5; raise toward 1.0 for full physical colour bleed.
        float brightness = 0.5f;
        float max_distance = 300.0f;
        /// Trace resolution divisor. SSIL tolerates `quarter` well because indirect
        /// lighting is low-frequency and the bilateral upsample uses full-res guides.
        trace_resolution resolution = trace_resolution::full;

        bool enable_multi_bounce = true;
        float multi_bounce_intensity = 0.8f;

        bool enable_spatial_denoise = true;
        spatial_denoise_settings spatial_denoise;

        bool enable_temporal_accumulation = true;
        temporal_settings temporal;
    };

    struct run_params
    {
        gfx::frame_buffer::ptr g_buffer;
        gfx::texture::ptr hiz_buffer;
        gfx::texture::ptr direct_lighting;
        gfx::texture::ptr prev_depth;
        gfx::texture::ptr prev_ssil;
        /// This frame's velocity buffer, passed explicitly by the pipeline. A valid
        /// texture IS the enable; null = legacy matrix reprojection.
        gfx::texture::ptr velocity;
        /// Environment SH coefficients (9x3 R32F); sampled per ray as the miss fallback so
        /// the trace integrates the environment where it escapes on-screen geometry. May be
        /// null on the first frame, in which case the fallback is disabled (misses = 0).
        gfx::texture::ptr irradiance_sh;
        const camera* cam{};
        ssil_settings settings;
    };

    auto init(rtti::context& ctx) -> bool;

    /// Executes the full SSIL pipeline (trace -> temporal -> denoise -> upsample).
    /// Returns the SSIL output texture: RGB = full hemispherical indirect diffuse in
    /// radiance-mean units (on-screen bounce where rays hit, environment SH where they
    /// miss); the consumer multiplies by PI to reach eval_irradiance_sh's irradiance units.
    /// A = SSIL blend weight: trace coverage without temporal, accumulated screen-hit
    /// evidence when temporal is enabled (mix(irradiance, ssil.rgb*PI, a)).
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr;

    /// Releases all GPU resources owned by this pass from the render_view.
    void release_resources(gfx::render_view& rview);

private:
    auto run_trace(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

    auto run_spatial_denoise(gfx::render_view& rview,
                             const gfx::frame_buffer::ptr& ssil_curr,
                             const gfx::frame_buffer::ptr& g_buffer,
                             const gfx::texture::ptr& moments,
                             const camera* cam,
                             const ssil_settings& settings) -> gfx::frame_buffer::ptr;

    /// Returns true when the temporal pass produced valid luminance moments this frame
    /// (i.e. the resolve shader ran). False on the first / disoccluded frame where the
    /// history is merely seeded -- the denoiser then uses its spatial variance estimate.
    /// Outputs:
    ///   `out_result_fb` -- the framebuffer holding this frame's temporal colour result
    ///                      (alias of the ping-pong WRITE target).
    ///   `out_moments_tex` -- the moments texture to feed the denoiser. Only meaningful
    ///                        when the return value is true.
    auto run_temporal_resolve(gfx::render_view& rview,
                              gfx::frame_buffer::ptr ssil_input,
                              const gfx::frame_buffer::ptr& g_buffer,
                              const gfx::texture::ptr& prev_depth,
                              const gfx::texture::ptr& velocity,
                              const camera* cam,
                              const ssil_settings& settings,
                              gfx::frame_buffer::ptr& out_result_fb,
                              gfx::texture::ptr& out_moments_tex) -> bool;

    /// Joint-bilateral upsample of a reduced-resolution SSIL buffer to the full
    /// G-buffer resolution. Only invoked when the trace runs below full res.
    auto run_upsample(gfx::render_view& rview,
                      const gfx::frame_buffer::ptr& ssil_input,
                      const gfx::frame_buffer::ptr& g_buffer,
                      const camera* cam,
                      const ssil_settings& settings) -> gfx::frame_buffer::ptr;

    auto create_or_update_ssil_fb(gfx::render_view& rview,
                                  const std::string& name,
                                  const gfx::frame_buffer::ptr& reference,
                                  trace_resolution res,
                                  uint64_t extra_flags = 0) -> gfx::frame_buffer::ptr;

    auto create_or_update_ssil_tex(gfx::render_view& rview,
                                   const std::string& name,
                                   const gfx::frame_buffer::ptr& reference,
                                   trace_resolution res,
                                   uint64_t extra_flags = 0) -> gfx::texture::ptr;

    /// Creates / updates a two-attachment framebuffer (colour + luminance moments) used
    /// by the temporal resolve pass for spatiotemporal variance (SVGF).
    auto create_or_update_ssil_fb_mrt(gfx::render_view& rview,
                                      const std::string& fbo_name,
                                      const std::string& color_tex_name,
                                      const std::string& moments_tex_name,
                                      const gfx::frame_buffer::ptr& reference,
                                      trace_resolution res,
                                      uint64_t extra_flags = 0) -> gfx::frame_buffer::ptr;

    /// Releases the temporal-history ping-pong textures and framebuffers. Called when
    /// temporal accumulation is toggled off so the next enable starts from a clean init.
    void release_history_resources(gfx::render_view& rview);

    /// Releases the spatial-denoise scratch textures and framebuffers (both full-res and
    /// mixed half-res tiers, plus the variance ping-pong). Called when the denoiser is
    /// toggled off so the textures don't keep occupying VRAM.
    void release_denoise_resources(gfx::render_view& rview);

    // Trace program (fullscreen fragment shader)
    struct trace_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_ssil_params;
        gfx::program::uniform_ptr u_ssil_params2;
        /// x = distance-scaled hit-acceptance thickness (see ssil_settings::thickness).
        gfx::program::uniform_ptr u_ssil_params3;
        /// xy = G-buffer (full) width/height; z = 1 when tracing at half resolution (UV from gl_FragCoord).
        gfx::program::uniform_ptr u_ssil_resolution;
        gfx::program::uniform_ptr s_color;
        gfx::program::uniform_ptr s_normal;
        gfx::program::uniform_ptr s_hiz;
        gfx::program::uniform_ptr s_emissive;
        gfx::program::uniform_ptr s_albedo;
        gfx::program::uniform_ptr s_prev_ssil;
        gfx::program::uniform_ptr s_irradiance;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_ssil_params, "u_ssil_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_ssil_params2, "u_ssil_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_ssil_params3, "u_ssil_params3", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_ssil_resolution, "u_ssil_resolution", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_color, "s_color", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_hiz, "s_hiz", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_emissive, "s_emissive", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_albedo, "s_albedo", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_prev_ssil, "s_prev_ssil", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_irradiance, "s_irradiance", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool { return program && program->is_valid(); }
    } trace_program_;

    // Spatial denoise compute program
    struct denoise_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_denoise_params;
        gfx::program::uniform_ptr u_denoise_params2;
        gfx::program::uniform_ptr s_ssil_input;
        gfx::program::uniform_ptr s_normal;
        gfx::program::uniform_ptr s_depth;
        gfx::program::uniform_ptr s_ssil_moments;
        gfx::program::uniform_ptr s_ssil_variance;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_denoise_params, "u_denoise_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_denoise_params2, "u_denoise_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_ssil_input, "s_ssil_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssil_moments, "s_ssil_moments", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssil_variance, "s_ssil_variance", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool { return program && program->is_valid(); }
    } denoise_program_;

    // Geometry-aware 2x downsample compute program. Produces the half-resolution input the
    // wide a-trous passes run on (see cs_ssil_downsample.sc).
    struct downsample_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_downsample_params;
        gfx::program::uniform_ptr s_ssil_input;
        gfx::program::uniform_ptr s_normal;
        gfx::program::uniform_ptr s_depth;
        gfx::program::uniform_ptr s_ssil_variance;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_downsample_params, "u_downsample_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_ssil_input, "s_ssil_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssil_variance, "s_ssil_variance", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool { return program && program->is_valid(); }
    } downsample_program_;

    // Temporal resolve program (fullscreen fragment shader)
    struct temporal_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_temporal_params;
        /// x = normal-dot validity threshold (e.g. 0.85). yzw reserved.
        gfx::program::uniform_ptr u_temporal_params2;
        gfx::program::uniform_ptr u_temporal_resolution;
        gfx::program::uniform_ptr u_prev_view_proj;
        gfx::program::uniform_ptr s_ssil_curr;
        gfx::program::uniform_ptr s_ssil_history;
        gfx::program::uniform_ptr s_depth;
        gfx::program::uniform_ptr s_prev_depth;
        gfx::program::uniform_ptr s_ssil_moments_history;
        /// Full-res G-buffer normal. Used for the normal-validity disocclusion gate.
        gfx::program::uniform_ptr s_normal;
        /// Velocity buffer (RG total uv-delta, BA object-only component).
        gfx::program::uniform_ptr s_velocity;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_temporal_params, "u_temporal_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_temporal_params2, "u_temporal_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_temporal_resolution, "u_temporal_resolution", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_prev_view_proj, "u_prev_view_proj", gfx::uniform_type::Mat4);
            cache_uniform(program.get(), s_ssil_curr, "s_ssil_curr", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssil_history, "s_ssil_history", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_prev_depth, "s_prev_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssil_moments_history, "s_ssil_moments_history", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_velocity, "s_velocity", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool { return program && program->is_valid(); }
    } temporal_program_;

    // Joint-bilateral upsample program (fullscreen fragment shader)
    struct upsample_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_upsample_params;
        gfx::program::uniform_ptr s_ssil_input;
        gfx::program::uniform_ptr s_normal;
        gfx::program::uniform_ptr s_depth;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_upsample_params, "u_upsample_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_ssil_input, "s_ssil_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool { return program && program->is_valid(); }
    } upsample_program_;
};

} // namespace unravel
