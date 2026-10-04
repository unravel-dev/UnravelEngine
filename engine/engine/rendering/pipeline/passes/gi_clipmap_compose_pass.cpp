#include "gi_clipmap_compose_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/gi/gi_constants.h>

#include <logging/logging.h>

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
    return compose_program_.is_valid();
}

auto gi_clipmap_compose_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/Clipmap Compose");
    if(!compose_program_.is_valid())
    {
        // Loudly, once: the CPU composer takes over, which costs main-thread time whenever a level re-snaps.
        if(!invalid_warning_emitted_)
        {
            invalid_warning_emitted_ = true;
            APPLOG_WARNING("[SurfaceCache] Clipmap compose compute program is not valid on "
                           "this backend; composing on the CPU.");
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
    auto& clipmap = view_cache.get_clipmap_mutable();
    const auto& clipmap_gpu = view_cache.get_clipmap_gpu();
    if(!clipmap_gpu.is_valid())
    {
        return false;
    }
    // STABLE VIEW LAYOUT. These three views exist every frame, in this order, empty when the
    // frame has nothing to compose. bgfx reports a view's GPU time from an OLDER frame's
    // timestamp query for the same VIEW ID under the current frame's name (renderer.h
    // viewStats), so a layout that only gains these views on dirty frames would shift every
    // later GI row's timing by several rows for a frame or two after each change, attributing
    // one pass's time to another in every profile taken under motion. Touched-but-empty views
    // are close to free and keep the per-pass rows trustworthy exactly where they matter, on
    // the moving frames.
    gfx::render_pass scroll_copy_pass("GI/Clipmap Scroll Copy");
    gfx::render_pass scroll_place_pass("GI/Clipmap Scroll Place");
    gfx::render_pass compose_pass("GI/Clipmap Compose");
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
        compose_level_voxels(clipmap, clipmap_gpu, surface_cache, level, scroll_copy_pass, scroll_place_pass, compose_pass);
        ++composed;
    }
    // Consumed here rather than by the uploader: in GPU mode the uploader has no voxels to send, so
    // it would clear the mask before this pass ever saw it.
    clipmap.clear_dirty_levels();
    return composed > 0;
}

void gi_clipmap_compose_pass::compose_level_voxels(const global_sdf_clipmap& clipmap,
                                                   const global_sdf_clipmap_gpu& clipmap_gpu,
                                                   surface_cache_system& surface_cache,
                                                   uint32_t level,
                                                   gfx::render_pass& scroll_copy_pass,
                                                   gfx::render_pass& scroll_place_pass,
                                                   gfx::render_pass& compose_pass)
{
    const auto& lvl = clipmap.get_level(level);
    const uint32_t resolution = clipmap.get_settings().resolution;
    global_sdf_clipmap::voxel_box overlap;
    std::array<global_sdf_clipmap::voxel_box, 3> exposed;
    uint32_t exposed_count = 0;
    // SCROLL-ONLY (level::scroll_only): the overlap of the old and new windows holds exactly
    // the bytes a recompose would write, so it is moved - out to the scratch slab and back
    // in at its new position, two blits, since a blit cannot shift voxels within one
    // texture - and only the exposed slabs are composed. The coverage moves the same way at
    // its downsample (the window snaps by whole coverage texels). A scratch that failed to
    // allocate falls back to composing the whole level.
    const scroll_volume distance{&scroll_scratch_[level], &clipmap_gpu.get_texture(), 1u};
    const scroll_volume coverage{&coverage_scroll_scratch_[level],
                                 &clipmap_gpu.get_coverage_texture(),
                                 global_sdf_clipmap_gpu::coverage_downsample};
    if(lvl.scroll_only)
    {
        exposed_count = global_sdf_clipmap::compute_scroll_boxes(lvl.scroll_shift, resolution, overlap, exposed);
    }
    if(exposed_count > 0 && (!ensure_scroll_scratch(distance, resolution) || !ensure_scroll_scratch(coverage, resolution)))
    {
        exposed_count = 0;
    }
    if(exposed_count > 0)
    {
        blit_scroll_overlap(distance, resolution, level, overlap, lvl.scroll_shift, scroll_copy_pass, scroll_place_pass);
        blit_scroll_overlap(coverage, resolution, level, overlap, lvl.scroll_shift, scroll_copy_pass, scroll_place_pass);
        for(uint32_t box = 0; box < exposed_count; ++box)
        {
            dispatch_compose_box(compose_pass, clipmap, clipmap_gpu, surface_cache, level, exposed[box]);
        }
        return;
    }
    global_sdf_clipmap::voxel_box whole;
    whole.min = math::ivec3(0);
    whole.size = math::ivec3(int(resolution));
    dispatch_compose_box(compose_pass, clipmap, clipmap_gpu, surface_cache, level, whole);
}

