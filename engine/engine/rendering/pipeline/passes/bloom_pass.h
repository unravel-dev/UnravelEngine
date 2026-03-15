#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>

namespace unravel
{

class bloom_pass
{
public:
    struct settings
    {
        float threshold = 1.0f;
        float soft_knee = 0.5f; // 0 = hard cutoff, 1 = soft transition (prevents specular flicker)
        float clamp = 20.0f;    // max value before threshold; limits firefly exaggeration
        float intensity = 1.0f;
        int mip_count = 5;
    };

    struct run_params
    {
        gfx::frame_buffer::ptr input;
        gfx::frame_buffer::ptr output;
        settings config{};
    };

    auto init(rtti::context& ctx) -> bool;
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;
    void release_resources(gfx::render_view& rview);

private:
    static constexpr int max_mip_count = 6;

    auto create_or_resize_mip_chain(gfx::render_view& rview,
                                    const usize32_t& viewport_size,
                                    int mip_count) -> void;
    auto get_mip_fbo(gfx::render_view& rview, int mip_index) -> const gfx::frame_buffer::ptr&;
    auto create_or_update_output_fb(gfx::render_view& rview,
                                    const gfx::frame_buffer::ptr& input,
                                    const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr;

    struct downsample_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_pixel_size, "u_pixelSize", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_params, "u_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_tex, "s_tex", gfx::uniform_type::Sampler);
        }
        gfx::program::uniform_ptr u_pixel_size;
        gfx::program::uniform_ptr u_params;
        gfx::program::uniform_ptr s_tex;
        std::unique_ptr<gpu_program> program;
    } downsample_program_;

    struct upsample_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_pixel_size, "u_pixelSize", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_intensity, "u_intensity", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_tex, "s_tex", gfx::uniform_type::Sampler);
        }
        gfx::program::uniform_ptr u_pixel_size;
        gfx::program::uniform_ptr u_intensity;
        gfx::program::uniform_ptr s_tex;
        std::unique_ptr<gpu_program> program;
    } upsample_program_;

    struct combine_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_scene, "s_scene", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_bloom, "s_bloom", gfx::uniform_type::Sampler);
        }
        gfx::program::uniform_ptr s_scene;
        gfx::program::uniform_ptr s_bloom;
        std::unique_ptr<gpu_program> program;
    } combine_program_;
};

} // namespace unravel
