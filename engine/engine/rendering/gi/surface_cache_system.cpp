#include "surface_cache_system.h"

#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/gi_emitter_packing.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>

#include <concurrency/parallel.h>
#include <logging/logging.h>

namespace
{
namespace ANONYMOUS
{
/// Layout of the instance buffer: a flat array of vec4, matching BUFFER_RO(_, vec4, _).
auto get_vec4_buffer_layout() -> const gfx::vertex_layout&
{
    static const gfx::vertex_layout layout = []()
    {
        gfx::vertex_layout decl;
        decl.begin().add(gfx::attribute::TexCoord0, 4, gfx::attribute_type::Float).end();
        return decl;
    }();
    return layout;
}

/// Writes row @p row of an affine transform as a vec4. glm stores column-major with
/// m[column][row], so a row is gathered across columns.
void write_affine_row(float* dst, const ::math::mat4& m, int row)
{
    dst[0] = m[0][row];
    dst[1] = m[1][row];
    dst[2] = m[2][row];
    dst[3] = m[3][row];
}
} // namespace ANONYMOUS
} // namespace

namespace unravel
{

auto surface_cache_system::init(rtti::context& ctx) -> bool
{
    // The whole feature is compute-shaped: compose, attributes, light voxels and probes are
    // dispatches, and even the debug views read SSBOs. A backend without compute (measured:
    // Mesa handing bgfx a GL 3.1 compatibility context) cannot run ANY of it - the dispatches
    // are silently dropped, every volume keeps its allocation garbage, and the views paint
    // that garbage with nothing in the log to say why. Refusing loudly here is the honest
    // degradation: no GI, no debug views, one line naming the reason.
    const auto* caps = bgfx::getCaps();
    supported_ = caps != nullptr && 0 != (caps->supported & BGFX_CAPS_COMPUTE);
    if(!supported_)
    {
        APPLOG_WARNING("[SurfaceCache] GI disabled: this renderer backend reports no compute "
                       "shader support. The surface cache, its debug views and every GI pass "
                       "stay off; use the Vulkan backend for GI on this machine.");
        return true;
    }
    sdf_atlas::settings atlas_settings;
    if(!atlas_.init(atlas_settings))
    {
        APPLOG_WARNING("[SurfaceCache] Atlas initialisation failed. Surface cache GI is unavailable.");
        return false;
    }
    sdf_instance_grid::settings grid_settings;
    grid_.init(grid_settings);
    if(!light_buffer_.init())
    {
        APPLOG_WARNING("[SurfaceCache] Light buffer initialisation failed. Traced hits cannot be lit.");
    }
    // Texture means. Never seeded from the CPU (bgfx forbids CPU updates on compute-writable
    // buffers): an instance carries slot 0 until its texture's capture has WRITTEN its slot,
    // and the attribute composer skips the multiply for slot 0 - so an unwritten slot is
    // never read at all.
    texture_mean_buffer_ = gfx::create_dynamic_vertex_buffer(texture_mean_capacity,
                                                             ANONYMOUS::get_vec4_buffer_layout(),
                                                             BGFX_BUFFER_COMPUTE_READ_WRITE);
    if(!bgfx::isValid(texture_mean_buffer_))
    {
        APPLOG_WARNING("[SurfaceCache] Texture mean buffer allocation failed. Bounce albedo "
                       "falls back to base colour factors.");
    }
    return true;
}

auto surface_cache_system::deinit(rtti::context& ctx) -> bool
{
    instances_.clear();
    clipmap_instances_.clear();
    clipmap_keepalive_.clear();
    residency_.clear();
    if(bgfx::isValid(instance_buffer_))
    {
        gfx::destroy(instance_buffer_);
        instance_buffer_ = {bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(texture_mean_buffer_))
    {
        gfx::destroy(texture_mean_buffer_);
        texture_mean_buffer_ = {bgfx::kInvalidHandle};
    }
    texture_mean_slots_.clear();
    pending_texture_means_.clear();
    next_texture_mean_slot_ = 1;
    texture_mean_overflow_warned_ = false;
    instance_buffer_capacity_ = 0;
    instance_data_.clear();
    if(bgfx::isValid(grid_offset_buffer_))
    {
        gfx::destroy(grid_offset_buffer_);
        grid_offset_buffer_ = {bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(grid_instance_buffer_))
    {
        gfx::destroy(grid_instance_buffer_);
        grid_instance_buffer_ = {bgfx::kInvalidHandle};
    }
    grid_offset_capacity_ = 0;
    grid_instance_capacity_ = 0;
    grid_bounds_.clear();
    grid_params_.fill(0.0f);
    light_buffer_.shutdown();
    atlas_.shutdown();
    return true;
}

auto surface_cache_system::compute_wanted_mip(const math::bbox& world_bounds) const -> uint32_t
{
    /// Distance at which a placement drops a level, as a multiple of its own longest axis. A big
    /// building keeps its finest field far further away than a bolt does, which is the point.
    constexpr float k_finest_band = 6.0f;
    constexpr float k_middle_band = 24.0f;
    if(camera_positions_.empty())
    {
        // No camera is not "everything is far": an editor frame before any view exists would
        // otherwise place the whole scene at its coarsest level and then have to promote all of it.
        return 0;
    }
    const math::vec3 extent = world_bounds.get_dimensions();
    const float size = math::max(extent.x, math::max(extent.y, extent.z));
    if(!(size > 0.0f))
    {
        return 0;
    }
    float nearest = std::numeric_limits<float>::max();
    for(const auto& eye : camera_positions_)
    {
        // Distance to the BOX, not to its centre, so a placement the camera is standing inside
        // reads as zero rather than as half its own diagonal.
        const math::vec3 clamped = math::clamp(eye, world_bounds.min, world_bounds.max);
        nearest = math::min(nearest, math::length(eye - clamped));
    }
    const float relative = nearest / size;
    if(relative <= k_finest_band)
    {
        return 0;
    }
    if(relative <= k_middle_band)
    {
        return 1;
    }
    return mesh_sdf::mip_count - 1;
}

auto surface_cache_system::acquire_field(const hpp::uuid& mesh_uid,
                                        const mesh& m,
                                        uint32_t submesh_index,
                                        uint32_t wanted_mip) -> acquired_field
{
    // Keyed by submesh as well as mesh: each submesh has its own field, and they are uploaded to
    // the atlas independently.
    const field_key key{mesh_uid, submesh_index};
    auto& record = residency_[key];
    // Stamped before any early return, including the failures. The sweep releases whatever was not
    // asked for this frame, and a mesh that is present but currently unuploadable is still asked
    // for -- forgetting to stamp it would make the sweep drop the record and lose the fact that it
    // has no field, so the whole check would run again from scratch next frame.
    record.last_used_frame = world_frame_;
    if(record.has_no_field)
    {
        return {};
    }
    if(record.header_index != sdf_atlas::invalid_index)
    {
        // Already resident, but perhaps at a level chosen when this was further away or when the
        // atlas was busier. Promote only when the finer level fits with room to spare: promoting
        // into the last few slots would refuse the next field and trigger a growth or a scene-wide
        // demotion, which is a far worse trade than one placement staying coarse a little longer.
        // The margin is also the hysteresis that stops a placement on a band edge from being
        // released and re-uploaded every frame.
        if(wanted_mip < record.resident_mip)
        {
            const auto& finer = m.get_sdf(submesh_index, wanted_mip);
            const uint32_t headroom = atlas_.get_atlas_brick_dim() * atlas_.get_atlas_brick_dim();
            if(finer.is_valid() && atlas_.has_upload_budget(finer) &&
               finer.get_surface_brick_count() + headroom <= atlas_.get_free_brick_count())
            {
                atlas_.release(record.header_index);
                record.header_index = sdf_atlas::invalid_index;
                ++content_revision_;
            }
        }
        if(record.header_index != sdf_atlas::invalid_index)
        {
            return {record.header_index, record.resident_mip};
        }
    }
    const auto& sdf = m.get_sdf(submesh_index);
    if(!sdf.is_valid())
    {
        // No baked field: either the asset opted out or the bake could not produce one. That is a
        // property of the mesh and can never change while it is loaded, so it is recorded and not
        // retried for the rest of the session.
        record.has_no_field = true;
        return {};
    }
    // Name the phantom fields, once each: a shell floored this fat means the geometry is far
    // below its own field's resolution (a rope or curtain submesh whose bounds span a building
    // bakes metre voxels), and the result is not a bad field but a PHANTOM - a metre-thick blob
    // that occludes rays and steals attribution over a whole neighbourhood (measured: Sponza's
    // parapet ropes painting the gallery floor red in the albedo view). The bake cannot do
    // better at that voxel size; the mesh needs a finer SDF resolution in its import settings,
    // or to opt out of GI entirely.
    if(sdf.is_two_sided && sdf.two_sided_thickness > 0.25f && !record.thickness_warned)
    {
        record.thickness_warned = true;
        APPLOG_WARNING("[SurfaceCache] Mesh {} submesh {} bakes a {:.2f} m thick shell "
                       "({}x{}x{} voxels of {:.2f} m): thin geometry this far below its field's "
                       "resolution becomes a phantom occluder and steals GI attribution nearby. "
                       "Raise its SDF resolution or exclude it from GI in the import settings.",
                       hpp::to_string(mesh_uid),
                       submesh_index,
                       sdf.two_sided_thickness,
                       sdf.grid_dim.x,
                       sdf.grid_dim.y,
                       sdf.grid_dim.z,
                       sdf.voxel_size);
    }
    // Budget deferral BEFORE the refusal cache: a field deferred to keep this frame's uploads
    // inside the renderer's staging scratch is not refused - it must retry next frame, so it
    // must NOT consume the generation stamp below (that stamp would silence the retry until
    // something unrelated released bricks).
    if(!atlas_.has_upload_budget(sdf))
    {
        return {};
    }
    // Reached on first use, or after a previous attempt was refused for want of atlas room. A
    // refusal describes the atlas at a moment rather than the mesh, so it must be retried once the
    // sweep frees the previous scene's fields -- but ONLY then. Nothing else can change the answer,
    // and retrying unconditionally is what a scene that overruns the atlas turns into thousands of
    // doomed uploads per frame, climbing the refusal counters into the billions.
    const uint32_t generation = atlas_.get_release_generation();
    if(record.attempt_generation == generation)
    {
        return {};
    }
    record.attempt_generation = generation;
    // Finest level that FITS, not finest level full stop. A scene whose fields do not all fit the
    // shared atlas used to lose whole submeshes from GI -- silently in the image, since a missing
    // occluder just leaks light somewhere else. With a chain the same scene loses RESOLUTION
    // instead: each level down holds about a quarter of the bricks and reaches twice as far before
    // saturating, which for a distant or small submesh is a trade nobody sees.
    //
    // Checked against free slots BEFORE committing, because a refused upload is not free: it burns
    // the generation stamp and the submesh then waits for an unrelated release to retry.
    const uint32_t mip_count = m.get_sdf_mip_count(submesh_index);
    // Starts at the scene-wide bias, not at 0. See global_mip_bias_: choosing the finest level
    // that happens to fit is greedy, and greedy means the first arrivals spend the whole atlas.
    // The coarser of what distance asks for and what atlas pressure imposes. Neither may be
    // overridden by the other: a near placement still cannot have a level the atlas cannot hold.
    const uint32_t requested = math::max(wanted_mip, global_mip_bias_);
    const uint32_t first_mip = math::min(requested, mip_count > 0 ? mip_count - 1 : 0);
    for(uint32_t mip = first_mip; mip < mip_count; ++mip)
    {
        const auto& level = m.get_sdf(submesh_index, mip);
        if(!level.is_valid())
        {
            continue;
        }
        // The coarsest level is attempted whether or not it looks like it fits, so the "no room at
        // all" path still reaches atlas_.upload and reports through its own diagnostics rather
        // than failing silently here.
        const bool is_last = (mip + 1 == mip_count);
        if(!is_last && level.get_surface_brick_count() > atlas_.get_free_brick_count())
        {
            continue;
        }
        record.header_index = atlas_.upload(level);
        if(record.header_index != sdf_atlas::invalid_index)
        {
            record.resident_mip = mip;
            // Residency moved: a level fingerprint hashes the sdf pointer, which the packed
            // instance bytes do not carry.
            ++content_revision_;
            return {record.header_index, record.resident_mip};
        }
    }
    return {};
}

void surface_cache_system::apply_atlas_pressure()
{
    APP_SCOPE_PERF("GI/SurfaceCache/Apply Atlas Pressure");
    const uint64_t rejected = atlas_.get_rejected_brick_total();
    if(rejected <= acknowledged_rejected_bricks_)
    {
        return;
    }
    acknowledged_rejected_bricks_ = rejected;
    // MEMORY FIRST, RESOLUTION SECOND. A bigger atlas costs VRAM; a coarser field costs the
    // quality of every occluder in the scene. Growing is also what UE does -- its brick atlas
    // grows and its documented maximum is a target rather than a cap -- so degradation is the
    // last resort rather than the first response.
    if(atlas_.grow())
    {
        // grow() drops everything resident, because a slot index means a different position in a
        // differently sized atlas, and it resets its own refusal counters with them.
        acknowledged_rejected_bricks_ = 0;
        residency_.clear();
        ++content_revision_;
        return;
    }
    if(global_mip_bias_ + 1 >= mesh_sdf::mip_count)
    {
        // At the memory ceiling AND as coarse as the chains go. Nothing left to trade; the atlas
        // says what it needs.
        return;
    }
    ++global_mip_bias_;
    // Everything currently resident chose its level under the OLD bias, and most of it chose the
    // finest. Leaving those in place would leave the atlas exactly as full as it is now, so the
    // bias would apply only to fields that had not been placed yet -- which is the same
    // first-come-first-served failure one level down. Releasing the lot costs a frame of GI
    // during load and re-places everything at the new level.
    for(auto& entry : residency_)
    {
        if(entry.second.header_index != sdf_atlas::invalid_index)
        {
            atlas_.release(entry.second.header_index);
        }
    }
    residency_.clear();
    ++content_revision_;
    APPLOG_INFO("[SurfaceCache] SDF atlas is at its memory ceiling, so every field drops to mip {0} "
                "and is replaced. "
                "Each level down holds about a quarter of the bricks and reaches twice as far "
                "before saturating, so the scene keeps its occluders and loses resolution instead.",
                global_mip_bias_);
}

void surface_cache_system::release_unused_fields()
{
    APP_SCOPE_PERF("GI/SurfaceCache/Release Unused Fields");
    for(auto it = residency_.begin(); it != residency_.end();)
    {
        if(it->second.last_used_frame == world_frame_)
        {
            ++it;
            continue;
        }
        if(it->second.header_index != sdf_atlas::invalid_index)
        {
            // Residency moved (see the matching bump in acquire_field).
            ++content_revision_;
            atlas_.release(it->second.header_index);
        }
        it = residency_.erase(it);
    }
}


auto surface_cache_system::resolve_submesh_material(const model& mdl, const mesh& m, uint32_t submesh_index)
    -> material::sptr
{
    // The material of the submesh's DATA GROUP, which is its material index. Submeshes sharing a
    // material share this entry, which is correct: they are painted the same.
    const auto* sub = m.get_submesh(submesh_index, 0);
    if(sub == nullptr)
    {
        return {};
    }
    return mdl.get_material_instance(sub->data_group_id);
}

auto surface_cache_system::acquire_texture_mean_slot(const asset_handle<gfx::texture>& color_map,
                                                      bool& out_captured) -> uint32_t
{
    out_captured = false;
    if(!color_map.is_valid() || !bgfx::isValid(texture_mean_buffer_))
    {
        return 0;
    }
    auto [it, inserted] = texture_mean_slots_.try_emplace(color_map.uid());
    auto& entry = it->second;
    if(inserted)
    {
        if(next_texture_mean_slot_ < texture_mean_capacity)
        {
            entry.slot = next_texture_mean_slot_++;
        }
        else
        {
            // Slot 0 is the seeded white, so overflow degrades to factor-only albedo.
            entry.slot = 0;
            entry.queued = true;
            entry.captured = true;
            if(!texture_mean_overflow_warned_)
            {
                texture_mean_overflow_warned_ = true;
                APPLOG_WARNING("[SurfaceCache] More than {} distinct colour maps; further "
                               "textures bounce their base colour factor only.",
                               texture_mean_capacity - 1);
            }
        }
    }
    if(!entry.queued && color_map.is_ready())
    {
        // Queue only once the texture is actually RESIDENT. The readiness gate is load-bearing:
        // get(false) returns a default-constructed placeholder for a still-streaming asset, not
        // null - capturing that would write a black mean and poison the slot for the session.
        auto texture = color_map.get(false);
        if(texture && texture->is_valid())
        {
            pending_texture_means_.push_back({texture, entry.slot});
            entry.queued = true;
        }
    }
    out_captured = entry.captured;
    // Slot 0 (the composer's skip-the-multiply case) until the capture has written the slot:
    // the mean buffer is compute-write-only, so there is no seeded value to read before then.
    return entry.captured ? entry.slot : 0u;
}

auto surface_cache_system::take_texture_mean_captures(uint32_t budget)
    -> std::vector<texture_mean_capture>
{
    std::vector<texture_mean_capture> captures;
    while(!pending_texture_means_.empty() && uint32_t(captures.size()) < budget)
    {
        texture_mean_capture capture = pending_texture_means_.front();
        pending_texture_means_.erase(pending_texture_means_.begin());
        // Cannot fire after the is_ready gate in acquire, but a slot must NEVER be marked
        // captured without its dispatch actually running - a marked-but-unwritten slot reads
        // garbage into every albedo that uses it.
        if(!capture.texture || !capture.texture->is_valid())
        {
            continue;
        }
        for(auto& [uid, entry] : texture_mean_slots_)
        {
            if(entry.slot == capture.slot)
            {
                entry.captured = true;
            }
        }
        // A landed capture changes the clipmap-level fingerprints (mean_captured is hashed
        // into them) without touching the packed instance bytes.
        ++content_revision_;
        captures.push_back(std::move(capture));
    }
    return captures;
}

namespace
{
/// One FNV-1a round over a float's bits. Component by component, never over sizeof:
/// math::vec3 carries alignment padding whose bytes are indeterminate, and hashing them made
/// every static placement read as moved every frame (measured: 163 regions per frame in a
/// parked scene, the whole screen at the fast cap).
auto mix_float(uint64_t hash, float value) -> uint64_t
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (hash ^ uint64_t(bits)) * 0x100000001b3ull;
}

auto mix_vec3(uint64_t hash, const math::vec3& v) -> uint64_t
{
    return mix_float(mix_float(mix_float(hash, v.x), v.y), v.z);
}

/// FNV-1a over a placement's sixteen matrix floats. Both keys a placement needs - the pose
/// cache's and the dirty tracker's - continue from this with their own inputs, so the matrix
/// is hashed once per placement instead of once per key.
auto hash_matrix(const math::mat4& m) -> uint64_t
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for(int column = 0; column < 4; ++column)
    {
        for(int row = 0; row < 4; ++row)
        {
            hash = mix_float(hash, m[column][row]);
        }
    }
    return hash;
}

/// The pose cache's key: the transform and the field's local bounds, the two inputs of the
/// inverse and the transformed bounds (the material is deliberately not part of it).
auto compute_pose_key(uint64_t matrix_hash, const math::bbox& local_bounds) -> uint64_t
{
    return mix_vec3(mix_vec3(matrix_hash, local_bounds.min), local_bounds.max);
}

/// World bounds of a local box under an affine matrix, by the per-axis min/max method
/// bbox::mul uses for a transform: each world axis contributes the smaller and the larger of
/// its two scaled endpoints and the translation rides on top. Exact - it IS the box of the
/// eight transformed corners - at six vec3 scales instead of eight mat4 x vec4 products.
auto transform_bounds(const math::mat4& m, const math::bbox& local) -> math::bbox
{
    const math::vec3 x_axis(m[0]);
    const math::vec3 y_axis(m[1]);
    const math::vec3 z_axis(m[2]);
    const math::vec3 translation(m[3]);
    const math::vec3 xa = x_axis * local.min.x;
    const math::vec3 xb = x_axis * local.max.x;
    const math::vec3 ya = y_axis * local.min.y;
    const math::vec3 yb = y_axis * local.max.y;
    const math::vec3 za = z_axis * local.min.z;
    const math::vec3 zb = z_axis * local.max.z;
    return math::bbox(math::min(xa, xb) + math::min(ya, yb) + math::min(za, zb) + translation,
                      math::max(xa, xb) + math::max(ya, yb) + math::max(za, zb) + translation);
}
} // namespace

auto surface_cache_system::compute_placement_hash(uint64_t matrix_hash,
                                                   const math::vec3& albedo,
                                                   const math::vec3& emissive) -> uint64_t
{
    // The pose and the material the attribute voxels bake: either moving makes the light this
    // placement bounced (or emitted) stale wherever it stood.
    return mix_vec3(mix_vec3(matrix_hash, albedo), emissive);
}

auto surface_cache_system::acquire_tracked(uint64_t identity) -> std::pair<tracked_placement&, bool>
{
    auto [it, inserted] = tracked_placements_.try_emplace(identity);
    return {it->second, inserted};
}

void surface_cache_system::record_placement(tracked_placement& tracked,
                                            bool inserted,
                                            uint64_t placement_hash,
                                            const math::vec3& emissive,
                                            const math::bbox& bounds)
{
    const uint64_t hash = placement_hash;
    // EMISSIVE REACH: a bounced pool sits within a probe spacing of its placement (the
    // kernel's margin), but an emitter lights everything it faces out to where L x A / d^2
    // drops below GI_TEMPORAL_DIRTY_EMISSIVE_IRRADIANCE. Its region is inflated to that
    // distance so the pool it LEFT flushes too: the temporal collapses there and the screen
    // tier stops reading last frame's composite there (the loop that kept the old glow alive
    // for seconds). Power-derived, so a bullet inflates by centimetres, a room panel by the
    // room.
    math::bbox region_bounds = bounds;
    const float luminance = 0.2126f * emissive.x + 0.7152f * emissive.y + 0.0722f * emissive.z;
    if(luminance > 0.0f && !(bounds.min.x > bounds.max.x))
    {
        const math::vec3 extent = math::max(bounds.max - bounds.min, math::vec3(0.0f));
        const float area = 2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
        const float reach = math::clamp(std::sqrt(luminance * area / float(gi::GI_TEMPORAL_DIRTY_EMISSIVE_IRRADIANCE)),
                                        0.0f,
                                        float(gi::GI_TEMPORAL_DIRTY_EMISSIVE_REACH_MAX));
        region_bounds.min -= math::vec3(reach);
        region_bounds.max += math::vec3(reach);
    }
    if(inserted || tracked.swept)
    {
        // A placement appearing (or re-appearing after a sweep) lights and occludes where
        // nothing did: its bounds are stale from the first frame.
        tracked.history.push_back({world_frame_, region_bounds, bounds});
    }
    else if(tracked.placement_hash != hash)
    {
        // Both the vacated spot and the new one: the light the placement bounced where it WAS
        // is exactly the trail the flush exists for.
        tracked.history.push_back({world_frame_, tracked.bounds, tracked.field_bounds});
        tracked.history.push_back({world_frame_, region_bounds, bounds});
    }
    tracked.placement_hash = hash;
    tracked.bounds = region_bounds;
    tracked.field_bounds = bounds;
    tracked.seen_frame = world_frame_;
    tracked.swept = false;
}

namespace
{
auto pack_region_list(const std::vector<surface_cache_system::dirty_region>& regions,
                      float* out_bounds,
                      uint32_t max_regions) -> uint32_t
{
    uint32_t count = 0;
    for(const auto& region : regions)
    {
        if(count >= max_regions)
        {
            break;
        }
        float* slot = out_bounds + size_t(count) * 8u;
        slot[0] = region.bounds.min.x;
        slot[1] = region.bounds.min.y;
        slot[2] = region.bounds.min.z;
        slot[3] = 0.0f;
        slot[4] = region.bounds.max.x;
        slot[5] = region.bounds.max.y;
        slot[6] = region.bounds.max.z;
        slot[7] = 0.0f;
        ++count;
    }
    return count;
}
} // namespace

auto surface_cache_system::pack_dirty_regions(float* out_bounds, uint32_t max_regions) const -> uint32_t
{
    return pack_region_list(dirty_regions_, out_bounds, max_regions);
}

auto surface_cache_system::pack_vis_memo_regions(float* out_bounds, uint32_t max_regions) const -> uint32_t
{
    return pack_region_list(vis_memo_regions_, out_bounds, max_regions);
}

void surface_cache_system::rebuild_dirty_regions()
{
    APP_SCOPE_PERF("GI/SurfaceCache/Rebuild Dirty Regions");
    dirty_regions_.clear();
    dirty_candidates_.clear();
    const uint64_t hold = uint64_t(gi::GI_TEMPORAL_DIRTY_HOLD_FRAMES);
    for(auto it = tracked_placements_.begin(); it != tracked_placements_.end();)
    {
        auto& tracked = it->second;
        if(tracked.seen_frame != world_frame_ && !tracked.swept)
        {
            // Vanished this frame: the spot it left is stale like a move's vacated spot.
            // Recorded once; the entry is erased below once the history ages out.
            tracked.history.push_back({world_frame_, tracked.bounds, tracked.field_bounds});
            tracked.swept = true;
        }
        // Age out the history beyond the hold window. Entries are appended in frame order, so
        // the aged ones are a PREFIX: the begin index steps past them in O(1) per frame, where
        // the erase this replaced shifted every surviving entry - about 96 of them, 32 bytes
        // each, for every mover, every frame, which was most of this function's cost with a
        // crowd. Compacted once the dead prefix outweighs the live tail, so memory stays bounded.
        auto& history = tracked.history;
        size_t& begin = tracked.history_begin;
        while(begin < history.size() && world_frame_ - history[begin].frame > hold)
        {
            ++begin;
        }
        if(begin == history.size())
        {
            history.clear();
            begin = 0;
            if(tracked.swept)
            {
                it = tracked_placements_.erase(it);
                continue;
            }
            ++it;
            continue;
        }
        if(begin > history.size() / 2)
        {
            history.erase(history.begin(), history.begin() + ptrdiff_t(begin));
            begin = 0;
        }
        // The newest entry is the last appended, so the placement's latest change is O(1).
        dirty_candidates_.push_back({history.back().frame, &tracked});
        ++it;
    }
    // Only the GI_TEMPORAL_DIRTY_MAX_BOUNDS most recent regions ever reach a shader
    // (pack_dirty_regions); past that count the consumers switch to their screen-wide fallback
    // and never read the rest. So the unions - each a walk of a placement's whole window - are
    // computed for the top of the list only, and the full sort that ordered two thousand
    // regions to hand over sixteen is a partial one. At or under the budget the output is
    // exactly what it was.
    constexpr size_t budget = size_t(gi::GI_TEMPORAL_DIRTY_MAX_BOUNDS);
    // The vis-memo's list (pack_vis_memo_regions) is the same placements over its shorter
    // hold, so its total is counted before the cut: the cut keeps the newest, which is every
    // placement inside that hold whenever the total fits the budget.
    const uint64_t memo_hold = uint64_t(gi::GI_VIS_MEMO_REGION_HOLD_FRAMES);
    vis_memo_regions_.clear();
    size_t memo_total = 0;
    for(const auto& candidate : dirty_candidates_)
    {
        if(world_frame_ - candidate.first <= memo_hold)
        {
            ++memo_total;
        }
    }
    const auto newer_first = [](const std::pair<uint64_t, tracked_placement*>& a,
                                const std::pair<uint64_t, tracked_placement*>& b)
    {
        return a.first > b.first;
    };
    const size_t candidate_total = dirty_candidates_.size();
    if(candidate_total > budget)
    {
        std::partial_sort(dirty_candidates_.begin(),
                          dirty_candidates_.begin() + ptrdiff_t(budget),
                          dirty_candidates_.end(),
                          newer_first);
        dirty_candidates_.resize(budget);
    }
    else
    {
        std::sort(dirty_candidates_.begin(), dirty_candidates_.end(), newer_first);
    }
    for(const auto& [latest_frame, tracked] : dirty_candidates_)
    {
        dirty_region region;
        region.bounds.reset();
        const auto& history = tracked->history;
        for(size_t i = tracked->history_begin; i < history.size(); ++i)
        {
            const auto& entry = history[i];
            // Skip an inverted (never set) box; add_point of its sentinels would span the
            // world.
            if(entry.bounds.min.x > entry.bounds.max.x)
            {
                continue;
            }
            region.bounds.add_point(entry.bounds.min);
            region.bounds.add_point(entry.bounds.max);
            region.last_change_frame = std::max(region.last_change_frame, entry.frame);
        }
        if(!(region.bounds.min.x > region.bounds.max.x))
        {
            dirty_regions_.push_back(region);
        }
        if(world_frame_ - latest_frame > memo_hold)
        {
            continue;
        }
        dirty_region memo_region;
        memo_region.bounds.reset();
        for(size_t i = tracked->history_begin; i < history.size(); ++i)
        {
            const auto& entry = history[i];
            if(world_frame_ - entry.frame > memo_hold || entry.field_bounds.min.x > entry.field_bounds.max.x)
            {
                continue;
            }
            memo_region.bounds.add_point(entry.field_bounds.min);
            memo_region.bounds.add_point(entry.field_bounds.max);
            memo_region.last_change_frame = std::max(memo_region.last_change_frame, entry.frame);
        }
        if(!(memo_region.bounds.min.x > memo_region.bounds.max.x))
        {
            vis_memo_regions_.push_back(memo_region);
        }
    }
    // Over budget the total is the candidate count (the cut discarded the rest); under it every
    // candidate with a populated box is in the list, so the list is the exact count.
    dirty_region_total_ = candidate_total > budget ? candidate_total : dirty_regions_.size();
    vis_memo_region_total_ = memo_total > budget ? memo_total : vis_memo_regions_.size();
}

void surface_cache_system::add_instance(uint64_t identity,
                                         uint32_t header_index,
                                         const mesh_sdf& sdf,
                                         const math::mat4& local_to_world,
                                         const std::shared_ptr<mesh>& owner,
                                         const material_summary& material)
{
    instance inst;
    // Decoded once per material per frame (summarize_material); a non-PBR material keeps the
    // neutral defaults rather than guessing at a colour nothing on screen is painted with.
    inst.albedo = material.albedo;
    inst.emissive = material.emissive;
    inst.metalness = material.metalness;
    inst.mean_slot = material.mean_slot;
    inst.mean_captured = material.mean_captured;
    inst.emissive_mean_slot = material.emissive_mean_slot;
    inst.emissive_mean_captured = material.emissive_mean_captured;
    inst.local_to_world = local_to_world;
    inst.header_index = header_index;
    // POSE CACHE: the inverse, the smallest scale axis and the transformed bounds are pure
    // functions of the transform and the field's local bounds, and a static placement
    // presents the same pair every frame - the tracker keys placements by identity, so it
    // carries them across frames. Recomputed only when the key moves. One lookup serves the
    // cache and the tracker both (acquire_tracked); a fresh record has no pose to offer.
    const uint64_t matrix_hash = hash_matrix(local_to_world);
    const uint64_t pose_key = compute_pose_key(matrix_hash, sdf.bounds);
    auto [tracked, inserted] = acquire_tracked(identity);
    if(tracked.has_pose && tracked.pose_key == pose_key)
    {
        inst.world_to_local = tracked.world_to_local;
        inst.local_to_world_scale = tracked.local_to_world_scale;
        inst.world_bounds = tracked.field_bounds;
    }
    else
    {
        // A placement matrix is affine (rotation, scale, skew, translation - never a
        // projection), so the 3x3 inverse plus a translation is the whole answer, at a fraction
        // of the general 4x4 cofactor expansion glm::inverse runs. glm:: explicitly: namespace
        // math declares its own inverse() for math::transform, which hides the glm overloads
        // from qualified lookup as math::inverse.
        inst.world_to_local = glm::affineInverse(local_to_world);
        // Scale is recovered from the matrix rather than from a transform object, because the
        // renderer hands out plain matrices for submesh nodes.
        const float scale_x = math::length(math::vec3(local_to_world[0]));
        const float scale_y = math::length(math::vec3(local_to_world[1]));
        const float scale_z = math::length(math::vec3(local_to_world[2]));
        // Smallest axis, not average: a distance measured in local space maps to at least this
        // much world distance, so using it keeps every step an under-estimate. The largest or the
        // mean would let a sphere trace overshoot a non-uniformly scaled instance and pass
        // through it.
        inst.local_to_world_scale = math::max(math::min(scale_x, math::min(scale_y, scale_z)), 1e-6f);
        // World-space AABB of the field's local bounds, for the tracer's broad phase: the exact
        // box of the transformed corners, so a rotated instance still gets a bound that
        // contains it.
        inst.world_bounds = transform_bounds(local_to_world, sdf.bounds);
    }
    instances_.push_back(inst);
    record_placement(tracked,
                     inserted,
                     compute_placement_hash(matrix_hash, inst.albedo, inst.emissive),
                     inst.emissive,
                     inst.world_bounds);
    tracked.pose_key = pose_key;
    tracked.has_pose = true;
    tracked.world_to_local = inst.world_to_local;
    tracked.local_to_world_scale = inst.local_to_world_scale;
    // INSTANCE VELOCITY (the gather temporal's hit-motion signal): the bounds centre's
    // displacement since this placement's previous frame, and the largest displacement of
    // any local-bounds corner (a spinning placement moves its surface, not its centre);
    // both zero on a first sighting.
    if(tracked.has_last_pose)
    {
        auto& moving = instances_.back();
        // A point's displacement is (L1 - L0) x point: one matrix difference and eight
        // products, where two transforms per corner was sixteen.
        const math::mat4 delta = local_to_world - tracked.last_local_to_world;
        moving.velocity = math::vec3(delta * math::vec4(sdf.bounds.get_center(), 1.0f));
        float max_corner = 0.0f;
        for(const auto& corner : sdf.bounds.get_corners())
        {
            max_corner = math::max(max_corner, math::length(math::vec3(delta * math::vec4(corner, 1.0f))));
        }
        moving.max_corner_displacement = max_corner;
    }
    tracked.last_local_to_world = local_to_world;
    tracked.has_last_pose = true;
    // The clipmap composer borrows a raw mesh_sdf pointer, so the owning mesh has to be kept
    // alive for as long as the composition input list references it. A crowd of one mesh pushed
    // the same pointer once per placement; consecutive placements of one mesh now share an
    // entry. A repeat is harmless and an omission is not, so only the previous entry is compared.
    if(clipmap_keepalive_.empty() || clipmap_keepalive_.back().get() != owner.get())
    {
        clipmap_keepalive_.push_back(owner);
    }
    global_sdf_instance clipmap_instance;
    clipmap_instance.sdf = &sdf;
    clipmap_instance.world_to_local = inst.world_to_local;
    clipmap_instance.world_bounds = inst.world_bounds;
    clipmap_instance.local_to_world_scale = inst.local_to_world_scale;
    // Attribute-voxel material (GI v2 plan 3.1): the same per-submesh values the tracer's
    // instance buffer carries, so a cascade surface voxel and a near-field hit agree on what
    // the surface looks like.
    clipmap_instance.albedo = inst.albedo;
    clipmap_instance.emissive = inst.emissive;
    clipmap_instance.mean_slot = inst.mean_slot;
    clipmap_instance.mean_captured = inst.mean_captured;
    clipmap_instance.emissive_mean_slot = inst.emissive_mean_slot;
    clipmap_instance.emissive_mean_captured = inst.emissive_mean_captured;
    clipmap_instances_.push_back(clipmap_instance);
}

void surface_cache_system::upload_instance_grid()
{
    APP_SCOPE_PERF("GI/SurfaceCache/Upload Instance Grid");
    // The grid is a pure function of the instance set (bounds are part of the packed data the
    // fingerprint covers), so an unchanged fingerprint means an identical grid: skip the CPU
    // rebuild and the multi-megabyte re-upload. Without this a static scene re-staged the whole
    // structure every frame - at Bistro scale that alone kept the Vulkan backend allocating
    // staging memory continuously.
    if(grid_uploaded_fingerprint_ == instance_fingerprint_ && grid_.is_valid() &&
       bgfx::isValid(grid_offset_buffer_) && bgfx::isValid(grid_instance_buffer_))
    {
        return;
    }
    grid_uploaded_fingerprint_ = instance_fingerprint_;
    grid_bounds_.clear();
    grid_bounds_.reserve(instances_.size());
    for(const auto& inst : instances_)
    {
        grid_bounds_.push_back(inst.world_bounds);
    }
    grid_.build(grid_bounds_);
    // w of the second vec4 gates the whole tier. Zeroed first so every early-out below leaves the
    // grid switched off rather than pointing a tracer at a stale structure -- a tracer that walks
    // last frame's cells finds last frame's instances, which is worse than not culling at all.
    grid_params_.fill(0.0f);
    if(!grid_.is_valid())
    {
        return;
    }
    const auto& offsets = grid_.get_cell_offsets();
    const auto& cell_instances = grid_.get_cell_instances();
    const auto ensure_capacity = [](gfx::dynamic_index_buffer_handle& buffer,
                                    uint32_t& capacity,
                                    uint32_t required) -> void
    {
        if(bgfx::isValid(buffer) && required <= capacity)
        {
            return;
        }
        if(bgfx::isValid(buffer))
        {
            gfx::destroy(buffer);
        }
        // Grow with slack, so a scene gaining a few instances per frame does not recreate the
        // buffer every frame.
        capacity = required + required / 2u + 64u;
        buffer = gfx::create_dynamic_index_buffer(capacity, BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_INDEX32);
    };
    ensure_capacity(grid_offset_buffer_, grid_offset_capacity_, math::max(uint32_t(offsets.size()), 1u));
    ensure_capacity(grid_instance_buffer_,
                    grid_instance_capacity_,
                    math::max(uint32_t(cell_instances.size()), 1u));
    if(!bgfx::isValid(grid_offset_buffer_) || !bgfx::isValid(grid_instance_buffer_))
    {
        return;
    }
    gfx::update(grid_offset_buffer_, 0, gfx::copy(offsets.data(), uint32_t(offsets.size() * sizeof(uint32_t))));
    if(!cell_instances.empty())
    {
        gfx::update(grid_instance_buffer_,
                    0,
                    gfx::copy(cell_instances.data(), uint32_t(cell_instances.size() * sizeof(uint32_t))));
    }
    const auto& origin = grid_.get_origin();
    const auto& dim = grid_.get_dim();
    grid_params_[0] = origin.x;
    grid_params_[1] = origin.y;
    grid_params_[2] = origin.z;
    grid_params_[3] = grid_.get_cell_size();
    grid_params_[4] = float(dim.x);
    grid_params_[5] = float(dim.y);
    grid_params_[6] = float(dim.z);
    grid_params_[7] = 1.0f;
}

void surface_cache_system::rebuild_emitters()
{
    emitters_.clear();
    for(const auto& inst : instances_)
    {
        // THE FACTOR, NOT THE EMITTED RADIANCE. The attribute composer scales emission by the
        // emissive map's texture mean, but that mean is written by a compute shader and never
        // read back, so this side cannot see it: a textured emitter's table entry is up to
        // 1/mean too bright relative to the volume every ray actually reads. Bounded, because
        // the entry is only ever used as a RANKING - which pieces enter the table and which the
        // probes aim at - and aiming at a dimmer-than-expected emitter costs sampling
        // efficiency, never energy: the aimed ray traces the real field like any other. The one
        // numeric consumer is the reflection near-field's emitter_fraction, whose ratio cancels
        // the factor but whose fraction does not, so a textured emitter reads as accounting for
        // more of the cell than it does. Reading the means back to the CPU would close all of
        // it - and would let the CPU composer apply the albedo mean it also skips today.
        const math::vec3& radiance = inst.emissive;
        const float luminance = 0.2126f * radiance.x + 0.7152f * radiance.y + 0.0722f * radiance.z;
        if(luminance < float(gi::GI_EMISSIVE_NEE_MIN_LUMINANCE))
        {
            continue;
        }
        const math::vec3 extent = inst.world_bounds.max - inst.world_bounds.min;
        // SEGMENTS: the bounding sphere of a long strip swallows every probe near it, so the
        // bounds are cut into pieces no longer than GI_EMISSIVE_NEE_SEGMENT per axis, each a
        // sphere a probe can aim inside. A thin or flat piece still wastes part of its cone
        // on directions past it - those rays read whatever stands behind, which is the right
        // answer for that direction.
        const float segment = float(gi::GI_EMISSIVE_NEE_SEGMENT);
        math::ivec3 pieces(math::max(1, int(std::ceil(extent.x / segment))),
                           math::max(1, int(std::ceil(extent.y / segment))),
                           math::max(1, int(std::ceil(extent.z / segment))));
        // BOUNDED per instance (see GI_EMISSIVE_NEE_MAX_PIECES): the longest axis is halved
        // until the count fits, so a large or rotated bound keeps whole-object coverage with
        // longer segments instead of monopolising the table and building thousands of pieces
        // to be thrown away. The loop always shrinks - a product above the cap needs an axis
        // of at least three - and stops at (1, 1, 1).
        while(pieces.x * pieces.y * pieces.z > int(gi::GI_EMISSIVE_NEE_MAX_PIECES))
        {
            if(pieces.x >= pieces.y && pieces.x >= pieces.z)
            {
                pieces.x = math::max(1, pieces.x / 2);
            }
            else if(pieces.y >= pieces.z)
            {
                pieces.y = math::max(1, pieces.y / 2);
            }
            else
            {
                pieces.z = math::max(1, pieces.z / 2);
            }
        }
        const math::vec3 piece_extent(extent.x / float(pieces.x), extent.y / float(pieces.y), extent.z / float(pieces.z));
        // Ranked by the SHARED weight (gi_emitter_packing.h), the same expression the shader
        // reconstructs from the packed extent - so the table's order and the reflection tier's
        // near-field top-K cannot disagree about which pieces matter.
        const float piece_power = gi::emitter_selection_weight(luminance, piece_extent);
        for(int z = 0; z < pieces.z; ++z)
        {
            for(int y = 0; y < pieces.y; ++y)
            {
                for(int x = 0; x < pieces.x; ++x)
                {
                    emitter e;
                    e.center = inst.world_bounds.min +
                               piece_extent * (math::vec3(float(x), float(y), float(z)) + math::vec3(0.5f));
                    e.radius = 0.5f * math::length(piece_extent);
                    e.radiance = radiance;
                    e.power = piece_power;
                    e.extent = piece_extent;
                    emitters_.push_back(e);
                }
            }
        }
    }
    const size_t cap = size_t(gi::GI_EMISSIVE_NEE_MAX_EMITTERS);
    if(emitters_.size() > cap)
    {
        std::partial_sort(emitters_.begin(),
                          emitters_.begin() + ptrdiff_t(cap),
                          emitters_.end(),
                          [](const emitter& a, const emitter& b) { return a.power > b.power; });
        emitters_.resize(cap);
    }
}

void surface_cache_system::upload_instances()
{
    APP_SCOPE_PERF("GI/SurfaceCache/Upload Instances");
    // resize, not assign: every one of the 40 floats per instance is written below (including
    // the trailing pad), so the quarter-megabyte zero-fill assign did at Bistro scale was
    // fully overwritten every frame. The emitter table rides after the instances (see
    // rebuild_emitters): the tracers bind this buffer already and have no stage to spare.
    const size_t instance_floats = size_t(instances_.size()) * instance_vec4_stride * 4u;
    instance_data_.resize(instance_floats + emitters_.size() * emitter_vec4_stride * 4u);
    for(size_t i = 0; i < emitters_.size(); ++i)
    {
        const auto& e = emitters_[i];
        float* dst = instance_data_.data() + instance_floats + i * emitter_vec4_stride * 4u;
        dst[0] = e.center.x;
        dst[1] = e.center.y;
        dst[2] = e.center.z;
        dst[3] = e.radius;
        dst[4] = e.radiance.x;
        dst[5] = e.radiance.y;
        dst[6] = e.radiance.z;
        // The piece extent rides the power lane (see emitter::extent and gi_emitter_packing.h):
        // 8 bits per axis as a fraction of GI_EMISSIVE_NEE_SEGMENT, exact in a float, negated
        // and offset so a shader fed by an older upload (a positive power) reads "no extent".
        // The power itself does not survive; GiLoadEmitter rebuilds it from this extent and the
        // radiance with gi::emitter_selection_weight's shader mirror.
        dst[7] = gi::pack_emitter_extent_lane(e.extent);
    }
    // The pack is an indexed write per instance into a pre-sized array - no shared state, order
    // preserved by construction - so it runs across the pool once the list is large enough to
    // pay for the dispatch; below that the same loop runs inline on this thread.
    constexpr size_t parallel_pack_threshold = 512;
    float* const instance_base = instance_data_.data();
    poolstl::for_each_par_if(
        instances_.size() >= parallel_pack_threshold,
        instances_.begin(),
        instances_.end(),
        [&](const instance& inst)
        {
            const size_t i = size_t(&inst - instances_.data());
            float* dst = instance_base + i * instance_vec4_stride * 4u;
            ANONYMOUS::write_affine_row(dst + 0, inst.world_to_local, 0);
            ANONYMOUS::write_affine_row(dst + 4, inst.world_to_local, 1);
            ANONYMOUS::write_affine_row(dst + 8, inst.world_to_local, 2);
            ANONYMOUS::write_affine_row(dst + 12, inst.local_to_world, 0);
            ANONYMOUS::write_affine_row(dst + 16, inst.local_to_world, 1);
            ANONYMOUS::write_affine_row(dst + 20, inst.local_to_world, 2);
            dst[24] = inst.world_bounds.min.x;
            dst[25] = inst.world_bounds.min.y;
            dst[26] = inst.world_bounds.min.z;
            dst[27] = float(inst.header_index);
            dst[28] = inst.world_bounds.max.x;
            dst[29] = inst.world_bounds.max.y;
            dst[30] = inst.world_bounds.max.z;
            dst[31] = inst.local_to_world_scale;
            dst[32] = inst.albedo.x;
            dst[33] = inst.albedo.y;
            dst[34] = inst.albedo.z;
            // Both texture-mean slots share this lane, the colour map's below the emissive map's:
            // every one of the record's 44 floats is spoken for, and both slots are under
            // texture_mean_capacity (1024), so the packed integer stays under 2^21 and exact in a
            // float. MIRROR OF SdfMeanSlotColor / SdfMeanSlotEmissive in sdf_common.sh.
            static_assert(texture_mean_capacity <= mean_slot_radix,
                          "a mean slot must fit below the packing radix");
            dst[35] = float(inst.mean_slot) + float(inst.emissive_mean_slot) * float(mean_slot_radix);
            dst[36] = inst.emissive.x;
            dst[37] = inst.emissive.y;
            dst[38] = inst.emissive.z;
            // Lane 9 w: metalness, read by the reflection trace kernel next to the emission.
            dst[39] = inst.metalness;
            // Lane 10: the instance velocity (see instance::velocity), w = the largest corner
            // displacement over the same frame (rotation).
            dst[40] = inst.velocity.x;
            dst[41] = inst.velocity.y;
            dst[42] = inst.velocity.z;
            dst[43] = inst.max_corner_displacement;
        });
    // Content hash over the exact bytes the GPU receives: any change to a transform, material
    // colour, bounds, or field index flips it. Eight bytes per round instead of the byte-serial
    // FNV chain this used to run - the old loop was ~256K dependent multiplies at Bistro scale,
    // ~100 us of main thread per frame spent deciding to skip an upload. FNV-1a over 64-bit
    // lanes keeps the same "any byte flips it" property at an eighth of the rounds.
    uint64_t fingerprint = 1469598103934665603ull;
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(instance_data_.data());
        const size_t total = instance_data_.size() * sizeof(float);
        size_t i = 0;
        for(; i + 8u <= total; i += 8u)
        {
            uint64_t lane;
            std::memcpy(&lane, bytes + i, sizeof(lane));
            fingerprint = (fingerprint ^ lane) * 1099511628211ull;
        }
        for(; i < total; ++i)
        {
            fingerprint = (fingerprint ^ bytes[i]) * 1099511628211ull;
        }
    }
    const uint32_t required_vec4 = math::max(uint32_t(instance_data_.size() / 4u), 1u);
    bool recreated = false;
    if(!bgfx::isValid(instance_buffer_) || required_vec4 > instance_buffer_capacity_)
    {
        if(bgfx::isValid(instance_buffer_))
        {
            gfx::destroy(instance_buffer_);
        }
        // Grow with slack so a scene gaining a few instances per frame does not recreate the
        // buffer every frame.
        instance_buffer_capacity_ = required_vec4 + required_vec4 / 2u + 64u;
        instance_buffer_ = gfx::create_dynamic_vertex_buffer(instance_buffer_capacity_,
                                                             ANONYMOUS::get_vec4_buffer_layout(),
                                                             BGFX_BUFFER_COMPUTE_READ);
        recreated = true;
    }
    // Re-upload only what changed: a static scene keeps its instance set byte-identical frame
    // to frame, and re-staging it anyway is pure allocator pressure (see upload_instance_grid).
    if(!instance_data_.empty() && (recreated || fingerprint != instance_fingerprint_))
    {
        gfx::update(instance_buffer_,
                    0,
                    gfx::copy(instance_data_.data(), uint32_t(instance_data_.size() * sizeof(float))));
    }
    if(fingerprint != instance_fingerprint_)
    {
        ++content_revision_;
    }
    instance_fingerprint_ = fingerprint;
}