auto gi_clipmap_compose_pass::ensure_scroll_scratch(const scroll_volume& target, uint32_t resolution) -> bool
{
    const uint32_t size = resolution / target.downsample;
    auto& scratch = *target.scratch;
    const bool stale = !scratch || !scratch->is_valid() || scratch->info.width != size || scratch->info.depth != size;
    if(stale)
    {
        scratch = std::make_shared<gfx::texture>(static_cast<uint16_t>(size),
                                                 static_cast<uint16_t>(size),
                                                 static_cast<uint16_t>(size),
                                                 false,
                                                 bgfx::TextureFormat::R8,
                                                 BGFX_TEXTURE_BLIT_DST);
    }
    return scratch && scratch->is_valid();
}

void gi_clipmap_compose_pass::blit_scroll_overlap(const scroll_volume& target,
                                                  uint32_t resolution,
                                                  uint32_t level,
                                                  const global_sdf_clipmap::voxel_box& overlap,
                                                  const math::ivec3& shift,
                                                  gfx::render_pass& copy_pass,
                                                  gfx::render_pass& place_pass)
{
    const uint32_t downsample = target.downsample;
    const auto size16 = static_cast<uint16_t>(resolution / downsample);
    const auto slab_z = static_cast<uint16_t>(level * resolution / downsample);
    const auto& scratch = *target.scratch;
    const auto& volume = *target.volume;
    // Blits run at the start of their view, so the copy out and the placement back each take a
    // view of their own, ahead of the compose dispatches.
    bgfx::blit(copy_pass.id,
               bgfx::TextureRegion{.handle = scratch->native_handle(), .width = size16, .height = size16, .depth = size16},
               bgfx::TextureRegion{.handle = volume->native_handle(),
                                   .z = slab_z,
                                   .width = size16,
                                   .height = size16,
                                   .depth = size16});
    // New-window voxel v came from old-window voxel v + shift. Every overlap extent is at least one
    // snap (compute_scroll_boxes reports no scroll otherwise), a whole number of texels at any
    // downsample, which matters because a zero blit extent means "the rest of the mip".
    // Component-wise: glm's SIMD integer vector division needs an intrinsic this toolchain lacks.
    const auto scale_down = [downsample](const math::ivec3& v) -> math::ivec3
    {
        const int d = int(downsample);
        return {v.x / d, v.y / d, v.z / d};
    };
    const math::ivec3 source = scale_down(overlap.min + shift);
    const math::ivec3 destination = scale_down(overlap.min);
    const math::ivec3 extent = scale_down(overlap.size);
    bgfx::blit(place_pass.id,
               bgfx::TextureRegion{.handle = volume->native_handle(),
                                   .x = static_cast<uint16_t>(destination.x),
                                   .y = static_cast<uint16_t>(destination.y),
                                   .z = static_cast<uint16_t>(slab_z + destination.z),
                                   .width = static_cast<uint16_t>(extent.x),
                                   .height = static_cast<uint16_t>(extent.y),
                                   .depth = static_cast<uint16_t>(extent.z)},
               bgfx::TextureRegion{.handle = scratch->native_handle(),
                                   .x = static_cast<uint16_t>(source.x),
                                   .y = static_cast<uint16_t>(source.y),
                                   .z = static_cast<uint16_t>(source.z),
                                   .width = static_cast<uint16_t>(extent.x),
                                   .height = static_cast<uint16_t>(extent.y),
                                   .depth = static_cast<uint16_t>(extent.z)});
}

