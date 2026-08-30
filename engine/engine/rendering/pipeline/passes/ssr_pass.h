#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <engine/rendering/camera.h>
#include <graphics/texture.h>
#include <graphics/render_pass.h>
#include "tonemapping_pass.h"
#include "trace_resolution.h"
#include <array>

namespace unravel
{

class ssr_pass
{
public:

    /// FidelityFX SSR settings
    struct fidelityfx_ssr_settings
    {
        /// Cone tracing parameters
        struct cone_tracing_settings
        {
            float cone_angle_bias = 0.05f;                  ///< Controls cone growth rate (0.1 - 0.5)
            int max_mip_level = 6;                          ///< Number of blur mip levels - 1
            float blur_base_sigma = 1.0f;                   ///< Base blur sigma for mip generation (CPU-side only)
            float roughness_multiplier = 2.0f;             ///< Multiplier for roughness-based blur (higher = more blur for rough surfaces)
        };

        /// Temporal accumulation parameters
        struct temporal_settings
        {
            float history_strength     = 0.9f;   // 0 … 1  (was blend_factor)
            float depth_threshold      = 0.01f;  // clip-space 0 … ~0.03
            float roughness_sensitivity= 0.3f;   // 0 … 1
            float motion_scale_pixels = 120.0f;   // Motion scale in pixels
            float normal_dot_threshold = 0.95f;   // Normal dot threshold for motion detection
            int max_accum_frames = 8;        // Maximum accumulation frames
        };

        int max_steps = 64;                             ///< Maximum ray marching steps for hierarchical traversal
        int max_rays = 4;                              ///< Maximum rays for rough surfaces (future: cone tracing)
        float depth_tolerance = 0.1f;                   ///< Depth tolerance for hit validation
        float brightness = 1.0f;                        ///< Reflection brightness multiplier
        /// Width of the ANGULAR dead cone toward the exact view axis where SSR fades out
        /// (reflections of the camera's own position - occluded space with no color).
        /// 0 = no fade, 0.1 fades only the last ~37 degrees, 1 fades across the whole
        /// toward-camera hemisphere. Toward-camera hits OUTSIDE the cone composite at full
        /// confidence now: the backface rejection already guarantees their stored color is
        /// the face the ray sees (the old meters-scaled dot collapsed every toward-camera
        /// ray to this value as a floor - mirrors ghosted their foreground reflections).
        float facing_reflections_fading = 0.1f;
        float roughness_depth_tolerance = 1.0f;         ///< Additional depth tolerance for rough surfaces
        /// Border band, as a fraction of the screen, over which reflections sourced near
        /// the frame edge fade toward the fallback layer - softening the transition where
        /// rays leave the screen. Hits deeper than the band composite at full strength.
        /// (Replaces the fade_in_start/fade_in_end pair, whose two values were fed as
        /// per-axis widths by accident; legacy scenes load fade_in_start into this.)
        float screen_edge_fade = 0.1f;
        /// Trace resolution divisor. SSR is runtime-capped at `half` because sub-half
        /// tracing breaks Hi-Z traversal, temporal clamp and the spatial denoiser.
        trace_resolution resolution = trace_resolution::full;
        // Cone tracing parameters
        bool enable_cone_tracing = false;                 ///< Enable cone tracing for glossy reflections
        cone_tracing_settings cone_tracing;             ///< Cone tracing specific settings
        

        bool enable_temporal_accumulation = true;       ///< Enable temporal accumulation
        // Temporal accumulation parameters
        temporal_settings temporal;                      ///< Temporal accumulation settings

        /// Spatial denoise parameters (a-trous wavelet filter between trace and temporal resolve)
        struct spatial_denoise_settings
        {
            float depth_sigma = 0.02f;      ///< Depth edge-stopping threshold (0.005 strict – 0.05 loose)
            float normal_power = 64.0f;     ///< Normal edge-stopping exponent (16 loose – 128 strict)
            float luma_sigma = 1.0f;        ///< Luminance edge-stopping threshold (0.3 sharp – 2.0 smooth)
            int passes = 3;                 ///< Number of a-trous filter passes (step = 1, 2, 4, ... 1<<i)
        };

        bool enable_spatial_denoise = false;             ///< Enable spatial denoising before temporal resolve
        spatial_denoise_settings spatial_denoise;        ///< Spatial denoise settings
    };

    /// Combined SSR settings
    struct ssr_settings
    {
        fidelityfx_ssr_settings fidelityfx;             ///< FidelityFX SSR settings
    };

