#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <engine/rendering/camera.h>
#include <graphics/texture.h>
#include <graphics/render_pass.h>

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
        float history_strength = 0.5f;
        float depth_threshold = 0.01f;
        int max_accum_frames = 8;
    };

    /// SSIL settings
    struct ssil_settings
    {
        int max_rays = 4;
        int max_steps = 64;
        float depth_tolerance = 0.15f;
        float brightness = 0.15f;
        float max_distance = 20.0f;
        bool enable_half_res = false;

        bool enable_multi_bounce = true;
        float multi_bounce_intensity = 0.5f;

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
        const camera* cam{};
        ssil_settings settings;
    };

    auto init(rtti::context& ctx) -> bool;

    /// Executes the full SSIL pipeline (trace -> denoise -> temporal).
    /// Returns the SSIL output texture (RGB = indirect diffuse, A = confidence).
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr;

    /// Releases all GPU resources owned by this pass from the render_view.
    void release_resources(gfx::render_view& rview);

private:
    auto run_trace(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

    auto run_spatial_denoise(gfx::render_view& rview,
                             const gfx::frame_buffer::ptr& ssil_curr,
                             const gfx::frame_buffer::ptr& g_buffer,
                             const ssil_settings& settings) -> gfx::frame_buffer::ptr;

    auto run_temporal_resolve(gfx::render_view& rview,
                              const gfx::frame_buffer::ptr& ssil_input,
                              const gfx::frame_buffer::ptr& g_buffer,
                              const gfx::texture::ptr& prev_depth,
                              const camera* cam,
                              const ssil_settings& settings) -> gfx::texture::ptr;

    auto create_or_update_ssil_fb(gfx::render_view& rview,
                                  const std::string& name,
                                  const gfx::frame_buffer::ptr& reference,
                                  bool half_res,
                                  uint64_t extra_flags = 0) -> gfx::frame_buffer::ptr;

    auto create_or_update_ssil_tex(gfx::render_view& rview,
                                   const std::string& name,
                                   const gfx::frame_buffer::ptr& reference,
                                   bool half_res,
                                   uint64_t extra_flags = 0) -> gfx::texture::ptr;

    // Trace program (fullscreen fragment shader)
    struct trace_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_ssil_params;
        gfx::program::uniform_ptr u_ssil_params2;
        gfx::program::uniform_ptr s_color;
        gfx::program::uniform_ptr s_normal;
        gfx::program::uniform_ptr s_hiz;
        gfx::program::uniform_ptr s_emissive;
        gfx::program::uniform_ptr s_albedo;
        gfx::program::uniform_ptr s_prev_ssil;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_ssil_params, "u_ssil_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_ssil_params2, "u_ssil_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_color, "s_color", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_hiz, "s_hiz", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_emissive, "s_emissive", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_albedo, "s_albedo", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_prev_ssil, "s_prev_ssil", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool { return program && program->is_valid(); }
    } trace_program_;

    // Spatial denoise compute program
    struct denoise_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_denoise_params;
        gfx::program::uniform_ptr s_ssil_input;
        gfx::program::uniform_ptr s_normal;
        gfx::program::uniform_ptr s_depth;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_denoise_params, "u_denoise_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_ssil_input, "s_ssil_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_normal, "s_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
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

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_temporal_params, "u_temporal_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_prev_view_proj, "u_prev_view_proj", gfx::uniform_type::Mat4);
            cache_uniform(program.get(), s_ssil_curr, "s_ssil_curr", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssil_history, "s_ssil_history", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_prev_depth, "s_prev_depth", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool { return program && program->is_valid(); }
    } temporal_program_;
};

} // namespace unravel
