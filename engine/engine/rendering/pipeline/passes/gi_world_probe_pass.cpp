#include "gi_world_probe_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_constants.h>

#include <graphics/graphics.h>

#include <cmath>

namespace unravel
{

auto gi_world_probe_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs_trace = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_world_probe_trace.sc");
    trace_program_.program = std::make_unique<gpu_program>(cs_trace);
    trace_program_.cache_uniforms();
    auto cs_convolve = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_world_probe_convolve.sc");
    convolve_program_.program = std::make_unique<gpu_program>(cs_convolve);
    convolve_program_.cache_uniforms();
    return is_valid();
}

auto gi_world_probe_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/World Probes");
    if(!is_valid() || !params.surface_cache || !params.view_cache)
    {
        return false;
    }
    auto& surface_cache = *params.surface_cache;
    if(!surface_cache.is_enabled())
    {
        return false;
    }
    auto& view_cache = *params.view_cache;
    const auto& clipmap_gpu = view_cache.get_clipmap_gpu();
    const auto& clipmap = view_cache.get_clipmap();
    if(!clipmap_gpu.is_valid() || !clipmap_gpu.has_world_probes())
    {
        return false;
    }
    auto& atlas = surface_cache.get_atlas();
    const auto& instances = surface_cache.get_instances();
    constexpr uint32_t axis = global_sdf_clipmap_gpu::world_probe_axis;
    const uint32_t probe_count = axis * axis * axis * global_sdf_clipmap::level_count;
    // Rays reach the whole traceable world: the outermost cascade's half extent, the same
    // derivation the gather's max distance uses (a longer promise would be fiction).
    const float trace_reach = clipmap.get_level_extent(global_sdf_clipmap::level_count - 1u) * 0.5f;
    // Window centres: the camera's probe cell per level, in whole cells (the snap is what makes
    // rotation and sub-cell translation no-ops).
    float window[global_sdf_clipmap::level_count * 4] = {};
    for(uint32_t level = 0; level < global_sdf_clipmap::level_count; ++level)
    {
        const float spacing =
            clipmap.get_level(level).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
        const float safe_spacing = spacing > 0.0f ? spacing : 1.0f;
        window[level * 4 + 0] = std::floor(params.camera_position.x / safe_spacing + 0.5f);
        window[level * 4 + 1] = std::floor(params.camera_position.y / safe_spacing + 0.5f);
        window[level * 4 + 2] = std::floor(params.camera_position.z / safe_spacing + 0.5f);
        window[level * 4 + 3] = trace_reach;
    }
    const float base_spacing =
        clipmap.get_level(0).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
    // Change fast window (plan section 8): a changed light set OR changed scene content
    // doubles the strata per frame for one full window, so the bounce chain reacts at twice
    // the speed exactly while something is changing and costs nothing while the scene is
    // still. The content epoch fires on geometry/material changes (a door closing) and is
    // scroll-suppressed, so camera motion alone never pins the fast path.
    if(params.light_hash != last_light_hash_)
    {
        last_light_hash_ = params.light_hash;
        fast_frames_ = gi::GI_WORLD_PROBE_WINDOW;
    }
    if(clipmap.get_content_epoch() != last_content_epoch_)
    {
        last_content_epoch_ = clipmap.get_content_epoch();
        fast_frames_ = gi::GI_WORLD_PROBE_WINDOW;
    }
    const uint32_t strata_per_frame = fast_frames_ > 0 ? 2u : 1u;
    if(fast_frames_ > 0)
    {
        --fast_frames_;
    }
    const float probe_params[4] = {base_spacing, float(params.frame), 1.0f, float(strata_per_frame)};
    const auto env_sh =
        params.irradiance_sh ? params.irradiance_sh : default_textures::get().black_texture();
    {
        gfx::render_pass pass("GI/World Probe Trace");
        trace_program_.program->begin();
        gfx::set_texture(trace_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
        gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
        gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
        gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
        gfx::set_texture(trace_program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
        gfx::set_image(5,
                       clipmap_gpu.get_world_probe_radiance()->native_handle(),
                       0,
                       gfx::access::Write,
                       gfx::texture_format::RGBA16F);
        gfx::set_buffer(6, clipmap_gpu.get_world_probe_cells(), gfx::access::ReadWrite);
        gfx::set_texture(trace_program_.s_light_voxels, 10, clipmap_gpu.get_light_voxel_texture());
        gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
        gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
        gfx::set_texture(trace_program_.s_gi_env_sh, 14, env_sh);
        const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                     float(atlas.get_atlas_voxel_dim()),
                                     float(instances.size()),
                                     0.0f};
        gfx::set_uniform(trace_program_.u_sdf_params, sdf_params);
        gfx::set_uniform(trace_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
        gfx::set_uniform(trace_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
        gfx::set_uniform(trace_program_.u_sdf_clipmap_levels,
                         clipmap_gpu.get_level_params(),
                         global_sdf_clipmap::level_count);
        const float light_voxel_params[4] = {float(clipmap_gpu.get_attr_resolution()), 0.0f, 0.0f, 1.0f};
        gfx::set_uniform(trace_program_.u_gi_light_voxel_params, light_voxel_params);
        gfx::set_uniform(trace_program_.u_gi_world_probe_params, probe_params);
        gfx::set_uniform(trace_program_.u_gi_world_probe_window, window, global_sdf_clipmap::level_count);
        gfx::dispatch(pass.id, trace_program_.program->native_handle(), probe_count, 1, 1);
        trace_program_.program->end();
    }
    {
        gfx::render_pass pass("GI/World Probe Convolve");
        convolve_program_.program->begin();
        gfx::set_texture(convolve_program_.s_world_probe_radiance,
                         11,
                         clipmap_gpu.get_world_probe_radiance());
        gfx::set_image(5,
                       clipmap_gpu.get_world_probe_irradiance()->native_handle(),
                       0,
                       gfx::access::Write,
                       gfx::texture_format::RGBA16F);
        gfx::set_image(6,
                       clipmap_gpu.get_world_probe_depth()->native_handle(),
                       0,
                       gfx::access::Write,
                       gfx::texture_format::RG16F);
        gfx::set_uniform(convolve_program_.u_gi_world_probe_params, probe_params);
        gfx::dispatch(pass.id, convolve_program_.program->native_handle(), probe_count, 1, 1);
        convolve_program_.program->end();
    }
    return true;
}

} // namespace unravel
