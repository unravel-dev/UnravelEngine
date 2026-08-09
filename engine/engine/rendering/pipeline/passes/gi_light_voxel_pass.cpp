#include "gi_light_voxel_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/shadow.h>

#include <graphics/graphics.h>

namespace unravel
{
namespace
{
/// Must match NUM_THREADS in cs_gi_light_voxels.sc.
constexpr uint32_t light_voxel_group_size = 64u;
} // namespace

auto gi_light_voxel_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_light_voxels.sc");
    program_.cache_uniforms();
    program_.program = std::make_unique<gpu_program>(cs);
    return program_.is_valid();
}

auto gi_light_voxel_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/Light Voxels");
    if(!program_.is_valid())
    {
        // Loudly, once: a program that failed to create leaves the light volume unwritten -
        // every downstream view then paints allocation garbage with a perfectly clean log,
        // which is how a whole backend's GI stayed silently broken (measured: Linux GL).
        if(!invalid_warning_emitted_)
        {
            invalid_warning_emitted_ = true;
            APPLOG_WARNING("[SurfaceCache] Light voxel compute program is not valid on this "
                           "backend; the light volume will never be written.");
        }
        return false;
    }
    if(!params.surface_cache || !params.view_cache)
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
    const auto& light_buffer = surface_cache.get_light_buffer();
    if(!clipmap_gpu.is_valid() || !clipmap_gpu.get_light_voxel_texture())
    {
        return false;
    }
    auto& atlas = surface_cache.get_atlas();
    const auto& instances = surface_cache.get_instances();
    gfx::render_pass pass("GI/Light Voxels");
    program_.program->begin();
    gfx::set_texture(program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
    gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
    gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
    gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
    gfx::set_texture(program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
    if(light_buffer.is_valid())
    {
        gfx::set_buffer(5, light_buffer.get_buffer(), gfx::access::Read);
    }
    gfx::set_buffer(6, clipmap_gpu.get_surface_list_buffer(), gfx::access::Read);
    // The count sits at stage 10 so the light-volume IMAGE can take stage 7: OpenGL
    // guarantees only eight image units (bindings 0-7).
    gfx::set_buffer(10, clipmap_gpu.get_surface_count_buffer(), gfx::access::Read);
    gfx::set_texture(program_.s_attr_albedo, 8, clipmap_gpu.get_attr_albedo_texture());
    gfx::set_texture(program_.s_attr_emissive, 9, clipmap_gpu.get_attr_emissive_texture());
    gfx::set_image_3d(7,
                      clipmap_gpu.get_light_voxel_texture()->native_handle(),
                      0,
                      gfx::access::Write,
                      gfx::texture_format::RGBA16F);
    gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
    gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
    const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                 float(atlas.get_atlas_voxel_dim()),
                                 float(instances.size()),
                                 0.0f};
    gfx::set_uniform(program_.u_sdf_params, sdf_params);
    gfx::set_uniform(program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
    gfx::set_uniform(program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    gfx::set_uniform(program_.u_sdf_clipmap_levels,
                     clipmap_gpu.get_level_params(),
                     global_sdf_clipmap::level_count);
    const float light_params[4] = {light_buffer.is_valid() ? float(light_buffer.get_light_count()) : 0.0f,
                                   0.0f,
                                   0.0f,
                                   0.0f};
    gfx::set_uniform(program_.u_gpu_light_params, light_params);
    // Shadow tracing wholly owned by gi_constants (Phase 8): no settings, one source.
    const float shadow_params[4] = {float(gi::GI_SHADOW_DISTANCE),
                                    float(gi::GI_SHADOW_NORMAL_BIAS_VOXELS),
                                    float(gi::GI_MESH_SDF_TRACE_RANGE),
                                    float(gi::GI_TRACE_MAX_STEPS)};
    gfx::set_uniform(program_.u_gi_shadow_params, shadow_params);
    const float shadow_params2[4] = {float(gi::GI_SHADOW_SURFACE_BIAS),
                                     float(gi::GI_SHADOW_RELAXATION),
                                     0.0f,
                                     float(gi::GI_SHADOW_RAY_START_VOXELS)};
    gfx::set_uniform(program_.u_gi_shadow_params2, shadow_params2);
    // Sun shadow-map tier (see gi_lighting.sh): cascade 0 of the sun's CSM answers sun
    // visibility for the voxels it covers; the traced field remains the answer beyond it.
    // VSM packs moment pairs rather than RGBA depth, so it falls back to tracing entirely.
    float sun_params[4] = {-1.0f, 0.0f, 0.0f, 0.0f};
    gfx::texture_handle sun_map = {bgfx::kInvalidHandle};
    if(params.sun_shadows != nullptr && params.sun_light_index >= 0 &&
       params.sun_shadows->get_depth_type() == shadow::PackDepth::RGBA)
    {
        sun_map = params.sun_shadows->get_rt_texture(0);
    }
    if(bgfx::isValid(sun_map))
    {
        gfx::set_uniform(program_.u_gi_sun_shadowmap_mtx, params.sun_shadows->get_shadow_map_matrix(0));
        sun_params[0] = float(params.sun_light_index);
        sun_params[1] = params.sun_shadows->get_shadow_map_bias();
        // One filter footprint inside the edge, mirroring the lighting shader's cascade
        // selection bounds, so a clamped tap never answers for a position outside the crop.
        sun_params[2] = 0.01f;
        gfx::set_texture(14, program_.s_gi_sun_shadowmap->native_handle(), sun_map);
    }
    else
    {
        // The stage must hold SOMETHING valid on backends that validate bindings; the tier is
        // disabled by the negative index, so the content is never read.
        gfx::set_texture(14,
                         program_.s_gi_sun_shadowmap->native_handle(),
                         default_textures::get().black_texture()->native_handle());
    }
    gfx::set_uniform(program_.u_gi_sun_shadowmap_params, sun_params);
    const uint32_t attr_resolution = clipmap_gpu.get_attr_resolution();
    const float voxel_params[4] = {float(attr_resolution),
                                   0.0f,
                                   float(params.frame),
                                   1.0f};
    gfx::set_uniform(program_.u_gi_light_voxel_params, voxel_params);
    const float camera[4] = {params.camera_position.x,
                             params.camera_position.y,
                             params.camera_position.z,
                             0.0f};
    gfx::set_uniform(program_.u_gi_light_voxel_camera, camera);
    // Bounce inputs: LAST frame's world probes. Absent (wrong resolution, first frames), the
    // ready flag stays zero and the shader takes direct light alone.
    const auto& view_clipmap = view_cache.get_clipmap();
    const bool probes_ready = clipmap_gpu.has_world_probes();
    const float base_spacing =
        view_clipmap.get_level(0).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
    const float probe_params[4] = {base_spacing, float(params.frame), probes_ready ? 1.0f : 0.0f, 0.0f};
    gfx::set_uniform(program_.u_gi_world_probe_params, probe_params);
    if(probes_ready)
    {
        gfx::set_texture(program_.s_world_probe_irradiance, 11, clipmap_gpu.get_world_probe_irradiance());
        gfx::set_texture(program_.s_world_probe_depth, 15, clipmap_gpu.get_world_probe_depth());
        gfx::set_uniform(program_.u_gi_world_probe_atlas, clipmap_gpu.get_world_probe_atlas_params());
    }
    else
    {
        // The probe samplers are ACTIVE regardless of the ready flag (a uniform branch
        // eliminates nothing), and OpenGL fails the whole dispatch when an unbound sampler's
        // unit-0 default collides with the 3D atlas bound there - which blacked out the light
        // voxels and with them the entire GI chain on that backend. The ready flag in
        // u_gi_world_probe_params gates what is actually read.
        const auto black = default_textures::get().black_texture();
        gfx::set_texture(program_.s_world_probe_irradiance, 11, black);
        gfx::set_texture(program_.s_world_probe_depth, 15, black);
    }
    // Every level's full segment, early-out beyond the per-level count. The counts live on the
    // GPU (the attribute dispatch appends them), so a tighter launch needs indirect args - a
    // measured optimisation, not a correctness matter.
    const uint32_t capacity = attr_resolution * attr_resolution * attr_resolution;
    const uint32_t total = capacity * global_sdf_clipmap::level_count;
    gfx::dispatch(pass.id,
                  program_.program->native_handle(),
                  (total + light_voxel_group_size - 1u) / light_voxel_group_size,
                  1,
                  1);
    program_.program->end();
    return true;
}

} // namespace unravel