auto surface_cache_system::summarize_material(const material::sptr& mat) -> const material_summary&
{
    auto [it, inserted] = material_summaries_.try_emplace(mat.get());
    material_summary& summary = it->second;
    if(!inserted)
    {
        return summary;
    }
    // Colour lives on pbr_material, not on the material base. A material of some other kind -
    // or a submesh with none at all - keeps the neutral defaults rather than guessing, which is
    // strictly better than tinting the scene with a colour nothing is painted with.
    const auto* pbr = mat ? mat->safe_cast<pbr_material>() : nullptr;
    if(pbr == nullptr)
    {
        return summary;
    }
    summary.is_pbr = true;
    summary.is_blended = pbr->get_alpha_mode() == alpha_mode::blend;
    // Linear decode matches the G-buffer path (picker colors are sRGB-encoded).
    const auto base_color = pbr->get_base_color().to_linear();
    summary.albedo = math::vec3(base_color.value.r, base_color.value.g, base_color.value.b);
    // Pre-multiplied by intensity, so the shader stores radiance directly and never has to
    // know that emission is authored as a colour and a separate scale.
    const auto emissive_color = pbr->get_emissive_color().to_linear();
    summary.emissive = math::vec3(emissive_color.value.r, emissive_color.value.g, emissive_color.value.b) *
                       pbr->get_emissive_intensity();
    summary.emissive_luminance =
        0.2126f * summary.emissive.x + 0.7152f * summary.emissive.y + 0.0722f * summary.emissive.z;
    summary.metalness = math::clamp(pbr->get_metalness(), 0.0f, 1.0f);
    summary.mean_slot = acquire_texture_mean_slot(pbr->get_color_map(), summary.mean_captured);
    // The emissive MAP's mean, the same treatment the colour map gets above. Emission is a
    // source, so this is the difference between bouncing what a sign actually emits and
    // bouncing its factor across the whole submesh - see cs_gi_clipmap_attributes.sc.
    summary.emissive_mean_slot =
        acquire_texture_mean_slot(pbr->get_emissive_map(), summary.emissive_mean_captured);
    return summary;
}

