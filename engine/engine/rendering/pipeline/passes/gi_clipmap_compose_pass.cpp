#include "gi_clipmap_compose_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>

#include <graphics/graphics.h>

namespace unravel
{
namespace
{
/// Must match NUM_THREADS in cs_gi_clipmap_compose.sc.
constexpr uint32_t compose_group_size = 4u;
} // namespace

auto gi_clipmap_compose_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs_compose = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_clipmap_compose.sc");
    compose_program_.program = std::make_unique<gpu_program>(cs_compose);
    compose_program_.cache_uniforms();
    return compose_program_.is_valid();
}

auto gi_clipmap_compose_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/Clipmap Compose");
    if(!compose_program_.is_valid() || !params.surface_cache || !params.view_cache)
    {
        return false;
    }
    auto& surface_cache = *params.surface_cache;
    if(!surface_cache.is_enabled())
    {
        return false;
    }
    auto& view_cache = *params.view_cache;
    auto& clipmap = view_cache.get_clipmap_mutable();
    const auto& clipmap_gpu = view_cache.get_clipmap_gpu();
    if(!clipmap_gpu.is_valid())
    {
        return false;
    }
    const uint32_t dirty = clipmap.get_dirty_levels();
    if(dirty == 0)
    {
        // Nothing stale. Reporting true is still correct -- the caller must not fall back to the
        // CPU composer, which would recompose levels that are already current on the GPU.
        return true;
    }
    // An empty instance list means the whole scene left GI. The levels still have to be REWRITTEN
    // rather than left alone, or they keep occluding with geometry that is gone; the dispatch does
    // that correctly, writing the saturated "nothing reached this voxel" value everywhere.
    const auto& instances = surface_cache.get_instances();
    auto& atlas = surface_cache.get_atlas();
    const auto& clipmap_settings = clipmap.get_settings();
    const uint32_t resolution = clipmap_settings.resolution;
    const uint32_t groups = (resolution + compose_group_size - 1u) / compose_group_size;
    uint32_t composed = 0;
    for(uint32_t level = 0; level < global_sdf_clipmap::level_count; ++level)
    {
        if((dirty & (1u << level)) == 0u)
        {
            continue;
        }
        const auto& lvl = clipmap.get_level(level);
        if(!(lvl.voxel_size > 0.0f))
        {
            continue;
        }
        gfx::render_pass pass("GI/Clipmap Compose");
        compose_program_.program->begin();
        gfx::set_texture(compose_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
        gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
        gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
        gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
        gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
        gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
        // Stage 5 is the clipmap as an IMAGE here, where the tracing passes bind it as a sampler at
        // stage 4. Writing the level in place is what avoids a staging copy and the per-level
        // update_texture_3d the CPU path pays.
        gfx::set_image(5, clipmap_gpu.get_texture()->native_handle(), 0, gfx::access::Write, gfx::texture_format::R8);

        const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                     float(atlas.get_atlas_voxel_dim()),
                                     float(instances.size()),
                                     0.0f};
        gfx::set_uniform(compose_program_.u_sdf_params, sdf_params);
        gfx::set_uniform(compose_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
        gfx::set_uniform(compose_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
        // The reach is what the CPU composer seeds `nearest` with, and it must be the same value:
        // it is simultaneously the cheap-reject bound and the saturated output, so a mismatch
        // changes the composed bytes rather than merely the cost.
        const float reach = clipmap_settings.encode_range * lvl.voxel_size;
        const float compose_params[4] = {float(level), float(resolution), lvl.voxel_size, reach};
        gfx::set_uniform(compose_program_.u_clipmap_compose_params, compose_params);
        const float compose_origin[4] = {lvl.origin.x, lvl.origin.y, lvl.origin.z, 0.0f};
        gfx::set_uniform(compose_program_.u_clipmap_compose_origin, compose_origin);
        gfx::dispatch(pass.id, compose_program_.program->native_handle(), groups, groups, groups);
        compose_program_.program->end();
        ++composed;
    }
    // Consumed here rather than by the uploader: in GPU mode the uploader has no voxels to send, so
    // it would clear the mask before this pass ever saw it.
    clipmap.clear_dirty_levels();
    return composed > 0;
}

} // namespace unravel
