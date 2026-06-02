// prefilter_pass.cpp
#include "prefilter_pass.h"
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
namespace unravel
{

auto prefilter_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs = am.get_asset<gfx::shader>("engine:/data/shaders/prefilter/cs_prefilter.sc");
    cs_.program = std::make_unique<gpu_program>(cs);
    cs_.cache_uniforms();
    auto cs_mip = am.get_asset<gfx::shader>("engine:/data/shaders/prefilter/cs_mip_downsample.sc");
    mip_downsample_.program = std::make_unique<gpu_program>(cs_mip);
    mip_downsample_.cache_uniforms();
    return cs_.program->is_valid() && mip_downsample_.program->is_valid();
}

auto prefilter_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr
{
    return run_compute(rview, params);
}

auto prefilter_pass::run_compute(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr
{
    // Prepare output cubemap
    const auto& ti = params.output_cube->info;
    uint8_t max_mips = ti.numMips;

    // if(bgfx::getRendererType() == bgfx::RendererType::Direct3D12)
    // {
    //     APP_SCOPE_PERF("Rendering/Env Mip Gen Pass");
    //     for(uint8_t face = 0; face < 6; ++face)
    //     {
    //         generate_mips(params.input_faces[face]);
    //     }
    // }

    // Simple copy if disabled
    {
        APP_SCOPE_PERF("Rendering/Env Blit Pass");

        auto output_cube = params.output_cube;

        if(!params.apply_prefilter)
        {
            output_cube = params.output_cube_prefiltered;
        }
        gfx::render_pass pass("Prefilter/Blit Faces to Cubemap Pass");
        for(uint8_t face = 0; face < 6; ++face)
        {
            auto src = params.input_faces[face]->native_handle();
            for(uint8_t mip = 0; mip < max_mips; ++mip)
            {
                uint16_t dim = ti.width >> mip;
                bgfx::blit(pass.id, output_cube->native_handle(), mip, 0, 0, face, src, mip, 0, 0, 0, dim, dim, 1);
            }
        }

        if(!params.apply_prefilter)
        {
            return output_cube;
        }
    }

    // Compute shader prefiltering
    {
        APP_SCOPE_PERF("Rendering/Env Compute Prefilter Pass");

        const auto& input_cube = params.output_cube;
        const auto& output_cube = params.output_cube_prefiltered;
        uint16_t cube_size = ti.width;

        // Process all mip levels using compute shader
        for(uint8_t mip = 0; mip < max_mips; ++mip)
        {
            gfx::render_pass::push_scope("Prefilter");
            gfx::render_pass::push_scope(fmt::format("MIP {}", mip).c_str());

            uint16_t dim = cube_size >> mip;
            
            gfx::render_pass pass("Prefilter Compute Pass");

            // Begin compute program
            cs_.program->begin();

            // Bind input cubemap
            gfx::set_texture(cs_.s_env, 0, input_cube);

            // Bind output cubemap as 2D array image (all faces at once)
            gfx::set_image(1, output_cube->native_handle(), mip, bgfx::Access::Write);

            // Set uniforms for this mip level (no face index needed)
            float data[4] = {float(mip), 0.0f, float(cube_size), float(max_mips)};
            gfx::set_uniform(cs_.u_data, data);

            // Calculate dispatch size for this mip level
            // Process all faces in parallel with Z dimension
            uint32_t num_groups_x = (dim + 7) / 8;
            uint32_t num_groups_y = (dim + 7) / 8;
            uint32_t num_groups_z = 1; // All 6 faces handled by workgroup size

            // Dispatch compute shader for all faces at once
            bgfx::dispatch(pass.id, cs_.program->native_handle(), num_groups_x, num_groups_y, num_groups_z);

            cs_.program->end();

            gfx::render_pass::pop_scope();
            gfx::render_pass::pop_scope();
        }
        
        // Add memory barrier to ensure all compute operations are complete
        // This ensures proper synchronization between mip levels and before texture usage
        gfx::render_pass barrier_pass("Prefilter/Barrier Pass");
        // bgfx handles memory barriers automatically between frame boundaries,
        // but we add explicit synchronization for safety
    }

    return params.output_cube_prefiltered;
}

void prefilter_pass::generate_mips(const gfx::texture::ptr& texture)
{
    uint8_t num_mips = texture->info.numMips;
    if(num_mips <= 1)
    {
        return;
    }
    for(uint8_t mip = 1; mip < num_mips; ++mip)
    {
        uint16_t w = texture->info.width >> mip;
        uint16_t h = texture->info.height >> mip;
        if(w == 0 || h == 0)
        {
            break;
        }
        gfx::render_pass pass("Prefilter/MIP Downsample Pass");
        mip_downsample_.program->begin();
        gfx::set_image(0, texture->native_handle(), mip - 1, bgfx::Access::Read);
        gfx::set_image(1, texture->native_handle(), mip, bgfx::Access::Write);
        float params_data[4] = {float(w), float(h), 0.0f, 0.0f};
        gfx::set_uniform(mip_downsample_.u_params, params_data);
        uint32_t gx = (w + 7) / 8;
        uint32_t gy = (h + 7) / 8;
        bgfx::dispatch(pass.id, mip_downsample_.program->native_handle(), gx, gy, 1);
        mip_downsample_.program->end();
    }
}

} // namespace unravel