void surface_cache_system::walk_scene(scene& scn)
{
    APP_SCOPE_PERF("GI/SurfaceCache/Walk Scene");
    material_summaries_.clear();
    scn.registry->view<transform_component, model_component, active_component>().each(
        [&](auto entity, auto&& transform_comp, auto&& model_comp, auto&& /*active*/)
        {
            const auto& mdl = model_comp.get_model();
            if(!mdl.is_valid())
            {
                return;
            }
            // LOD0 always. The field is already a coarse approximation of the surface, so
            // tracing against a simplified LOD would compound two independent approximations
            // and make occlusion depend on camera distance -- which would break world-space
            // stability, since the same wall would occlude differently as the camera moves.
            const auto mesh_handle = mdl.get_lod(0);
            if(!mesh_handle.is_ready())
            {
                return;
            }
            const auto mesh_ptr = mesh_handle.get();
            if(!mesh_ptr)
            {
                return;
            }
            // Place each submesh's OWN field wherever that submesh is DRAWN, which means making
            // the same per-submesh decision model::submit makes (see the has_transforms branch
            // in submit_for_batching):
            //
            //   - A submesh with mapped node transforms is drawn once at each of them. A model
            //     is an entity hierarchy, and the geometry hangs off child entities carrying
            //     submesh_component; model::submit uses those children's global transforms
            //     DIRECTLY, without composing them with the root, and importers routinely bake
            //     an axis convention into that child node. The transforms also differ BETWEEN
            //     submeshes on a real model, so one whole-mesh field placed at each transform
            //     in turn would duplicate the entire model once per transform.
            //   - A submesh with none is drawn at the model's own transform.
            //
            // The test has to be on THIS submesh's transform list. Testing whether the outer
            // list is populated instead reads as "the hierarchy resolved" and is true for a
            // primitive, whose pose is sized to the submesh count but never mapped, because
            // nothing carries a submesh_component -- so every primitive silently vanished from
            // GI while still rendering normally.
            const auto& submesh_transforms = model_comp.get_submesh_transforms();
            const math::mat4& world_transform = transform_comp.get_transform_global().get_matrix();
            const uint32_t sdf_count = mesh_ptr->get_sdf_count();
            const uint64_t entity_key = uint64_t(static_cast<uint32_t>(entity)) << 32u;
            const size_t submesh_count = mesh_ptr->get_submeshes_count();
            for(uint32_t submesh_index = 0; submesh_index < uint32_t(submesh_count); ++submesh_index)
            {
                const auto* submesh = mesh_ptr->get_submesh(submesh_index);
                if(submesh == nullptr)
                {
                    continue;
                }
                // ONE material resolve per submesh (every instance of a submesh is drawn with the
                // same material), decoded once per material per frame, and shared by both paths
                // below. Hoisted above acquire_field because the material can veto the placement
                // outright, and a vetoed submesh must not take an atlas slot.
                const material_summary& material =
                    summarize_material(resolve_submesh_material(mdl, *mesh_ptr, submesh_index));
                // THE ONE DEFINITION of "has a field", so the two paths below stay disjoint and no
                // submesh falls through both. Skinned submeshes never place a field, even when
                // the compiled asset carries one (assets baked before the compiler learned to
                // refuse them still do): the field is bind-pose geometry and pinning it to the
                // entity's root transform drags a rigid statue through the clipmap in a pose the
                // character is not in - deforming geometry receives GI without contributing.
                // Alpha-blended submeshes never occlude either: a blended surface transmits
                // light, so a field there blocks bounces that should pass straight through -
                // glass that darkens the room behind it. Decided here rather than only at bake
                // time because the material is a property of the INSTANCE (a component can
                // override it), so the same compiled mesh may be opaque in one placement and
                // blended in another. Cutout stays: it is opaque wherever it is not discarded.
                const bool has_field =
                    submesh_index < sdf_count && !submesh->skinned && !material.is_blended;
                const uint64_t submesh_key = entity_key | (uint64_t(submesh_index) << 16u);
                if(!has_field)
                {
                    // EMISSIVE SUBMESHES WITHOUT A FIELD (no baked SDF, skinned, or blended - a
                    // screen, a hologram, a glowing decal) are not voxelised: they light the gather
                    // through the screen tier alone. The light one leaves behind when it moves is
                    // exactly the residual the dirty regions flush, and without a placement it
                    // never registered one - an emissive moved in the editor kept its old glow on
                    // the walls until the camera moved (the screen-history loop's memory). They are
                    // tracked here with their drawn bounds.
                    if(!material.is_pbr ||
                       material.emissive_luminance < float(gi::GI_EMISSIVE_NEE_MIN_LUMINANCE))
                    {
                        continue;
                    }
                    const auto track_drawn = [&](uint64_t identity, const math::mat4& local_to_world)
                    {
                        auto [tracked, inserted] = acquire_tracked(identity);
                        record_placement(tracked,
                                         inserted,
                                         compute_placement_hash(hash_matrix(local_to_world),
                                                                material.albedo,
                                                                material.emissive),
                                         material.emissive,
                                         transform_bounds(local_to_world, submesh->bbox));
                    };
                    if(!submesh_transforms.has_transforms(submesh_index))
                    {
                        track_drawn(submesh_key, world_transform);
                        continue;
                    }
                    const size_t drawn_transform_count = submesh_transforms.get_transform_count(submesh_index);
                    for(size_t instance_index = 0; instance_index < drawn_transform_count; ++instance_index)
                    {
                        const math::mat4* transform_ptr =
                            submesh_transforms.get_transform(submesh_index, instance_index);
                        if(transform_ptr != nullptr)
                        {
                            track_drawn(submesh_key | (uint64_t(instance_index) & 0xFFFFu), *transform_ptr);
                        }
                    }
                    continue;
                }
                // World bounds of the submesh at the ENTITY's transform. A submesh drawn at several
                // node transforms is banded by the entity rather than per placement: the field is
                // shared by all of them, so there is one level to choose, and the placements are
                // offsets within one model rather than scattered across the world.
                const math::bbox submesh_world_bounds = transform_bounds(world_transform, submesh->bbox);
                const auto acquired = acquire_field(mesh_handle.uid(),
                                                    *mesh_ptr,
                                                    submesh_index,
                                                    compute_wanted_mip(submesh_world_bounds));
                const uint32_t header_index = acquired.header_index;
                if(header_index == sdf_atlas::invalid_index)
                {
                    continue;
                }
                // The level the atlas actually took, which under pressure is not the finest one.
                // Its bounds and voxel differ from the finest level's, and those are what the
                // placement and the tracer must agree on.
                const auto& sdf = mesh_ptr->get_sdf(submesh_index, acquired.mip_level);
                if(!submesh_transforms.has_transforms(submesh_index))
                {
                    add_instance(submesh_key, header_index, sdf, world_transform, mesh_ptr, material);
                    continue;
                }
                const size_t transform_count = submesh_transforms.get_transform_count(submesh_index);
                for(size_t instance_index = 0; instance_index < transform_count; ++instance_index)
                {
                    // Null for an inactive or out-of-range instance, which is the same accessor
                    // and therefore the same answer the renderer gets: a submesh switched off is
                    // not drawn, so it must not occlude or bounce light either.
                    const math::mat4* transform_ptr =
                        submesh_transforms.get_transform(submesh_index, instance_index);
                    if(transform_ptr == nullptr)
                    {
                        continue;
                    }
                    add_instance(submesh_key | (uint64_t(instance_index) & 0xFFFFu),
                                 header_index,
                                 sdf,
                                 *transform_ptr,
                                 mesh_ptr,
                                 material);
                }
            }
        });
}