    struct run_params
    {
        gfx::frame_buffer::ptr output;       ///< Optional output buffer
        gfx::frame_buffer::ptr g_buffer;     ///< G-buffer containing normals
        gfx::texture::ptr hiz_buffer;        ///< Hi-Z buffer texture
        gfx::texture::ptr previous_frame;    ///< Previous frame color for reflection sampling
        /// This frame's velocity buffer, passed explicitly by the pipeline. A valid
        /// texture IS the enable; null = legacy matrix reprojection.
        gfx::texture::ptr velocity;
        /// True while the velocity pass drew ANY mover within one accumulation window.
        /// This is the CONTENT-LAG guard, not a ghost-rejection guard: the trace samples
        /// PREV_SCENE_HDR, so a moving emitter's radiance sweeps across every glossy
        /// surface one frame late while the geometry at the hit stays static and
        /// t-confirmed - no per-pixel geometric signal can see a radiance-only change,
        /// and deep accumulation stretches that one-frame lag into a long tail. While
        /// dynamic radiance exists on screen the accumulation window is capped, which is
        /// the correct window for a signal that changes every frame; converged static
        /// content agrees with its neighbourhood box and loses only the release's tail.
        bool velocity_movers_recent = false;
        const camera* cam{};
        ssr_settings settings;
    };

    /// Must be called once (after bgfx::init() and after `asset_manager` is registered in context).
    auto init(rtti::context& ctx) -> bool;

    /// Executes the SSR pass. Returns the actual output framebuffer.
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

    /// Releases all GPU resources owned by this pass from the render_view.
    void release_resources(gfx::render_view& rview);

