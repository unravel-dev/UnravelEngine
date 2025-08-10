#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <engine/rendering/camera.h>
#include <graphics/texture.h>
#include <graphics/render_pass.h>
#include "tonemapping_pass.h"
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
        int max_rays = 8;                              ///< Maximum rays for rough surfaces (future: cone tracing)
        float depth_tolerance = 0.1f;                   ///< Depth tolerance for hit validation
        float brightness = 1.0f;                        ///< Reflection brightness multiplier
        float facing_reflections_fading = 0.1f;         ///< Fade factor for camera-facing reflections
        float roughness_depth_tolerance = 1.0f;         ///< Additional depth tolerance for rough surfaces
        float fade_in_start = 0.1f;                     ///< Screen edge fade start
        float fade_in_end = 0.2f;                       ///< Screen edge fade end
        bool enable_half_res = false;                   ///< Enable half resolution for SSR buffers
        // Cone tracing parameters
        bool enable_cone_tracing = false;                 ///< Enable cone tracing for glossy reflections
        cone_tracing_settings cone_tracing;             ///< Cone tracing specific settings
        

        bool enable_temporal_accumulation = true;       ///< Enable temporal accumulation
        // Temporal accumulation parameters
        temporal_settings temporal;                      ///< Temporal accumulation settings
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
        const camera* cam{};
        ssr_settings settings;
    };

    /// Must be called once (after bgfx::init() and after `asset_manager` is registered in context).
    auto init(rtti::context& ctx) -> bool;

    /// Executes the SSR pass. Returns the actual output framebuffer.
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

    /// Executes the FidelityFX SSR pass. Returns the actual output framebuffer.
    auto run_fidelityfx(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

    /// Executes the three-pass SSR pipeline (trace, temporal resolve, composite)
    auto run_fidelityfx_three_pass(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

    /// Executes the SSR trace pass only. Returns SSR current frame buffer.
    auto run_ssr_trace(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

    /// Executes the temporal resolve pass. Returns updated SSR history buffer.
    auto run_temporal_resolve(gfx::render_view& rview, 
                              const gfx::frame_buffer::ptr& ssr_curr,
                              const gfx::frame_buffer::ptr& g_buffer,
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


private:
    /// Creates or updates the output framebuffer using the render_view
    auto create_or_update_output_fb(gfx::render_view& rview,
                                   const gfx::frame_buffer::ptr& reference,
                                   const gfx::frame_buffer::ptr& output)
        -> gfx::frame_buffer::ptr;

    /// Creates or updates the SSR current framebuffer with size multiplier
    auto create_or_update_ssr_curr_fb(gfx::render_view& rview, 
                                      const gfx::frame_buffer::ptr& reference, 
                                      bool enable_half_res) -> gfx::frame_buffer::ptr;

    /// Creates or updates the SSR history texture with size multiplier
    auto create_or_update_ssr_history_tex(gfx::render_view& rview, 
                                          const gfx::frame_buffer::ptr& reference, 
                                          bool enable_half_res) -> gfx::texture::ptr;

    /// Creates or updates the SSR history temp framebuffer with size multiplier
    auto create_or_update_ssr_history_temp_fb(gfx::render_view& rview, 
                                              const gfx::frame_buffer::ptr& reference, 
                                              bool enable_half_res) -> gfx::frame_buffer::ptr;


    // FidelityFX SSR Pixel Shader Program
    struct fidelityfx_pixel_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_ssr_params;      // x: max_steps, y: depth_tolerance, z: max_rays, w: brightness
        gfx::program::uniform_ptr u_hiz_params;      // x: buffer_width, y: buffer_height, z: num_depth_mips, w: half_res
        gfx::program::uniform_ptr u_fade_params;     // x: fade_in_start, y: fade_in_end, z: roughness_depth_tolerance, w: facing_reflections_fading
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
        gfx::program::uniform_ptr u_fade_params;      // x: fade_in_start, y: fade_in_end, z: unused, w: unused
        gfx::program::uniform_ptr u_prev_view_proj;   // Previous frame view-projection matrix
        gfx::program::uniform_ptr s_ssr_curr;         // Current frame SSR result
        gfx::program::uniform_ptr s_ssr_history;      // Previous frame SSR history
        gfx::program::uniform_ptr s_normal;           // Normal buffer
        gfx::program::uniform_ptr s_depth;            // Depth buffer

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

};

} // namespace unravel
