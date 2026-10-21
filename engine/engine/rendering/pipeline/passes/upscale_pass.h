#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/frame_buffer.h>

namespace unravel
{

/**
 * @brief Upscales a scene rendered at a fraction of its view's size into the view's output.
 *
 * A 5-tap Catmull-Rom filter keeps the sharpness of a bicubic filter from five bilinear taps. The film grain
 * and dither the tone curve leaves for later are added here, at the output's resolution: added before the
 * upscale, the filter would blur them into blotches and the banding they hide would come back.
 */
class upscale_pass
{
public:
    struct run_params
    {
        /// The finished scene at the render size, display encoded.
        gfx::frame_buffer::ptr input;
        /// The view's output, at the viewport's size.
        gfx::frame_buffer::ptr output;
        /// The output with a depth of its own attached, to fill with `input_depth` too; null: the color alone.
        gfx::frame_buffer::ptr output_depth;
        /// The scene's depth at the render size.
        gfx::texture::ptr input_depth;
        /// Film grain amount, as the tonemapping settings give it.
        float grain_intensity = 0.0f;
        bool dithering = false;
    };

    /// Loads the program; called once, after bgfx and the asset manager are up.
    auto init(rtti::context& ctx) -> bool;

    void run(const run_params& params);

private:
    /// Each output pixel takes the depth of the scene texel under it.
    void resample_depth(bgfx::ViewId view, const gfx::texture::ptr& input_depth);

    struct upscale_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_input, "s_input", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_upscale_input_size, "u_upscale_input_size", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_output_noise, "u_output_noise", bgfx::UniformType::Vec4);
        }

        gfx::program::uniform_ptr s_input;
        gfx::program::uniform_ptr u_upscale_input_size;
        gfx::program::uniform_ptr u_output_noise;
        std::unique_ptr<gpu_program> program;
    } upscale_program_;

    struct depth_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_depth, "s_depth", bgfx::UniformType::Sampler);
        }

        gfx::program::uniform_ptr s_depth;
        std::unique_ptr<gpu_program> program;
    } depth_program_;
};

} // namespace unravel
