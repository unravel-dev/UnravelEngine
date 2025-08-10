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
  
    
    // Load compute shader version
    auto cs = am.get_asset<gfx::shader>("engine:/data/shaders/prefilter/cs_prefilter.sc");
    cs_.program = std::make_unique<gpu_program>(cs);
    cs_.cache_uniforms();
    
    return cs_.program->is_valid();
}

auto prefilter_pass::run(const run_params& params) -> gfx::texture::ptr
{
    return run_compute(params);
}

auto prefilter_pass::run_compute(const run_params& params) -> gfx::texture::ptr
{
    // Prepare output cubemap
    const auto& ti = params.output_cube->info;
    uint8_t max_mips = ti.numMips;

    // Simple copy if disabled
    {
        APP_SCOPE_PERF("Rendering/Env Blit Pass");

        auto output_cube = params.output_cube;

        if(!params.apply_prefilter)
        {
            output_cube = params.output_cube_prefiltered;
        }
        gfx::render_pass pass("blit_faces_to_cubemap_pass");
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
            return params.output_cube;
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
            gfx::render_pass::push_scope(fmt::format("mip {}", mip).c_str());

            uint16_t dim = cube_size >> mip;
            
            gfx::render_pass pass("prefilter_compute_pass");

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
        }
        
        // Add memory barrier to ensure all compute operations are complete
        // This ensures proper synchronization between mip levels and before texture usage
        gfx::render_pass barrier_pass("prefilter_barrier_pass");
        // bgfx handles memory barriers automatically between frame boundaries,
        // but we add explicit synchronization for safety
    }

    return params.output_cube_prefiltered;
}

} // namespace unravel
