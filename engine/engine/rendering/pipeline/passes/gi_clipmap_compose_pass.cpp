#include "gi_clipmap_compose_pass.h"

#include "lumen_pass_common.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/gi/gi_constants.h>

#include <logging/logging.h>

#include <graphics/graphics.h>

namespace unravel
{
namespace
{
/// Must match NUM_THREADS in cs_gi_clipmap_compose.sc (gi/brick_dispatch.sh BRICK_DISPATCH_EDGE).
constexpr uint32_t compose_group_size = 4u;
/// Groups per row of a brick dispatch: below the 65535 groups an axis takes.
constexpr uint32_t max_bricks_per_row = 32768u;
/// Must match NUM_THREADS in cs_gi_clipmap_mip.sc.
constexpr uint32_t mip_group_size = 4u;
/// Passes of a level's coarse mip, the first reading the level: the distance travels this
/// many mip texels from the level's surfaces. Odd, so the last pass writes the level's slab.
constexpr uint32_t mip_propagation_passes = 5u;
} // namespace

gi_clipmap_compose_pass::~gi_clipmap_compose_pass()
{
    lumen_pass::destroy_handle(brick_boxes_);
}

auto gi_clipmap_compose_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs_compose = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_clipmap_compose.sc");
    compose_program_.cache_uniforms();
    compose_program_.program = std::make_unique<gpu_program>(cs_compose);
    auto cs_mip = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_clipmap_mip.sc");
    mip_program_.cache_uniforms();
    mip_program_.program = std::make_unique<gpu_program>(cs_mip);
    if(!mip_program_.is_valid())
    {
        APPLOG_WARNING("[GI] The global distance field mip program failed to load; rays step through empty space by "
                       "each level alone.");
    }
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
    gfx::render_pass mip_pass("GI/Clipmap Mip");
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
    std::vector<math::vec4> table;
    std::vector<level_bricks> levels;
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
        const auto boxes = get_level_compose_boxes(clipmap, clipmap_gpu, level, scroll_copy_pass, scroll_place_pass);
        level_bricks entry;
        entry.level = level;
        entry.first_box = uint32_t(table.size() / 2u);
        entry.bricks = global_sdf_clipmap::append_brick_boxes(boxes, int(compose_group_size), 0u, table);
        entry.box_count = uint32_t(table.size() / 2u) - entry.first_box;
        levels.push_back(entry);
    }
    lumen_pass::upload_vec4_table(brick_boxes_, table);
    uint32_t composed = 0;
    for(const auto& entry : levels)
    {
        if(entry.bricks > 0u)
        {
            dispatch_compose_bricks(compose_pass, clipmap, clipmap_gpu, surface_cache, entry);
        }
        build_level_mip(clipmap_gpu, entry.level, mip_pass);
        ++composed;
    }
    // Consumed here rather than by the uploader: in GPU mode the uploader has no voxels to send, so
    // it would clear the mask before this pass ever saw it.
    clipmap.clear_dirty_levels();
    return composed > 0;
}

