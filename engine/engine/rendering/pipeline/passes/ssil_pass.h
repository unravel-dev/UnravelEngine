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
        float luma_sigma = 1.0f;
        int passes = 4;
    };

    /// Temporal accumulation parameters
    struct temporal_settings
    {
        float history_strength = 0.9f;
        /// Relative view-space depth tolerance for temporal disocclusion (fraction of
        /// linear depth). The shader rejects history when |expected - stored| / stored
        /// exceeds this, so it is scale-invariant (same value works near and far).
        float depth_threshold = 0.05f;
        int max_accum_frames = 8;
    };

    /// SSIL settings
    struct ssil_settings
    {
        int max_rays = 4;
        int max_steps = 64;
        float depth_tolerance = 0.15f;
        /// Gain on the on-screen indirect BOUNCE only (the environment-miss fallback stays
        /// at probe intensity). 1.0 is the physically-matched single-bounce strength, but
        /// that reads strong with saturated albedos (e.g. a red floor bleeding onto walls),
        /// so the default is a tamer 0.5; raise toward 1.0 for full physical colour bleed.
        float brightness = 0.5f;
        float max_distance = 20.0f;
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
    /// A = confidence: single-frame ray-agreement from the trace, replaced by temporal
    /// convergence when accumulation runs. The consumer blends toward the SH probe by this
    /// alpha on noisy / disoccluded / unconverged pixels (mix(irradiance, ssil.rgb*PI, a)).
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
    auto run_temporal_resolve(gfx::render_view& rview,
                              const gfx::frame_buffer::ptr& ssil_input,
                              const gfx::frame_buffer::ptr& g_buffer,
                              const gfx::texture::ptr& prev_depth,
                              const camera* cam,
                              const ssil_settings& settings) -> bool;

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

    // Trace program (fullscreen fragment shader)
    struct trace_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_ssil_params;
        gfx::program::uniform_ptr u_ssil_params2;
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

    // Temporal resolve program (fullscreen fragment shader)
    struct temporal_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_temporal_params;
        gfx::program::uniform_ptr u_prev_view_proj;
        gfx::program::uniform_ptr s_ssil_curr;
        gfx::program::uniform_ptr s_ssil_history;
        gfx::program::uniform_ptr s_depth;
        gfx::program::uniform_ptr s_prev_depth;
        gfx::program::uniform_ptr s_ssil_moments_history;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_temporal_params, "u_temporal_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_prev_view_proj, "u_prev_view_proj", gfx::uniform_type::Mat4);
            cache_uniform(program.get(), s_ssil_curr, "s_ssil_curr", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssil_history, "s_ssil_history", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_prev_depth, "s_prev_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssil_moments_history, "s_ssil_moments_history", gfx::uniform_type::Sampler);
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
