#include "sdf_debug_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_constants.h>

#include <graphics/graphics.h>

namespace unravel
{
namespace
{

} // namespace

auto sdf_debug_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_sdf_debug = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_sdf_debug.sc");
    debug_program_.cache_uniforms();
    debug_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_sdf_debug);
    return debug_program_.is_valid();
}

auto sdf_debug_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/SDF Debug Pass");
    if(!debug_program_.is_valid() || !params.output || !params.cam || !params.surface_cache ||
       !params.view_cache)
    {
        return false;
    }
    auto& surface_cache = *params.surface_cache;
    if(!surface_cache.is_enabled())
    {
        return false;
    }
    const auto& instances = surface_cache.get_instances();
    if(instances.empty())
    {
        return false;
    }
    auto& atlas = surface_cache.get_atlas();

    gfx::render_pass pass("GI/SDF Debug Pass");
    pass.bind(params.output.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());

    debug_program_.program->begin();

    gfx::set_texture(debug_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
    gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
    gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
    gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);

    // Global cascade. Bound even when unavailable so the sampler always has a valid texture;
    // u_sdf_clipmap_params.w tells the shader whether to consult it.
    const auto& clipmap = params.view_cache->get_clipmap();
    const auto& clipmap_gpu = params.view_cache->get_clipmap_gpu();
    const bool clipmap_ready = clipmap_gpu.is_valid();
    // EVERY sampler below is ACTIVE in every mode (the debug mode is a uniform branch, which
    // eliminates nothing), and OpenGL hard-fails the draw when an unbound sampler's unit 0
    // default collides with the 3D atlas at unit 0 ("program texture usage" -
    // GL_INVALID_OPERATION, the view renders black). So each stage always gets a texture of
    // the right dimensionality; the ready flags in the uniforms gate what is actually read.
    gfx::set_texture(debug_program_.s_attr_albedo,
                     8,
                     clipmap_gpu.get_attr_albedo_texture() ? clipmap_gpu.get_attr_albedo_texture()
                                                           : atlas.get_atlas_texture());
    gfx::set_texture(debug_program_.s_attr_emissive,
                     9,
                     clipmap_gpu.get_attr_emissive_texture() ? clipmap_gpu.get_attr_emissive_texture()
                                                             : atlas.get_atlas_texture());
    // Per-slot world-probe bookkeeping for the lattice view. Bound unconditionally for the same
    // reason every sampler above is: an unbound stage is a hard draw failure on OpenGL, not a
    // silently empty read.
    if(clipmap_gpu.has_world_probes())
    {
        // The claimed CELL, not the window count: the count is forced to zero unless
        // world_probe_jitter is on (off by default), so it carries no state to show.
        gfx::set_buffer(6, clipmap_gpu.get_world_probe_cells(), gfx::access::Read);
    }
    // Screen-probe records and the temporal moments, for the two SCREEN-SPACE views. The
    // probe buffer rides stage 14 (the trace uses 7, which the world-probe bookkeeping takes
    // here); both are bound unconditionally for the same OpenGL reason as the samplers above.
    if(bgfx::isValid(params.probes.buffer))
    {
        gfx::set_buffer(14, params.probes.buffer, gfx::access::Read);
    }
    {
        const float probe_params[4] = {float(params.probes.count_x),
                                       float(params.probes.count_y),
                                       params.probes.spacing,
                                       0.0f};
        gfx::set_uniform(debug_program_.u_gi_probe_params, probe_params);
        const float probe_screen[4] = {float(params.probes.trace_size.width),
                                       float(params.probes.trace_size.height),
                                       params.probes.trace_size.width > 0u
                                           ? 1.0f / float(params.probes.trace_size.width)
                                           : 0.0f,
                                       params.probes.trace_size.height > 0u
                                           ? 1.0f / float(params.probes.trace_size.height)
                                           : 0.0f};
        gfx::set_uniform(debug_program_.u_gi_probe_screen, probe_screen);
        // Only the WRITE half matters here: this view shows what the gather just produced.
        const float probe_temporal[4] = {0.0f, 0.0f, float(params.probes.write_offset), 0.0f};
        gfx::set_uniform(debug_program_.u_gi_probe_temporal, probe_temporal);
    }
    // Stage 7: D3D shares its 16 SRV registers between buffers and textures, and every other
    // one is taken - which is why the probe window-COUNT buffer is not bound (see the shader).
    // The temporal_cause view reads the FAST history through the same sampler: no stage is
    // free for a second screen texture, and the two views never run in the same frame.
    const auto& screen_history =
        params.settings.mode == debug_mode::temporal_cause ? params.fast : params.moments;
    gfx::set_texture(debug_program_.s_gi_moments,
                     7,
                     screen_history ? screen_history : default_textures::get().black_texture());

    // The temporal's dirty regions, packed exactly as the gather and the relight receive them
    // (surface_cache_system::pack_dirty_regions), so the view shows the set the lit path is
    // acting on rather than a second opinion. A zero count leaves every hit outside a region.
    {
        constexpr uint32_t max_regions = uint32_t(gi::GI_TEMPORAL_DIRTY_MAX_BOUNDS);
        float dirty_bounds[max_regions * 2u * 4u] = {};
        const uint32_t dirty_count = surface_cache.pack_dirty_regions(dirty_bounds, max_regions);
        // The margin the consumers use: one level-0 probe spacing, the reach of a small mover's
        // bounce pool.
        const float dirty_margin =
            clipmap.get_level(0).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
        const float dirty_params[4] = {float(dirty_count), math::max(dirty_margin, 1e-3f), 0.0f, 0.0f};
        gfx::set_uniform(debug_program_.u_gi_temporal_dirty, dirty_params);
        gfx::set_uniform(debug_program_.u_gi_temporal_bounds, dirty_bounds, uint16_t(2u * max_regions));
    }
    if(clipmap_gpu.get_light_voxel_texture())
    {
        gfx::set_texture(debug_program_.s_light_voxels, 10, clipmap_gpu.get_light_voxel_texture());
        const float light_voxel_params[4] = {float(clipmap_gpu.get_attr_resolution()), 0.0f, 0.0f, 1.0f};
        gfx::set_uniform(debug_program_.u_gi_light_voxel_params, light_voxel_params);
    }
    else
    {
        gfx::set_texture(debug_program_.s_light_voxels, 10, atlas.get_atlas_texture());
        const float light_voxel_params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(debug_program_.u_gi_light_voxel_params, light_voxel_params);
    }
    if(clipmap_gpu.has_world_probes())
    {
        gfx::set_texture(debug_program_.s_world_probe_irradiance, 11, clipmap_gpu.get_world_probe_irradiance());
        gfx::set_texture(debug_program_.s_world_probe_depth, 15, clipmap_gpu.get_world_probe_depth());
        const float base_spacing = params.view_cache->get_clipmap().get_level(0).voxel_size *
                                   float(gi::GI_WORLD_PROBE_DIVISOR);
        const float probe_params[4] = {base_spacing,
                                       0.0f,
                                       1.0f,
                                       params.settings.probe_visibility_variance_gate};
        gfx::set_uniform(debug_program_.u_gi_world_probe_params, probe_params);
        gfx::set_uniform(debug_program_.u_gi_world_probe_atlas, clipmap_gpu.get_world_probe_atlas_params());
    }
    else
    {
        const auto black = default_textures::get().black_texture();
        gfx::set_texture(debug_program_.s_world_probe_irradiance, 11, black);
        gfx::set_texture(debug_program_.s_world_probe_depth, 15, black);
        const float probe_params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(debug_program_.u_gi_world_probe_params, probe_params);
    }
    gfx::set_texture(debug_program_.s_sdf_clipmap,
                     4,
                     clipmap_ready ? clipmap_gpu.get_texture() : atlas.get_atlas_texture());
    gfx::set_uniform(debug_program_.u_sdf_clipmap_levels,
                     clipmap_gpu.get_level_params(),
                     global_sdf_clipmap::level_count);
    gfx::set_uniform(debug_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());

    // Scene lights, so a traced hit can be lit. Stage 5 is reserved by gi/gpu_lights.sh.
    const auto& light_buffer = surface_cache.get_light_buffer();
    if(light_buffer.is_valid())
    {
        gfx::set_buffer(5, light_buffer.get_buffer(), gfx::access::Read);
    }
    const float light_params[4] = {light_buffer.is_valid() ? float(light_buffer.get_light_count()) : 0.0f,
                                   0.0f,
                                   0.0f,
                                   0.0f};
    gfx::set_uniform(debug_program_.u_gpu_light_params, light_params);

    const float shadow_params[4] = {params.settings.shadow_distance,
                                    params.settings.shadow_normal_bias,
                                    params.settings.near_field_distance,
                                    float(params.settings.shadow_max_steps)};
    gfx::set_uniform(debug_program_.u_gi_shadow_params, shadow_params);

    const auto camera_position = params.cam->get_position();
    const float debug_camera[4] = {camera_position.x,
                                   camera_position.y,
                                   camera_position.z,
                                   0.0f};
    gfx::set_uniform(debug_program_.u_gi_debug_camera, debug_camera);

    const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                 float(atlas.get_atlas_voxel_dim()),
                                 float(instances.size()),
                                 float(surface_cache.get_emitters().size())};
    gfx::set_uniform(debug_program_.u_sdf_params, sdf_params);
    gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
    gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
    gfx::set_uniform(debug_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);

    const float debug_params[4] = {float(params.settings.max_steps),
                                   params.settings.max_distance,
                                   float(static_cast<uint8_t>(params.settings.mode)),
                                   params.settings.surface_bias};
    gfx::set_uniform(debug_program_.u_sdf_debug_params, debug_params);

    const float debug_params2[4] = {params.settings.near_field_distance,
                                    params.settings.step_relaxation,
                                    params.settings.view_scale,
                                    0.0f};
    gfx::set_uniform(debug_program_.u_sdf_debug_params2, debug_params2);

    // Alpha blended so the visualisation composites over the shaded scene: rays that hit
    // nothing write alpha 0 and leave the frame untouched.
    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB |
                   BGFX_STATE_BLEND_ALPHA);
    gfx::submit(pass.id, debug_program_.program->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);
    debug_program_.program->end();
    gfx::discard();
    return true;
}

void sdf_debug_pass::release_resources()
{
}

} // namespace unravel