auto gi_clipmap_compose_pass::get_level_compose_boxes(const global_sdf_clipmap& clipmap,
                                                      const global_sdf_clipmap_gpu& clipmap_gpu,
                                                      uint32_t level,
                                                      gfx::render_pass& scroll_copy_pass,
                                                      gfx::render_pass& scroll_place_pass)
    -> std::vector<global_sdf_clipmap::voxel_box>
{
    const auto& lvl = clipmap.get_level(level);
    const uint32_t resolution = clipmap.get_settings().resolution;
    global_sdf_clipmap::voxel_box overlap;
    std::array<global_sdf_clipmap::voxel_box, 3> exposed;
    uint32_t exposed_count = 0;
    // PARTIAL (level::is_partial): the old window's overlap with the new one holds the bytes a
    // recompose would write outside the changed instances' boxes. A re-snapped origin moves it -
    // out to the scratch slab and back in at its new position, two blits, since a blit cannot shift
    // voxels within one texture - and composes the exposed slabs; the boxes are composed in place.
    // The coverage moves the same way at its downsample (the window snaps by whole coverage
    // texels). A scratch that failed to allocate falls back to composing the whole level.
    const scroll_volume distance{&scroll_scratch_[level], &clipmap_gpu.get_texture(), 1u};
    const scroll_volume coverage{&coverage_scroll_scratch_[level],
                                 &clipmap_gpu.get_coverage_texture(),
                                 global_sdf_clipmap_gpu::coverage_downsample};
    bool is_partial = lvl.is_partial;
    if(is_partial)
    {
        exposed_count = global_sdf_clipmap::compute_scroll_boxes(lvl.scroll_shift, resolution, overlap, exposed);
    }
    if(exposed_count > 0 && (!ensure_scroll_scratch(distance, resolution) || !ensure_scroll_scratch(coverage, resolution)))
    {
        is_partial = false;
    }
    if(is_partial)
    {
        if(exposed_count > 0)
        {
            blit_scroll_overlap(distance, resolution, level, overlap, lvl.scroll_shift, scroll_copy_pass, scroll_place_pass);
            blit_scroll_overlap(coverage, resolution, level, overlap, lvl.scroll_shift, scroll_copy_pass, scroll_place_pass);
        }
        std::vector<global_sdf_clipmap::voxel_box> boxes(exposed.begin(), exposed.begin() + exposed_count);
        boxes.insert(boxes.end(), lvl.partial_boxes.begin(), lvl.partial_boxes.end());
        return boxes;
    }
    global_sdf_clipmap::voxel_box whole;
    whole.min = math::ivec3(0);
    whole.size = math::ivec3(int(resolution));
    return {whole};
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

void gi_clipmap_compose_pass::build_level_mip(const global_sdf_clipmap_gpu& clipmap_gpu,
                                              uint32_t level,
                                              gfx::render_pass& pass)
{
    const auto& mip = clipmap_gpu.get_mip_texture();
    const auto& scratch = clipmap_gpu.get_mip_scratch();
    if(!mip_program_.is_valid() || !mip || !scratch)
    {
        return;
    }
    const uint32_t mip_resolution = clipmap_gpu.get_mip_resolution();
    const uint32_t groups = (mip_resolution + mip_group_size - 1u) / mip_group_size;
    const float slab_z = float(level * mip_resolution);
    for(uint32_t step = 0; step < mip_propagation_passes; ++step)
    {
        // Even passes write the level's slab, odd ones the scratch; each reads what the previous one wrote.
        const bool writes_slab = (step % 2u) == 0u;
        const auto& source = writes_slab ? scratch : mip;
        const auto& destination = writes_slab ? mip : scratch;
        mip_program_.program->begin();
        gfx::set_texture(mip_program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
        gfx::set_texture(mip_program_.s_clipmap_mip_prev, 1, source);
        gfx::set_image_3d(2, destination->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::R8);
        gfx::set_uniform(mip_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
        const float params[4] = {float(level), float(mip_resolution), writes_slab ? 0.0f : slab_z, writes_slab ? slab_z : 0.0f};
        gfx::set_uniform(mip_program_.u_clipmap_mip_params, params);
        const math::vec4 mode(step == 0u ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        gfx::set_uniform(mip_program_.u_clipmap_mip_mode, mode);
        bgfx::dispatch(pass.id, mip_program_.program->native_handle(), groups, groups, groups);
        mip_program_.program->end();
    }
}

void gi_clipmap_compose_pass::dispatch_compose_bricks(gfx::render_pass& pass,
                                                      const global_sdf_clipmap& clipmap,
                                                      const global_sdf_clipmap_gpu& clipmap_gpu,
                                                      surface_cache_system& surface_cache,
                                                      const level_bricks& bricks)
{
    const uint32_t level = bricks.level;
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
    // w = 1: a GI cascade, which writes the coverage and leaves small objects out.
    const float compose_origin[4] = {lvl.origin.x, lvl.origin.y, lvl.origin.z, 1.0f};
    gfx::set_uniform(compose_program_.u_clipmap_compose_origin, compose_origin);
    const math::vec4 scale(clipmap_settings.object_radius_scale, 0.0f, 0.0f, 0.0f);
    gfx::set_uniform(compose_program_.u_clipmap_compose_scale, scale);
    bgfx::setBuffer(7, brick_boxes_, bgfx::Access::Read);
    const uint32_t row = std::min(bricks.bricks, max_bricks_per_row);
    const math::vec4 dispatch(float(bricks.first_box), float(bricks.box_count), float(bricks.bricks), float(row));
    gfx::set_uniform(compose_program_.u_brick_dispatch, dispatch);
    bgfx::dispatch(pass.id, compose_program_.program->native_handle(), row, (bricks.bricks + row - 1u) / row, 1);
    compose_program_.program->end();
}

} // namespace unravel