void surface_cache_system::update_world(scene& scn)
{
    APP_SCOPE_PERF("GI/SurfaceCache/Update World");
    // The debug views keep this alive even with GI off, but an unsupported backend has nothing
    // to keep alive: no dispatch downstream can consume what this uploads.
    if(!supported_)
    {
        return;
    }
    // Once per frame, however many cameras ask. All of this is a function of the scene, so a second
    // camera would rebuild an identical instance list and re-upload an identical grid.
    const uint64_t frame = uint64_t(gfx::get_render_frame());
    if(world_frame_ == frame)
    {
        return;
    }
    world_frame_ = frame;
    // Before anything is placed: if the atlas ran out since the last frame, drop the whole scene a
    // level first. Doing it here rather than inside the walk is the point -- the walk sees one
    // field at a time and cannot know the scene overruns until it already has.
    apply_atlas_pressure();
    // Every camera, not the one rendering: residency is shared, so the level a field gets must be
    // a function of the world. Gathered before any placement so compute_wanted_mip sees them all.
    camera_positions_.clear();
    scn.registry->view<transform_component, camera_component>().each(
        [&](auto /*entity*/, auto&& camera_transform, auto&& /*camera*/)
        {
            camera_positions_.push_back(camera_transform.get_transform_global().get_position());
        });
    instances_.clear();
    clipmap_instances_.clear();
    clipmap_keepalive_.clear();
    if(!is_enabled())
    {
        return;
    }
    walk_scene(scn);
    // Swept AFTER the walk, so the set of what is still wanted is complete. The cost is that a
    // scene change takes one extra frame to show GI: the incoming meshes are refused while the
    // outgoing ones still hold their bricks, and succeed on the retry once this has run. Sweeping
    // first would need the whole scene walked twice to know what to keep.
    release_unused_fields();
    rebuild_dirty_regions();
    atlas_.flush();
    light_buffer_.update(scn);
    rebuild_emitters();
    upload_instances();
    upload_instance_grid();
    // The cascade is deliberately NOT composed here. It is centred on a viewer, so it belongs to
    // the camera rather than to the world -- see surface_cache_view.
}

} // namespace unravel
