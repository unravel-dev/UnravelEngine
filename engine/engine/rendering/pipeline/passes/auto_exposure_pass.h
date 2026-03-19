#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <bgfx/bgfx.h>

namespace unravel
{

class auto_exposure_pass
{
public:
    struct settings
    {
        float min_ev = -1.0f;
        float max_ev = 0.0f;
        float compensation = 0.0f;
        float adaptation_speed_up = 3.0f;
        float adaptation_speed_down = 1.0f;
        float low_percentile = 0.50f;
        float high_percentile = 0.95f;
    };

    struct run_params
    {
        gfx::frame_buffer::ptr input;
        settings config{};
        float delta_time = 0.0f;
    };

    auto init(rtti::context& ctx) -> bool;

    /// Runs histogram + temporal average to produce a 1x1 adapted exposure texture.
    void run(gfx::render_view& rview, const run_params& params);

    /// Returns the current adapted exposure value's texture (1x1 R32F).
    /// Usable by the tonemapping pass to read the exposure.
    auto get_exposure_texture(gfx::render_view& rview) const -> gfx::texture::ptr;

    void release_resources(gfx::render_view& rview);

private:
    static constexpr float min_log_lum = -10.0f;
    static constexpr float max_log_lum = 20.0f;
    static constexpr int histogram_bins = 256;

    void ensure_resources(gfx::render_view& rview);
    void run_histogram(gfx::render_view& rview, const gfx::frame_buffer::ptr& input);
    void run_average(gfx::render_view& rview, const settings& config, float dt);

    struct histogram_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_hdr_input;
        gfx::program::uniform_ptr u_histogram_params;

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_hdr_input, "s_hdr_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_histogram_params, "u_histogram_params", gfx::uniform_type::Vec4);
        }
    } histogram_program_;

    struct average_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_average_params0;
        gfx::program::uniform_ptr u_average_params1;
        gfx::program::uniform_ptr u_average_params2;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_average_params0, "u_average_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_average_params1, "u_average_params1", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_average_params2, "u_average_params2", gfx::uniform_type::Vec4);
        }
    } average_program_;

    bgfx::DynamicIndexBufferHandle histogram_buffer_ = BGFX_INVALID_HANDLE;
};

} // namespace unravel
