#include "gi_clipmap_compose_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/gi/gi_constants.h>

#include <logging/logging.h>

#include <cstring>

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
    compose_program_.cache_uniforms();
    compose_program_.program = std::make_unique<gpu_program>(cs_compose);
    auto cs_attributes = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_clipmap_attributes.sc");
    attributes_program_.cache_uniforms();
    attributes_program_.program = std::make_unique<gpu_program>(cs_attributes);
    auto cs_reset = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_surface_count_reset.sc");
    reset_program_.cache_uniforms();
    reset_program_.program = std::make_unique<gpu_program>(cs_reset);
    auto cs_texture_mean = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_texture_mean.sc");
    texture_mean_program_.cache_uniforms();
    texture_mean_program_.program = std::make_unique<gpu_program>(cs_texture_mean);
    auto cs_buffer_fill = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_buffer_fill.sc");
    fill_program_.cache_uniforms();
    fill_program_.program = std::make_unique<gpu_program>(cs_buffer_fill);
    auto cs_volume_clear = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_volume_clear.sc");
    volume_clear_program_.cache_uniforms();
    volume_clear_program_.program = std::make_unique<gpu_program>(cs_volume_clear);
    auto cs_atlas_clear = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_atlas_clear.sc");
    atlas_clear_program_.cache_uniforms();
    atlas_clear_program_.program = std::make_unique<gpu_program>(cs_atlas_clear);
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
    auto& clipmap_gpu = view_cache.get_clipmap_gpu_mutable();
    if(!clipmap_gpu.is_valid())
    {
        return false;
    }
    // The silent failure mode worth a loud line: a helper shader that never compiled leaves
    // the sentinels unseeded and the bounce albedo factor-only, with nothing else to notice.
    if((!texture_mean_program_.is_valid() || !fill_program_.is_valid() ||
        !volume_clear_program_.is_valid() || !atlas_clear_program_.is_valid()) &&
       !helper_warning_emitted_)
    {
        helper_warning_emitted_ = true;
        APPLOG_WARNING("[SurfaceCache] GI helper shaders not ready (texture mean valid: {}, "
                       "buffer fill valid: {}, volume clear valid: {}, atlas clear valid: {}). "
                       "If this persists, check their compile errors.",
                       texture_mean_program_.is_valid(),
                       fill_program_.is_valid(),
                       volume_clear_program_.is_valid(),
                       atlas_clear_program_.is_valid());
    }
    // One-time GPU seed of the compute-writable cell/cursor buffers (CPU updates on those are
    // forbidden): claim sentinels for the attribute and world-probe cell ids, zeroed append
    // cursors for the never-yet-composed levels.
    if(clipmap_gpu.needs_buffer_seed() && fill_program_.is_valid())
    {
        gfx::render_pass seed_pass("GI/Buffer Seed");
        const auto fill = [&](gfx::dynamic_index_buffer_handle target, uint32_t count, uint32_t value)
        {
            if(!bgfx::isValid(target) || count == 0)
            {
                return;
            }
            fill_program_.program->begin();
            gfx::set_buffer(0, target, gfx::access::Write);
            float fill_params[4] = {float(count), 0.0f, 0.0f, 0.0f};
            std::memcpy(&fill_params[1], &value, sizeof(value));
            gfx::set_uniform(fill_program_.u_gi_buffer_fill_params, fill_params);
            gfx::dispatch(seed_pass.id, fill_program_.program->native_handle(), (count + 63u) / 64u, 1, 1);
            fill_program_.program->end();
        };
        const uint32_t attr_resolution_seed = clipmap_gpu.get_attr_resolution();
        const uint32_t attr_cell_count =
            attr_resolution_seed * attr_resolution_seed * attr_resolution_seed * global_sdf_clipmap::level_count;
        fill(clipmap_gpu.get_attr_cells(), attr_cell_count, 0xFFFFFFFFu);
        fill(clipmap_gpu.get_world_probe_cells(), clipmap_gpu.get_world_probe_cell_count(), 0xFFFFFFFFu);
        // Only when the GPU owns the counts: the CPU-composed variant carries no COMPUTE_WRITE
        // (a compute fill would be invalid on it) and was zero-seeded from the CPU at creation.
        if(clipmap_gpu.is_composed_on_gpu())
        {
            fill(clipmap_gpu.get_surface_count_buffer(), global_sdf_clipmap::level_count, 0u);
        }
        clipmap_gpu.mark_seed_done();
    }
    // One-time zero of every texel the lazy claim-and-zero scheme never touches (see the clear
    // shaders): never-claimed light-voxel slots and never-traced probe tiles are still read
    // through the filtered paths, and fresh GPU memory is only zero where the driver zeroes
    // it - on Linux/Vulkan it is garbage that detonates the probe<->voxel loop. A separate
    // one-time job from the buffer seed, so one helper failing to compile cannot hold the
    // other's work hostage.
    if(clipmap_gpu.needs_texture_clear() && volume_clear_program_.is_valid() &&
       atlas_clear_program_.is_valid())
    {
        gfx::render_pass clear_pass("GI/Texture Clear");
        if(const auto& light_volume = clipmap_gpu.get_light_voxel_texture())
        {
            volume_clear_program_.program->begin();
            gfx::set_image(0,
                           light_volume->native_handle(),
                           0,
                           gfx::access::Write,
                           gfx::texture_format::RGBA16F);
            const float volume_params[4] = {float(light_volume->info.width),
                                            float(light_volume->info.height),
                                            float(light_volume->info.depth),
                                            0.0f};
            gfx::set_uniform(volume_clear_program_.u_gi_volume_clear_params, volume_params);
            gfx::dispatch(clear_pass.id,
                          volume_clear_program_.program->native_handle(),
                          (light_volume->info.width + 3u) / 4u,
                          (light_volume->info.height + 3u) / 4u,
                          (light_volume->info.depth + 3u) / 4u);
            volume_clear_program_.program->end();
        }
        if(clipmap_gpu.has_world_probes())
        {
            const auto& radiance = clipmap_gpu.get_world_probe_radiance();
            const auto& irradiance = clipmap_gpu.get_world_probe_irradiance();
            const auto& depth = clipmap_gpu.get_world_probe_depth();
            atlas_clear_program_.program->begin();
            gfx::set_image(0, radiance->native_handle(), 0, gfx::access::Write, gfx::texture_format::RGBA16F);
            gfx::set_image(1, irradiance->native_handle(), 0, gfx::access::Write, gfx::texture_format::RGBA16F);
            gfx::set_image(2, depth->native_handle(), 0, gfx::access::Write, gfx::texture_format::RG16F);
            const float atlas_params[4] = {float(radiance->info.width),
                                           float(radiance->info.height),
                                           float(irradiance->info.width),
                                           float(irradiance->info.height)};
            gfx::set_uniform(atlas_clear_program_.u_gi_atlas_clear_params, atlas_params);
            const uint32_t clear_w = std::max(radiance->info.width, irradiance->info.width);
            const uint32_t clear_h = std::max(radiance->info.height, irradiance->info.height);
            gfx::dispatch(clear_pass.id,
                          atlas_clear_program_.program->native_handle(),
                          (clear_w + 7u) / 8u,
                          (clear_h + 7u) / 8u,
                          1);
            atlas_clear_program_.program->end();
        }
        clipmap_gpu.mark_texture_clear_done();
    }
    // Pending texture-mean captures drain here regardless of dirty levels: each is a one-time
    // 1x1x1 dispatch, and the fingerprint flip it causes is what marks the affected levels
    // dirty on a LATER frame - so gating them on a dirty level would deadlock the queue.
    if(texture_mean_program_.is_valid())
    {
        const auto captures = surface_cache.take_texture_mean_captures(
            surface_cache_system::max_texture_mean_captures_per_frame);
        if(!captures.empty())
        {
            if(!capture_log_emitted_)
            {
                capture_log_emitted_ = true;
                APPLOG_INFO("[SurfaceCache] Texture mean captures flowing ({} this frame).",
                            captures.size());
            }
            gfx::render_pass mean_pass("GI/Texture Means");
            for(const auto& capture : captures)
            {
                if(!capture.texture || !capture.texture->is_valid())
                {
                    continue;
                }
                texture_mean_program_.program->begin();
                gfx::set_texture(texture_mean_program_.s_mean_source, 0, capture.texture);
                gfx::set_buffer(1, surface_cache.get_texture_mean_buffer(), gfx::access::ReadWrite);
                const uint32_t max_dim = math::max(uint32_t(capture.texture->info.width),
                                                   uint32_t(capture.texture->info.height));
                // The lod whose mip is about the shader's 8x8 sampling grid: log2(max dim) - 3,
                // floored at the base level. Hardware clamps to the tail for mipless textures.
                const float lod = math::max(std::log2(float(math::max(max_dim, 1u))) - 3.0f, 0.0f);
                const float mean_params[4] = {float(capture.slot), lod, 0.0f, 0.0f};
                gfx::set_uniform(texture_mean_program_.u_gi_texture_mean_params, mean_params);
                gfx::dispatch(mean_pass.id, texture_mean_program_.program->native_handle(), 1, 1, 1);
                texture_mean_program_.program->end();
            }
        }
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
    // Attributes compose AFTER every distance level is written: the attribute shader samples the
    // composed field (band + gradient gates), so its input must be this frame's voxels. Separate
    // dispatches also give the backend its transition point from image-write to sampled-read.
    if(attributes_program_.is_valid() && reset_program_.is_valid())
    {
        const uint32_t attr_resolution = clipmap_gpu.get_attr_resolution();
        const uint32_t attr_groups = (attr_resolution + compose_group_size - 1u) / compose_group_size;
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
            gfx::render_pass pass("GI/Clipmap Attributes");
            // The level's append cursor resets in the same view, immediately before the append
            // dispatch: submission order is the only ordering guarantee needed.
            reset_program_.program->begin();
            const float reset_params[4] = {float(level), 0.0f, 0.0f, 0.0f};
            gfx::set_uniform(reset_program_.u_surface_reset_params, reset_params);
            gfx::set_buffer(8, clipmap_gpu.get_surface_count_buffer(), gfx::access::ReadWrite);
            gfx::dispatch(pass.id, reset_program_.program->native_handle(), 1, 1, 1);
            reset_program_.program->end();
            attributes_program_.program->begin();
            gfx::set_texture(attributes_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
            gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
            gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
            gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
            gfx::set_texture(attributes_program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
            gfx::set_image(5,
                           clipmap_gpu.get_attr_albedo_texture()->native_handle(),
                           0,
                           gfx::access::Write,
                           gfx::texture_format::RGBA8);
            gfx::set_image(6,
                           clipmap_gpu.get_attr_emissive_texture()->native_handle(),
                           0,
                           gfx::access::Write,
                           gfx::texture_format::RGBA16F);
            // The surface list sits at stage 9 so the light-volume IMAGE can take stage 7:
            // OpenGL guarantees only eight image units (bindings 0-7).
            gfx::set_buffer(9, clipmap_gpu.get_surface_list_buffer(), gfx::access::ReadWrite);
            gfx::set_buffer(8, clipmap_gpu.get_surface_count_buffer(), gfx::access::ReadWrite);
            gfx::set_buffer(11, clipmap_gpu.get_attr_cells(), gfx::access::ReadWrite);
            gfx::set_buffer(10, surface_cache.get_texture_mean_buffer(), gfx::access::Read);
            const float light_voxel_params[4] = {float(attr_resolution), 0.0f, 0.0f, 1.0f};
            gfx::set_uniform(attributes_program_.u_gi_light_voxel_params, light_voxel_params);
            gfx::set_image(7,
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
            gfx::set_uniform(attributes_program_.u_sdf_params, sdf_params);
            gfx::set_uniform(attributes_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
            gfx::set_uniform(attributes_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
            gfx::set_uniform(attributes_program_.u_sdf_clipmap_levels,
                             clipmap_gpu.get_level_params(),
                             global_sdf_clipmap::level_count);
            const float attr_voxel_size = lvl.voxel_size * float(global_sdf_clipmap::attr_downsample);
            // Reach mirrors the CPU reference: the surface band plus one attribute voxel of
            // margin between the field's zero crossing and the voxel centre.
            const float attr_reach =
                (float(gi::GI_SURFACE_VOXEL_BAND) + 1.0f) * attr_voxel_size;
            const float attr_params[4] = {float(level),
                                          float(attr_resolution),
                                          attr_voxel_size,
                                          attr_reach};
            gfx::set_uniform(attributes_program_.u_clipmap_attr_params, attr_params);
            const float compose_origin[4] = {lvl.origin.x, lvl.origin.y, lvl.origin.z, 0.0f};
            gfx::set_uniform(attributes_program_.u_clipmap_compose_origin, compose_origin);
            gfx::dispatch(pass.id,
                          attributes_program_.program->native_handle(),
                          attr_groups,
                          attr_groups,
                          attr_groups);
            attributes_program_.program->end();
        }
    }
    // Consumed here rather than by the uploader: in GPU mode the uploader has no voxels to send, so
    // it would clear the mask before this pass ever saw it.
    clipmap.clear_dirty_levels();
    return composed > 0;
}

} // namespace unravel
