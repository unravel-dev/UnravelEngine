#include "global_sdf_clipmap_gpu.h"

#include <engine/profiler/profiler.h>

#include <logging/logging.h>

namespace unravel
{

auto global_sdf_clipmap_gpu::init(uint32_t resolution) -> bool
{
    shutdown();
    resolution_ = resolution;
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
    APPLOG_INFO("[SurfaceCache] Global SDF clipmap ready: {} levels of {}^3 ({} KB).",
                global_sdf_clipmap::level_count,
                resolution,
                (size_t(resolution) * resolution * depth) / 1024);
    return true;
}

void global_sdf_clipmap_gpu::shutdown()
{
    texture_.reset();
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
    }
    clipmap.clear_dirty_levels();
}

} // namespace unravel