    /// Executes the FidelityFX SSR pass. Returns the actual output framebuffer.
    auto run_fidelityfx(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

    /// Executes the three-pass SSR pipeline (trace, temporal resolve, composite)
    auto run_fidelityfx_three_pass(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

    /// Executes the SSR trace pass only. Returns SSR current frame buffer.
    auto run_ssr_trace(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

    /// Executes the temporal resolve pass. Returns updated SSR history buffer.
    /// @param velocity This frame's velocity buffer; null = legacy matrix reprojection.
    /// @param curr_hit_t The trace's mean hit-distance target (SSR_CURR attachment 1) -
    ///        always the TRACE's own output, even when the colour input was spatially
    ///        denoised: hit distance carries validation data, not an image to filter.
    /// @param velocity_movers_recent The content-lag window cap (see run_params).
    auto run_temporal_resolve(gfx::render_view& rview,
                              const gfx::frame_buffer::ptr& ssr_curr,
                              const gfx::texture::ptr& curr_hit_t,
                              const gfx::frame_buffer::ptr& g_buffer,
                              const gfx::texture::ptr& velocity,
                              bool velocity_movers_recent,
                              const camera* cam,
                              const fidelityfx_ssr_settings& settings) -> gfx::frame_buffer::ptr;

    /// Executes the composite pass. Returns final blended output.
    auto run_composite(gfx::render_view& rview,
                      const gfx::frame_buffer::ptr& ssr_history,
                      const gfx::frame_buffer::ptr& ssr_curr,
                      const gfx::frame_buffer::ptr& probe_buffer,
                      const gfx::frame_buffer::ptr& g_buffer,
                      const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr;

    /// Generates blurred color buffer with mip chain for cone tracing
    auto generate_blurred_color_buffer(gfx::render_view& rview, 
                                     const gfx::texture::ptr& input_color,
                                     const gfx::frame_buffer::ptr& g_buffer,
                                     const fidelityfx_ssr_settings& settings) -> gfx::texture::ptr;

    /// Executes spatial denoise on SSR result before temporal resolve
    auto run_spatial_denoise(gfx::render_view& rview,
                             const gfx::frame_buffer::ptr& ssr_curr,
                             const gfx::frame_buffer::ptr& g_buffer,
                             const fidelityfx_ssr_settings& settings) -> gfx::frame_buffer::ptr;

private:
    /// Creates or updates the output framebuffer using the render_view
    auto create_or_update_output_fb(gfx::render_view& rview,
                                   const gfx::frame_buffer::ptr& reference,
                                   const gfx::frame_buffer::ptr& output)
        -> gfx::frame_buffer::ptr;

    /// Creates or updates the SSR current framebuffer at the given trace resolution.
    auto create_or_update_ssr_curr_fb(gfx::render_view& rview,
                                      const gfx::frame_buffer::ptr& reference,
                                      trace_resolution res) -> gfx::frame_buffer::ptr;

    /// Creates or updates the SSR history texture at the given trace resolution.
    auto create_or_update_ssr_history_tex(gfx::render_view& rview,
                                          const gfx::frame_buffer::ptr& reference,
                                          trace_resolution res) -> gfx::texture::ptr;

    /// Creates or updates the SSR history temp framebuffer at the given trace resolution.
    auto create_or_update_ssr_history_temp_fb(gfx::render_view& rview,
                                              const gfx::frame_buffer::ptr& reference,
                                              trace_resolution res) -> gfx::frame_buffer::ptr;

    /// Creates or updates a named SSR denoise ping-pong framebuffer at the given trace resolution.
    auto create_or_update_ssr_denoise_fb(gfx::render_view& rview,
                                         const std::string& name,
                                         const gfx::frame_buffer::ptr& reference,
                                         trace_resolution res) -> gfx::frame_buffer::ptr;


    // FidelityFX SSR Pixel Shader Program
    struct fidelityfx_pixel_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_ssr_params;      // x: max_steps, y: depth_tolerance, z: max_rays, w: brightness
        gfx::program::uniform_ptr u_hiz_params;      // x: hiz_width, y: hiz_height, z: trace_scale_x, w: trace_scale_y
        gfx::program::uniform_ptr u_fade_params;     // x: screen_edge_fade, y: unused, z: roughness_depth_tolerance, w: facing_reflections_fading
        gfx::program::uniform_ptr u_cone_params;     // x: cone_angle_bias, y: max_mip_level, z: unused, w: unused
        gfx::program::uniform_ptr u_prev_view_proj;   // Previous frame view-projection matrix
        gfx::program::uniform_ptr s_color;           // Input color texture
        gfx::program::uniform_ptr s_normal;          // Normal buffer
        gfx::program::uniform_ptr s_depth;           // Depth buffer
        gfx::program::uniform_ptr s_hiz;             // Hi-Z buffer
        gfx::program::uniform_ptr s_color_blurred;   // Pre-blurred color buffer with mip chain

        void cache_uniforms()
        {
            // Manual uniform creation for FidelityFX SSR using std::make_shared
            cache_uniform(program.get(), u_ssr_params, "u_ssr_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_hiz_params, "u_hiz_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_fade_params, "u_fade_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cone_params, "u_cone_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_prev_view_proj, "u_prev_view_proj", gfx::uniform_type::Mat4);
            cache_uniform(program.get(), s_color, "s_color", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_hiz, "s_hiz", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_color_blurred, "s_color_blurred", gfx::uniform_type::Sampler);
        }
        
        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } fidelityfx_pixel_program_;

    // Temporal resolve program for SSR temporal accumulation
    struct temporal_resolve_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_temporal_params;  // x: enable_temporal, y: history_strength, z: depth_threshold, w: roughness_sensitivity
        gfx::program::uniform_ptr u_motion_params;    // x: motion_scale_pixels, y: normal_dot_threshold, z: max_accum_frames, w: unused
        gfx::program::uniform_ptr u_fade_params;      // x: content-lag release ceiling, y: unused, z: trace_scale_x, w: trace_scale_y
        gfx::program::uniform_ptr u_prev_view_proj;   // Previous frame view-projection matrix
        gfx::program::uniform_ptr s_ssr_curr;         // Current frame SSR result
        gfx::program::uniform_ptr s_ssr_history;      // Previous frame SSR history
        gfx::program::uniform_ptr s_normal;           // Normal buffer
        gfx::program::uniform_ptr s_depth;            // Depth buffer
        gfx::program::uniform_ptr s_velocity;         // Velocity buffer (RG total, BA object-only)
        gfx::program::uniform_ptr s_ssr_curr_hit_t;   // Trace mean hit distance THIS frame
        gfx::program::uniform_ptr s_ssr_hist_hit_t;   // Accumulated hit-distance history

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_temporal_params, "u_temporal_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_motion_params, "u_motion_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_fade_params, "u_fade_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_prev_view_proj, "u_prev_view_proj", gfx::uniform_type::Mat4);
            cache_uniform(program.get(), s_ssr_curr, "s_ssr_curr", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssr_history, "s_ssr_history", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_velocity, "s_velocity", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssr_curr_hit_t, "s_ssr_curr_hit_t", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssr_hist_hit_t, "s_ssr_hist_hit_t", gfx::uniform_type::Sampler);
        }
        
        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } temporal_resolve_program_;

    // Composite program for blending SSR with reflection probes
    struct composite_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_ssr_history;      // Temporally filtered SSR result
        gfx::program::uniform_ptr s_ssr_curr;         // Current frame SSR result (for confidence)
        gfx::program::uniform_ptr s_normal;           // Normal buffer
        gfx::program::uniform_ptr s_depth;            // Depth buffer

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_ssr_history, "s_ssr_history", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssr_curr, "s_ssr_curr", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
        }
        
        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } composite_program_;

    // Unified blur compute program for cone tracing
    struct blur_compute_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_blur_params;
        gfx::program::uniform_ptr s_normal;  // Normal buffer for roughness sampling
        
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_blur_params, "u_blur_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
        }
        
        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    };
    blur_compute_program blur_compute_program_;

    // Spatial denoise compute program for edge-preserving a-trous wavelet filtering
    struct spatial_denoise_compute_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_denoise_params;  // x: step_size, y: depth_sigma, z: normal_power, w: luma_sigma
        gfx::program::uniform_ptr s_ssr_input;        // Input SSR result (sampler)
        gfx::program::uniform_ptr s_normal;           // Normal buffer
        gfx::program::uniform_ptr s_depth;            // Depth buffer

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_denoise_params, "u_denoise_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_ssr_input, "s_ssr_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    };
    spatial_denoise_compute_program spatial_denoise_compute_program_;

};

} // namespace unravel
