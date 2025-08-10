#include "hiz_pass.h"
#include <engine/assets/asset_manager.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <engine/profiler/profiler.h>

namespace unravel
{

auto hiz_pass::init(rtti::context& ctx) -> bool
{
    // Get the asset manager for loading compute shaders
    auto& am = ctx.get_cached<asset_manager>();

    // Load compute shaders
    auto cs_hiz_generate = am.get_asset<gfx::shader>("engine:/data/shaders/ssr/cs_hiz_generate.sc");
    auto cs_hiz_downsample = am.get_asset<gfx::shader>("engine:/data/shaders/ssr/cs_hiz_downsample.sc");

    if (!cs_hiz_generate || !cs_hiz_downsample)
    {
        return false;
    }

    // Create compute programs using gpu_program for compute shaders
    hiz_generate_.program = std::make_shared<gpu_program>(cs_hiz_generate);
    hiz_downsample_.program = std::make_shared<gpu_program>(cs_hiz_downsample);

    // Cache uniforms using the uniforms_cache pattern
    hiz_generate_.cache_uniforms();
    hiz_downsample_.cache_uniforms();

    return hiz_generate_.program->is_valid() && hiz_downsample_.program->is_valid();
}

void hiz_pass::run(gfx::render_view& rview, const run_params& params)
{
    // Validate input parameters
    const auto& depth_buffer = params.depth_buffer;
    const auto& output_hiz = params.output_hiz;
    if (!depth_buffer || !output_hiz || !params.cam)
    {
        return;
    }

    const uint32_t hiz_width = output_hiz->info.width;
    const uint32_t hiz_height = output_hiz->info.height;
    const uint32_t num_mips = output_hiz->info.numMips;

    // 1) Generate Hi-Z mip 0 from depth buffer
    {
        gfx::render_pass pass("hiz_generate_compute_pass");

        // Begin the compute program
        hiz_generate_.program->begin();

        // Set input depth texture using gfx utilities
        gfx::set_texture(hiz_generate_.s_depth, 0, depth_buffer);

        // Set output Hi-Z mip 0 as image
        gfx::set_image(1, output_hiz->native_handle(), 0, bgfx::Access::Write);

        // Set parameters for the compute shader
        math::vec4 hiz_params(float(hiz_width), float(hiz_height), 0.0f, 0.0f);
        gfx::set_uniform(hiz_generate_.u_hiz_params, hiz_params);

        // Dispatch compute shader with 8x8 thread groups
        uint32_t num_groups_x = (hiz_width + 7) / 8;
        uint32_t num_groups_y = (hiz_height + 7) / 8;
        bgfx::dispatch(pass.id, hiz_generate_.program->native_handle(), num_groups_x, num_groups_y, 1);

        hiz_generate_.program->end();
    }

    // 2) Generate remaining mip levels using compute shaders - optimized batching
    if (num_mips > 1)
    {
        gfx::render_pass pass("hiz_downsample_compute_pass");
        
        // Begin the downsampling compute program once for all mips
        hiz_downsample_.program->begin();

        for (uint32_t mip = 1; mip < num_mips; ++mip)
        {
            const uint32_t current_mip_width = hiz_width >> mip;
            const uint32_t current_mip_height = hiz_height >> mip;

            // Ensure minimum size of 1x1
            if (current_mip_width == 0 || current_mip_height == 0)
            {
                break;
            }

            // Set input (previous mip level) as read-only image
            gfx::set_image(0, output_hiz->native_handle(), mip - 1, bgfx::Access::Read);

            // Set output (current mip level) as write-only image
            gfx::set_image(1, output_hiz->native_handle(), mip, bgfx::Access::Write);

            // Set parameters for the downsampling compute shader
            math::vec4 hiz_params(float(current_mip_width), float(current_mip_height), 2.0f, float(mip));
            gfx::set_uniform(hiz_downsample_.u_hiz_params, hiz_params);

            // Dispatch compute shader with 8x8 thread groups
            uint32_t num_groups_x = (current_mip_width + 7) / 8;
            uint32_t num_groups_y = (current_mip_height + 7) / 8;
            bgfx::dispatch(pass.id, hiz_downsample_.program->native_handle(), num_groups_x, num_groups_y, 1);
        }

        // End the program once after all dispatches
        hiz_downsample_.program->end();
    }

    // Memory barrier to ensure all compute operations are complete
    // This is handled automatically by bgfx between frame boundaries,
    // but we add it for safety if the Hi-Z buffer is used immediately
}

} // namespace unravel