void gi_clipmap_compose_pass::dispatch_compose_box(gfx::render_pass& pass,
                                                   const global_sdf_clipmap& clipmap,
                                                   const global_sdf_clipmap_gpu& clipmap_gpu,
                                                   surface_cache_system& surface_cache,
                                                   uint32_t level,
                                                   const global_sdf_clipmap::voxel_box& box)
{
    const auto& lvl = clipmap.get_level(level);
    const auto& clipmap_settings = clipmap.get_settings();
    const uint32_t resolution = clipmap_settings.resolution;
    const auto& instances = surface_cache.get_instances();
    auto& atlas = surface_cache.get_atlas();
    compose_program_.program->begin();
    gfx::set_texture(compose_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
    bgfx::setBuffer(1, atlas.get_header_buffer(), bgfx::Access::Read);
    bgfx::setBuffer(2, atlas.get_indirection_buffer(), bgfx::Access::Read);
    bgfx::setBuffer(3, surface_cache.get_instance_buffer(), bgfx::Access::Read);
    bgfx::setBuffer(12, surface_cache.get_grid_buffer(), bgfx::Access::Read);
    // Stage 5 is the clipmap as an IMAGE here, where the tracing passes bind it as a sampler at
    // stage 4. Writing the level in place is what avoids a staging copy and the per-level
    // update_texture_3d the CPU path pays.
    gfx::set_image_3d(5, clipmap_gpu.get_texture()->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::R8);
    gfx::set_image_3d(6, clipmap_gpu.get_coverage_texture()->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::R8);
    const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                 float(atlas.get_atlas_voxel_dim()),
                                 float(instances.size()),
                                 0.0f};
    gfx::set_uniform(compose_program_.u_sdf_params, sdf_params);
    gfx::set_uniform(compose_program_.u_sdf_grid_params, surface_cache.get_grid_params(), gi::GI_SDF_GRID_PARAMS_VEC4);
    gfx::set_uniform(compose_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    // The reach is what the CPU composer seeds `nearest` with, and it must be the same value:
    // it is simultaneously the cheap-reject bound and the saturated output, so a mismatch
    // changes the composed bytes rather than merely the cost.
    const float reach = clipmap_settings.encode_range * lvl.voxel_size;
    const float compose_params[4] = {float(level), float(resolution), lvl.voxel_size, reach};
    gfx::set_uniform(compose_program_.u_clipmap_compose_params, compose_params);
    // w = 1: Lumen's cascade, which writes the coverage and leaves small objects out.
    const float compose_origin[4] = {lvl.origin.x, lvl.origin.y, lvl.origin.z, 1.0f};
    gfx::set_uniform(compose_program_.u_clipmap_compose_origin, compose_origin);
    const float range[4] = {float(box.min.x), float(box.min.y), float(box.min.z), clipmap_settings.object_radius_scale};
    gfx::set_uniform(compose_program_.u_clipmap_compose_range, range);
    const float range_size[4] = {float(box.size.x), float(box.size.y), float(box.size.z), 0.0f};
    gfx::set_uniform(compose_program_.u_clipmap_compose_range_size, range_size);
    const auto groups = [](int extent)
    {
        return (uint32_t(math::max(extent, 0)) + compose_group_size - 1u) / compose_group_size;
    };
    bgfx::dispatch(pass.id,
                   compose_program_.program->native_handle(),
                   groups(box.size.x),
                   groups(box.size.y),
                   groups(box.size.z));
    compose_program_.program->end();
}

} // namespace unravel
