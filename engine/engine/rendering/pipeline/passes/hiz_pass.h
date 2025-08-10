#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <engine/rendering/camera.h>
#include <graphics/texture.h>
#include <graphics/render_pass.h>
#include <hpp/small_vector.hpp>
#include <bgfx/bgfx.h>

namespace unravel
{

class hiz_pass
{
public:
    struct run_params
    {
        gfx::texture::ptr depth_buffer;   ///< Source depth buffer
        gfx::texture::ptr output_hiz;     ///< Output Hi-Z texture (must be R32F or R16F format with mips)
        const camera* cam{};              ///< Camera for near/far plane information
    };

    /// Must be called once (after bgfx::init() and after `asset_manager` is registered in context).
    auto init(rtti::context& ctx) -> bool;

    /// Executes the Hi-Z generation: generates mip chain from depth buffer using compute shaders
    void run(gfx::render_view& rview, const run_params& params);

private:
    /// Hi-Z generation compute program using uniforms_cache pattern
    struct hiz_generate_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_depth;         ///< Input depth texture sampler
        gfx::program::uniform_ptr u_hiz_params;    ///< Hi-Z generation parameters

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_hiz_params, "u_hiz_params", gfx::uniform_type::Vec4);
        }
    };

    /// Hi-Z downsampling compute program using uniforms_cache pattern
    struct hiz_downsample_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_hiz_params;    ///< Hi-Z downsampling parameters

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_hiz_params, "u_hiz_params", gfx::uniform_type::Vec4);
        }
    };

    hiz_generate_program hiz_generate_;     ///< Initial Hi-Z generation compute program
    hiz_downsample_program hiz_downsample_; ///< Hi-Z downsampling compute program
};

} // namespace unravel 
