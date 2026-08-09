#include "global_sdf_clipmap_gpu.h"

#include <engine/profiler/profiler.h>

#include <logging/logging.h>

#include <bx/math.h>

#include <vector>

namespace unravel
{

auto global_sdf_clipmap_gpu::init(uint32_t resolution, bool compose_on_gpu) -> bool
{
    shutdown();
    resolution_ = resolution;
    compose_on_gpu_ = compose_on_gpu;
    const uint32_t depth = resolution * global_sdf_clipmap::level_count;
    if(depth > 2048u)
    {
        APPLOG_ERROR("[SurfaceCache] Clipmap resolution {} needs a {}-deep texture, over the 2048 limit.",
                     resolution,
                     depth);
        resolution_ = 0;
        return false;
    }
    // Trilinear, clamped. The sampler is only ever addressed with a half-voxel margin inside a
    // level, so filtering cannot reach across the slab boundary into the next cascade.
    //
    // COMPUTE_WRITE so cs_gi_clipmap_compose can write the voxels in place. It costs nothing when
    // composition runs on the CPU -- the texture is still updated by update_texture_3d -- and
    // without it the image binding silently produces no writes rather than an error.
    const uint64_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP |
                           BGFX_TEXTURE_COMPUTE_WRITE;
    texture_ = std::make_shared<gfx::texture>(static_cast<uint16_t>(resolution),
                                              static_cast<uint16_t>(resolution),
                                              static_cast<uint16_t>(depth),
                                              false,
                                              gfx::texture_format::R8,
                                              flags);
    if(!texture_ || !texture_->is_valid())
    {
        APPLOG_ERROR("[SurfaceCache] Failed to create the {}x{}x{} clipmap texture.",
                     resolution,
                     resolution,
                     depth);
        texture_.reset();
        resolution_ = 0;
        return false;
    }
    // Attribute voxels (GI v2 plan 3.1): albedo + emissive at half resolution, and the
    // surface-voxel list segments + cursors the light-voxel update consumes. Created alongside
    // the distance volume because they recompose with it and share its lifetime.
    const uint32_t attr_resolution = get_attr_resolution();
    const uint32_t attr_depth = attr_resolution * global_sdf_clipmap::level_count;
    attr_albedo_texture_ = std::make_shared<gfx::texture>(static_cast<uint16_t>(attr_resolution),
                                                          static_cast<uint16_t>(attr_resolution),
                                                          static_cast<uint16_t>(attr_depth),
                                                          false,
                                                          gfx::texture_format::RGBA8,
                                                          flags);
    attr_emissive_texture_ = std::make_shared<gfx::texture>(static_cast<uint16_t>(attr_resolution),
                                                            static_cast<uint16_t>(attr_resolution),
                                                            static_cast<uint16_t>(attr_depth),
                                                            false,
                                                            gfx::texture_format::RGBA16F,
                                                            flags);
    // Six face slabs per level (gi_light_voxels.sh layout). At the runtime default this is
    // 64 * 4 * 6 = 1536 deep - inside the 2048 texture limit the distance volume already guards.
    const uint32_t light_depth = attr_resolution * global_sdf_clipmap::level_count * 6u;
    if(light_depth > 2048u)
    {
        APPLOG_ERROR("[SurfaceCache] Light voxel volume needs a {}-deep texture, over the 2048 limit.",
                     light_depth);
        shutdown();
        return false;
    }
    // REPEAT in u/v (bgfx default, so no clamp flags): the volume is TOROIDAL in xy and the
    // filtered read relies on hardware wrap to interpolate across the seam onto the far edge,
    // which holds the world-adjacent cells. W stays clamped - z packs (level, face) slabs and
    // the reader lerps its two z taps manually at texel centres.
    const uint64_t light_flags = BGFX_SAMPLER_W_CLAMP | BGFX_TEXTURE_COMPUTE_WRITE;
    light_voxel_texture_ = std::make_shared<gfx::texture>(static_cast<uint16_t>(attr_resolution),
                                                          static_cast<uint16_t>(attr_resolution),
                                                          static_cast<uint16_t>(light_depth),
                                                          false,
                                                          gfx::texture_format::RGBA16F,
                                                          light_flags);
    const uint32_t segment = attr_resolution * attr_resolution * attr_resolution;
    // The surface list/count flags follow the COMPOSER: the GPU compose pass writes them from
    // compute (needs COMPUTE_WRITE, and bgfx forbids CPU updates on such buffers), while the
    // CPU composer uploads them with gfx::update (which requires COMPUTE_WRITE absent). The
    // consumers only ever read them from compute, which both flag sets allow.
    const uint64_t surface_flags =
        (compose_on_gpu_ ? BGFX_BUFFER_COMPUTE_READ_WRITE : BGFX_BUFFER_COMPUTE_READ) |
        BGFX_BUFFER_INDEX32;
    surface_list_ = gfx::create_dynamic_index_buffer(segment * global_sdf_clipmap::level_count,
                                                     surface_flags);
    surface_count_ = gfx::create_dynamic_index_buffer(global_sdf_clipmap::level_count, surface_flags);
    attr_cells_ = gfx::create_dynamic_index_buffer(segment * global_sdf_clipmap::level_count,
                                                   BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
    // Sentinel cell ids (no real cell packs to ~0u, so every slot claims and zeroes its light
    // texels on first use) and the zeroed cursors are seeded ON THE GPU by the compose pass:
    // these buffers are compute-writable, and bgfx forbids - in debug - CPU updates on those.
    // The seed runs regardless of the composer - the light/world-probe passes write the cell
    // buffers in both modes.
    needs_buffer_seed_ = true;
    needs_texture_clear_ = true;
    // A CPU-composed count buffer cannot be seeded by that dispatch (no COMPUTE_WRITE), and it
    // does not need to be: it is CPU-writable, so the zeroes upload right here. Uncomposed
    // levels then read an empty list rather than allocation garbage.
    if(!compose_on_gpu_ && bgfx::isValid(surface_count_))
    {
        const std::array<uint32_t, global_sdf_clipmap::level_count> zero_counts{};
        gfx::update(surface_count_, 0, gfx::copy(zero_counts.data(), sizeof(zero_counts)));
    }
    if(!attr_albedo_texture_ || !attr_albedo_texture_->is_valid() || !attr_emissive_texture_ ||
       !attr_emissive_texture_->is_valid() || !light_voxel_texture_ || !light_voxel_texture_->is_valid() ||
       !bgfx::isValid(surface_list_) || !bgfx::isValid(surface_count_) || !bgfx::isValid(attr_cells_))
    {
        APPLOG_ERROR("[SurfaceCache] Failed to create the clipmap attribute resources.");
        shutdown();
        return false;
    }
    // A level's cursor is only meaningful once that level has composed; the compose pass's seed
    // dispatch zeroes them all so a consumer reading an as-yet-uncomposed level sees an empty
    // list rather than allocation garbage.
    // World probes (GI v2 plan 3.3): only when the shader's hardcoded axis matches this
    // resolution's derivation - the trace/convolve group layouts bake the axis in.
    if(resolution / 16u + 1u == world_probe_axis)
    {
        const uint32_t tile_grid_x = world_probe_axis * world_probe_axis;
        const uint32_t tile_grid_y = world_probe_axis * global_sdf_clipmap::level_count;
        const uint32_t radiance_w = tile_grid_x * 16u;
        const uint32_t radiance_h = tile_grid_y * 16u;
        const uint32_t gutter_w = tile_grid_x * 10u;
        const uint32_t gutter_h = tile_grid_y * 10u;
        world_probe_radiance_ = std::make_shared<gfx::texture>(static_cast<uint16_t>(radiance_w),
                                                               static_cast<uint16_t>(radiance_h),
                                                               false,
                                                               1,
                                                               gfx::texture_format::RGBA16F,
                                                               flags);
        world_probe_irradiance_ = std::make_shared<gfx::texture>(static_cast<uint16_t>(gutter_w),
                                                                 static_cast<uint16_t>(gutter_h),
                                                                 false,
                                                                 1,
                                                                 gfx::texture_format::RGBA16F,
                                                                 flags);
        world_probe_depth_ = std::make_shared<gfx::texture>(static_cast<uint16_t>(gutter_w),
                                                            static_cast<uint16_t>(gutter_h),
                                                            false,
                                                            1,
                                                            gfx::texture_format::RG16F,
                                                            flags);
        const uint32_t probe_count =
            world_probe_axis * world_probe_axis * world_probe_axis * global_sdf_clipmap::level_count;
        world_probe_cells_ = gfx::create_dynamic_index_buffer(probe_count,
                                                              BGFX_BUFFER_COMPUTE_READ_WRITE |
                                                                  BGFX_BUFFER_INDEX32);
        world_probe_atlas_params_[0] = 1.0f / float(gutter_w);
        world_probe_atlas_params_[1] = 1.0f / float(gutter_h);
        world_probe_atlas_params_[2] = float(gutter_w);
        world_probe_atlas_params_[3] = float(gutter_h);
        if(!world_probe_radiance_ || !world_probe_radiance_->is_valid() || !world_probe_irradiance_ ||
           !world_probe_irradiance_->is_valid() || !world_probe_depth_ || !world_probe_depth_->is_valid() ||
           !bgfx::isValid(world_probe_cells_))
        {
            APPLOG_ERROR("[SurfaceCache] Failed to create the world probe resources.");
            shutdown();
            return false;
        }
        // Sentinel cell ids (every slot claims and zeroes its strata on first trace) are seeded
        // by the compose pass's GPU fill - see needs_buffer_seed.
        world_probe_cell_count_ = probe_count;
    }
    else
    {
        APPLOG_WARNING("[SurfaceCache] World probes disabled: resolution {} does not derive the"
                       " compiled probe axis {}.",
                       resolution,
                       world_probe_axis);
    }
    APPLOG_INFO("[SurfaceCache] Global SDF clipmap ready: {} levels of {}^3 + {}^3 attributes ({} KB).",
                global_sdf_clipmap::level_count,
                resolution,
                attr_resolution,
                (size_t(resolution) * resolution * depth +
                 size_t(attr_resolution) * attr_resolution * attr_depth * 12u) /
                    1024);
    return true;
}

void global_sdf_clipmap_gpu::shutdown()
{
    texture_.reset();
    attr_albedo_texture_.reset();
    attr_emissive_texture_.reset();
    light_voxel_texture_.reset();
    world_probe_radiance_.reset();
    world_probe_irradiance_.reset();
    world_probe_depth_.reset();
    if(bgfx::isValid(world_probe_cells_))
    {
        gfx::destroy(world_probe_cells_);
        world_probe_cells_ = gfx::dynamic_index_buffer_handle{bgfx::kInvalidHandle};
    }
    world_probe_cell_count_ = 0;
    needs_buffer_seed_ = false;
    if(bgfx::isValid(attr_cells_))
    {
        gfx::destroy(attr_cells_);
        attr_cells_ = gfx::dynamic_index_buffer_handle{bgfx::kInvalidHandle};
    }
    world_probe_atlas_params_.fill(0.0f);
    if(bgfx::isValid(surface_list_))
    {
        gfx::destroy(surface_list_);
        surface_list_ = gfx::dynamic_index_buffer_handle{bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(surface_count_))
    {
        gfx::destroy(surface_count_);
        surface_count_ = gfx::dynamic_index_buffer_handle{bgfx::kInvalidHandle};
    }
    resolution_ = 0;
    level_params_.fill(0.0f);
    // Zeroing this clears the "cascade is resident" flag in w, which is what stops a consumer
    // from sampling a texture that is no longer there. upload() is the only writer and it
    // early-outs while invalid, so without this the flag would survive a shutdown.
    sampling_params_.fill(0.0f);
}

void global_sdf_clipmap_gpu::upload(global_sdf_clipmap& clipmap)
{
    APP_SCOPE_PERF("GI/Clipmap/Upload");
    if(!is_valid())
    {
        return;
    }
    // Level parameters are refreshed unconditionally: an origin can change without the voxels
    // needing a re-upload only if the level was skipped for budget, and in that case the
    // parameters must still describe the contents actually resident.
    for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
    {
        const auto& lvl = clipmap.get_level(i);
        float* params = level_params_.data() + size_t(i) * 4;
        params[0] = lvl.origin.x;
        params[1] = lvl.origin.y;
        params[2] = lvl.origin.z;
        params[3] = lvl.voxel_size;
    }
    // Refreshed alongside the level parameters and for the same reason: these describe how to
    // sample what is actually resident, so they must not lag a level that was skipped for budget.
    const auto& clipmap_settings = clipmap.get_settings();
    sampling_params_[0] = float(resolution_);
    sampling_params_[1] = clipmap_settings.blend_voxels;
    sampling_params_[2] = clipmap_settings.encode_range;
    sampling_params_[3] = 1.0f;
    // When the voxels are composed by a dispatch there is nothing to send, and the dirty mask is
    // NOT ours to clear -- gi_clipmap_compose_pass consumes it to decide which levels to compose.
    // Clearing it here would leave the levels marked clean and never composed at all: a cascade
    // frozen at whatever it last held, which traces perfectly and is simply wrong.
    //
    // The parameter refresh above still has to happen, because origins move whether or not the
    // voxels are rewritten here.
    if(clipmap.get_settings().compose_on_gpu)
    {
        return;
    }
    const uint32_t dirty = clipmap.get_dirty_levels();
    if(dirty == 0)
    {
        return;
    }
    for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
    {
        if((dirty & (1u << i)) == 0u)
        {
            continue;
        }
        const auto& lvl = clipmap.get_level(i);
        if(lvl.voxels.size() != size_t(resolution_) * resolution_ * resolution_)
        {
            continue;
        }
        gfx::update_texture_3d(texture_->native_handle(),
                               0,
                               0,
                               0,
                               static_cast<uint16_t>(i * resolution_),
                               static_cast<uint16_t>(resolution_),
                               static_cast<uint16_t>(resolution_),
                               static_cast<uint16_t>(resolution_),
                               gfx::copy(lvl.voxels.data(), uint32_t(lvl.voxels.size())));
        // Attributes ride along: the CPU composer produced them with the distance voxels, and a
        // consumer cannot tell which composer wrote what it samples, so the two paths must ship
        // the same set of resources.
        const uint32_t attr_resolution = get_attr_resolution();
        const size_t attr_count = size_t(attr_resolution) * attr_resolution * attr_resolution;
        if(lvl.attr_albedo.size() == attr_count && lvl.attr_emissive.size() == attr_count)
        {
            gfx::update_texture_3d(attr_albedo_texture_->native_handle(),
                                   0,
                                   0,
                                   0,
                                   static_cast<uint16_t>(i * attr_resolution),
                                   static_cast<uint16_t>(attr_resolution),
                                   static_cast<uint16_t>(attr_resolution),
                                   static_cast<uint16_t>(attr_resolution),
                                   gfx::copy(lvl.attr_albedo.data(), uint32_t(attr_count * sizeof(uint32_t))));
            std::vector<uint16_t> half_emissive(attr_count * 4u, 0u);
            for(size_t v = 0; v < attr_count; ++v)
            {
                half_emissive[v * 4u + 0u] = bx::halfFromFloat(lvl.attr_emissive[v].x);
                half_emissive[v * 4u + 1u] = bx::halfFromFloat(lvl.attr_emissive[v].y);
                half_emissive[v * 4u + 2u] = bx::halfFromFloat(lvl.attr_emissive[v].z);
            }
            gfx::update_texture_3d(attr_emissive_texture_->native_handle(),
                                   0,
                                   0,
                                   0,
                                   static_cast<uint16_t>(i * attr_resolution),
                                   static_cast<uint16_t>(attr_resolution),
                                   static_cast<uint16_t>(attr_resolution),
                                   static_cast<uint16_t>(attr_resolution),
                                   gfx::copy(half_emissive.data(), uint32_t(half_emissive.size() * sizeof(uint16_t))));
            const uint32_t count = uint32_t(lvl.attr_surface_list.size());
            gfx::update(surface_count_, i, gfx::copy(&count, sizeof(count)));
            if(count > 0u)
            {
                gfx::update(surface_list_,
                            i * uint32_t(attr_count),
                            gfx::copy(lvl.attr_surface_list.data(), count * uint32_t(sizeof(uint32_t))));
            }
        }
    }
    clipmap.clear_dirty_levels();
}

} // namespace unravel
