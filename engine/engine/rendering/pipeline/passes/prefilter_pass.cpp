#include "prefilter_pass.h"
#include "pass_helpers.h"
#include <algorithm>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/render_feature_support.h>
#include <graphics/graphics.h>
namespace unravel
{
namespace
{
/// Threads per group along x and y of cs_prefilter.eshader.
constexpr uint32_t PREFILTER_GROUP_SIZE = 8;
/// Groups along z: one per cube face.
constexpr uint32_t CUBE_FACES = 6;
/// Threads per group along x and y of cs_cube_downsample.eshader.
constexpr uint32_t DOWNSAMPLE_GROUP_SIZE = 8;
/// The downsample samples a mip at the corner of four texels: bilinear within it, never across mips.
constexpr uint64_t MIP_SOURCE_FLAGS =
    BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIP_POINT;
} // namespace

auto prefilter_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs = am.get_asset<gfx::shader>("engine:/data/shaders/prefilter/cs_prefilter.eshader");
    cs_.cache_uniforms();
    cs_.program = std::make_unique<gpu_program>(cs);
    auto downsample = am.get_asset<gfx::shader>("engine:/data/shaders/prefilter/cs_cube_downsample.eshader");
    downsample_.cache_uniforms();
    downsample_.program = std::make_unique<gpu_program>(downsample);
    return cs_.program->is_valid();
}

auto prefilter_pass::run(const run_params& params) -> gfx::texture::ptr
{
    // The filter needs its compute program and a device that writes RGBA16F cube faces; without either the
    // probe keeps its blitted, unfiltered faces instead of a cubemap nothing wrote.
    const bool can_prefilter = get_render_feature_support().is_available(render_feature::reflection_probe_prefilter) &&
                               cs_.program && cs_.program->begin();
    if(params.apply_prefilter && !can_prefilter)
    {
        run_params blit_params = params;
        blit_params.apply_prefilter = false;
        return run_compute(blit_params);
    }
    return run_compute(params);
}

auto prefilter_pass::run_compute(const run_params& params) -> gfx::texture::ptr
{
    const auto& ti = params.output_cube->info;
    uint8_t max_mips = ti.numMips;
    if(!params.apply_prefilter)
    {
        copy_faces(params, params.output_cube_prefiltered);
        return params.output_cube_prefiltered;
    }
    copy_faces(params, params.output_cube);

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
            pass.touch();

            // Bind input cubemap
            gfx::set_texture(cs_.s_env, 0, input_cube);

            // Bind output cubemap as 2D array image (all faces at once)
            bgfx::setImage(1, output_cube->native_handle(), mip, bgfx::Access::Write);

            // Set uniforms for this mip level (no face index needed)
            float data[4] = {float(mip), 0.0f, float(cube_size), float(max_mips)};
            gfx::set_uniform(cs_.u_data, data);

            // Every face of this mip level in one dispatch, a face per group along z.
            uint32_t num_groups_x = compute_group_count(dim, PREFILTER_GROUP_SIZE);
            uint32_t num_groups_y = compute_group_count(dim, PREFILTER_GROUP_SIZE);
            uint32_t num_groups_z = CUBE_FACES;

            gfx::dispatch(pass.id, cs_.program->native_handle(), num_groups_x, num_groups_y, num_groups_z);

            cs_.program->end();

            gfx::render_pass::pop_scope();
            gfx::render_pass::pop_scope();
        }
    }

    return params.output_cube_prefiltered;
}

void prefilter_pass::copy_faces(const run_params& params, const gfx::texture::ptr& output_cube)
{
    APP_SCOPE_PERF("Rendering/Env Blit Pass");
    const auto& ti = output_cube->info;
    // The faces carry the cube's box mips where the device generates them as the faces resolve, else the first only.
    const uint8_t face_mips = std::min(params.input_faces[0]->info.numMips, ti.numMips);
    gfx::render_pass pass("Prefilter/Blit Faces to Cubemap Pass");
    pass.touch();
    for(uint8_t face = 0; face < CUBE_FACES; ++face)
    {
        auto src = params.input_faces[face]->native_handle();
        for(uint8_t mip = 0; mip < face_mips; ++mip)
        {
            uint16_t dim = ti.width >> mip;
            bgfx::blit(pass.id,
                       bgfx::TextureRegion{.handle = output_cube->native_handle(),
                                           .mip = mip,
                                           .z = face,
                                           .width = dim,
                                           .height = dim,
                                           .depth = 1},
                       bgfx::TextureRegion{.handle = src, .mip = mip, .width = dim, .height = dim, .depth = 1});
        }
    }
    if(face_mips < ti.numMips)
    {
        generate_cube_mips(output_cube, face_mips);
    }
}

void prefilter_pass::generate_cube_mips(const gfx::texture::ptr& cube, std::uint8_t first_mip)
{
    if(first_mip == 0 || !downsample_.program || !downsample_.program->begin())
    {
        return;
    }
    const auto& ti = cube->info;
    if(!mip_source_ || mip_source_->info.width != ti.width || mip_source_->info.numMips != ti.numMips ||
       mip_source_->info.format != ti.format)
    {
        mip_source_ = std::make_shared<gfx::texture>(ti.width, ti.height, true, CUBE_FACES, ti.format, MIP_SOURCE_FLAGS);
    }
    for(uint8_t mip = first_mip; mip < ti.numMips; ++mip)
    {
        const uint8_t source_mip = mip - 1;
        const uint16_t source_size = std::max<uint16_t>(ti.width >> source_mip, 1);
        const uint16_t size = std::max<uint16_t>(ti.width >> mip, 1);
        gfx::render_pass pass("Prefilter/Cube Mip Pass");
        pass.touch();
        // A view's blits run before its dispatches.
        for(uint8_t face = 0; face < CUBE_FACES; ++face)
        {
            bgfx::blit(pass.id,
                       bgfx::TextureRegion{.handle = mip_source_->native_handle(),
                                           .mip = source_mip,
                                           .z = face,
                                           .width = source_size,
                                           .height = source_size,
                                           .depth = 1},
                       bgfx::TextureRegion{.handle = cube->native_handle(),
                                           .mip = source_mip,
                                           .z = face,
                                           .width = source_size,
                                           .height = source_size,
                                           .depth = 1});
        }
        gfx::set_texture(downsample_.s_source, 0, mip_source_);
        bgfx::setImage(1, cube->native_handle(), mip, bgfx::Access::Write);
        const float data[4] = {float(size), float(source_mip), 0.0f, 0.0f};
        gfx::set_uniform(downsample_.u_downsample, data);
        gfx::dispatch(pass.id,
                      downsample_.program->native_handle(),
                      compute_group_count(size, DOWNSAMPLE_GROUP_SIZE),
                      compute_group_count(size, DOWNSAMPLE_GROUP_SIZE),
                      CUBE_FACES);
    }
    downsample_.program->end();
}

} // namespace unravel
