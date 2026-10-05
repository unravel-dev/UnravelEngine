#include "surface_cache_system.h"

#include <engine/rendering/gi/gi_constants.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>

#include <concurrency/parallel.h>
#include <logging/logging.h>

namespace
{
namespace ANONYMOUS
{
/// Cells of the tracers' instance grid along the scene's longest axis (sdf_instance_grid defaults to
/// 32). A mesh-exact walk pays for every cell it crosses as well as for every instance a cell lists:
/// more cells make the cell traversal dominate, while fewer make long reflection and probe rays walk
/// fuller cells; 16 balances the two. Lossless: every resolution lists the same instances for a ray.
constexpr uint32_t instance_grid_resolution = 16u;
/// Longest cell edge of that grid, in metres: a scene longer than instance_grid_resolution x this (a city
/// block, not a courtyard) gets more cells instead of longer ones. A fixed count along the longest axis
/// would make cells list the instances of whole streets, and every ray crossing one pays a bounds test per
/// listed instance; much smaller cells stop paying off near the mesh trace ranges, where a march restarts
/// in every cell it crosses.
constexpr float instance_grid_max_cell_size = 6.0f;

/// Layout of the instance buffer: a flat array of vec4, matching BUFFER_RO(_, vec4, _).
auto get_vec4_buffer_layout() -> const bgfx::VertexLayout&
{
    static const bgfx::VertexLayout layout = []()
    {
        bgfx::VertexLayout decl;
        decl.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
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

/// FNV-1a over the placement identities in order (surface_cache_system::get_instance_order_hash).
constexpr uint64_t instance_order_hash_seed = 1469598103934665603ull;
constexpr uint64_t instance_order_hash_prime = 1099511628211ull;
} // namespace ANONYMOUS
} // namespace

namespace unravel
{

auto surface_cache_system::init(rtti::context& ctx) -> bool
{
    // The whole feature is compute-shaped: the clipmap compose and every Lumen pass are
    // dispatches, and even the debug views read SSBOs. A backend without compute (for example
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
    grid_settings.resolution = ANONYMOUS::instance_grid_resolution;
    grid_settings.max_cell_size = ANONYMOUS::instance_grid_max_cell_size;
    grid_.init(grid_settings);
    if(!light_buffer_.init())
    {
        APPLOG_WARNING("[SurfaceCache] Light buffer initialisation failed. Traced hits cannot be lit.");
    }
    return true;
}

auto surface_cache_system::deinit(rtti::context& ctx) -> bool
{
    instances_.clear();
    lumen_sources_.clear();
    clipmap_instances_.clear();
    clipmap_keepalive_.clear();
    residency_.clear();
    if(bgfx::isValid(instance_buffer_))
    {
        bgfx::destroy(instance_buffer_);
        instance_buffer_ = {bgfx::kInvalidHandle};
    }
    instance_buffer_capacity_ = 0;
    instance_data_.clear();
    if(bgfx::isValid(grid_buffer_))
    {
        bgfx::destroy(grid_buffer_);
        grid_buffer_ = {bgfx::kInvalidHandle};
    }
    grid_capacity_ = 0;
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
            return complete_acquired_field(record, m, submesh_index);
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
    // that occludes rays and steals attribution over a whole neighbourhood. The bake cannot do
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
    // Finest level that FITS, not finest level full stop. Without a chain, a scene whose fields do
    // not all fit the shared atlas would lose whole submeshes from GI -- silently in the image,
    // since a missing occluder just leaks light somewhere else. With a chain the same scene loses
    // RESOLUTION instead: each level down holds about a quarter of the bricks and reaches twice as
    // far before saturating, which for a distant or small submesh is a trade nobody sees.
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
            return complete_acquired_field(record, m, submesh_index);
        }
    }
    return {};
}

auto surface_cache_system::complete_acquired_field(mesh_residency& record, const mesh& m, uint32_t submesh_index)
    -> acquired_field
{
    acquired_field result{record.header_index, record.resident_mip};
    const uint32_t mip_count = m.get_sdf_mip_count(submesh_index);
    const uint32_t coarsest = mip_count > 0 ? mip_count - 1 : 0;
    if(record.resident_mip >= coarsest)
    {
        // The traced level is the coarsest: a separate copy would only hold atlas space.
        if(record.coarse_header_index != sdf_atlas::invalid_index)
        {
            atlas_.release(record.coarse_header_index);
            record.coarse_header_index = sdf_atlas::invalid_index;
            ++content_revision_;
        }
        return result;
    }
    if(record.coarse_header_index == sdf_atlas::invalid_index)
    {
        // Attempted once per atlas release generation, as the traced level is: a refusal for want of room only
        // changes when something is released.
        const auto& coarse = m.get_sdf(submesh_index, coarsest);
        const uint32_t generation = atlas_.get_release_generation();
        if(coarse.is_valid() && atlas_.has_upload_budget(coarse) && record.coarse_attempt_generation != generation)
        {
            record.coarse_attempt_generation = generation;
            record.coarse_header_index = atlas_.upload(coarse);
            if(record.coarse_header_index != sdf_atlas::invalid_index)
            {
                ++content_revision_;
            }
        }
    }
    if(record.coarse_header_index != sdf_atlas::invalid_index)
    {
        result.coarse_header_index = record.coarse_header_index;
        result.coarse_mip_level = coarsest;
    }
    return result;
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
        if(entry.second.coarse_header_index != sdf_atlas::invalid_index)
        {
            atlas_.release(entry.second.coarse_header_index);
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
        if(it->second.coarse_header_index != sdf_atlas::invalid_index)
        {
            ++content_revision_;
            atlas_.release(it->second.coarse_header_index);
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

namespace
{
/// One FNV-1a round over a float's bits. Component by component, never over sizeof:
/// math::vec3 carries alignment padding whose bytes are indeterminate, and hashing them would
/// make every static placement read as moved every frame.
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

/// FNV-1a over a placement's sixteen matrix floats, the start of its pose cache key.
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

void surface_cache_system::sweep_tracked_placements()
{
    // A placement not drawn this frame drops its cached pose; one that returns recomputes it.
    for(auto it = tracked_placements_.begin(); it != tracked_placements_.end();)
    {
        it = it->second.seen_frame == world_frame_ ? std::next(it) : tracked_placements_.erase(it);
    }
}

void surface_cache_system::add_instance(uint64_t identity,
                                         const placed_field& field,
                                         const math::mat4& local_to_world,
                                         const std::shared_ptr<mesh>& owner,
                                         const material_summary& material)
{
    const mesh_sdf& sdf = *field.sdf;
    instance inst;
    // Decoded once per material per frame (summarize_material).
    inst.is_two_sided = material.is_two_sided;
    inst.is_emissive_light_source =
        material.is_pbr && material.emissive_luminance >= float(gi::GI_EMISSIVE_LIGHT_SOURCE_MIN_LUMINANCE);
    inst.local_to_world = local_to_world;
    inst.header_index = field.header_index;
    inst.coarse_header_index =
        field.coarse_header_index != sdf_atlas::invalid_index ? field.coarse_header_index : field.header_index;
    // Chained, unlike the content fingerprints: it must change when the same placements arrive in another order.
    instance_order_hash_ = (instance_order_hash_ ^ identity) * ANONYMOUS::instance_order_hash_prime;
    // POSE CACHE: the inverse, the smallest scale axis and the transformed bounds are pure
    // functions of the transform and the field's local bounds, and a static placement
    // presents the same pair every frame - the tracker keys placements by identity, so it
    // carries them across frames. Recomputed only when the key moves; a fresh record has no
    // pose to offer.
    const uint64_t pose_key = compute_pose_key(hash_matrix(local_to_world), sdf.bounds);
    auto& tracked = tracked_placements_[identity];
    if(tracked.has_pose && tracked.pose_key == pose_key)
    {
        inst.world_to_local = tracked.world_to_local;
        inst.local_to_world_scale = tracked.local_to_world_scale;
        inst.axis_scale = tracked.axis_scale;
        inst.world_bounds = tracked.world_bounds;
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
        inst.axis_scale = math::max(math::vec3(scale_x, scale_y, scale_z), math::vec3(1e-6f));
        // World-space AABB of the field's local bounds, for the tracer's broad phase: the exact
        // box of the transformed corners, so a rotated instance still gets a bound that
        // contains it.
        inst.world_bounds = transform_bounds(local_to_world, sdf.bounds);
    }
    instances_.push_back(inst);
    tracked.pose_key = pose_key;
    tracked.has_pose = true;
    tracked.world_to_local = inst.world_to_local;
    tracked.local_to_world_scale = inst.local_to_world_scale;
    tracked.axis_scale = inst.axis_scale;
    tracked.world_bounds = inst.world_bounds;
    tracked.seen_frame = world_frame_;
    // The clipmap composer borrows a raw mesh_sdf pointer, so the owning mesh has to be kept
    // alive for as long as the composition input list references it. Consecutive placements of
    // one mesh share an entry, so a crowd of one mesh does not push the same pointer once per
    // placement. A repeat is harmless and an omission is not, so only the previous entry is
    // compared.
    if(clipmap_keepalive_.empty() || clipmap_keepalive_.back().get() != owner.get())
    {
        clipmap_keepalive_.push_back(owner);
    }
    global_sdf_instance clipmap_instance;
    clipmap_instance.sdf = &sdf;
    clipmap_instance.coarse_sdf = field.coarse_sdf;
    clipmap_instance.world_to_local = inst.world_to_local;
    clipmap_instance.world_bounds = inst.world_bounds;
    clipmap_instance.local_to_world_scale = inst.local_to_world_scale;
    clipmap_instance.axis_scale = inst.axis_scale;
    clipmap_instances_.push_back(clipmap_instance);
}

void surface_cache_system::upload_instance_grid()
{
    APP_SCOPE_PERF("GI/SurfaceCache/Upload Instance Grid");
    // The grid is a pure function of the instance set (bounds are part of the packed data the
    // fingerprint covers), so an unchanged fingerprint means an identical grid: skip the CPU
    // rebuild and the multi-megabyte re-upload. Without this a static scene would re-stage the
    // whole structure every frame, which on a large scene alone keeps the Vulkan backend
    // allocating staging memory continuously.
    if(grid_uploaded_fingerprint_ == instance_fingerprint_ && grid_.is_valid() &&
       bgfx::isValid(grid_buffer_))
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
    const auto ensure_capacity = [](bgfx::DynamicIndexBufferHandle& buffer,
                                    uint32_t& capacity,
                                    uint32_t required) -> void
    {
        if(bgfx::isValid(buffer) && required <= capacity)
        {
            return;
        }
        if(bgfx::isValid(buffer))
        {
            bgfx::destroy(buffer);
        }
        // Grow with slack, so a scene gaining a few instances per frame does not recreate the
        // buffer every frame.
        capacity = required + required / 2u + 64u;
        buffer = bgfx::createDynamicIndexBuffer(capacity, BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_INDEX32);
    };
    // One buffer, one upload: the offsets then the instance list (sdf_common.sh b_sdf_grid).
    grid_upload_.clear();
    grid_upload_.reserve(offsets.size() + cell_instances.size());
    grid_upload_.insert(grid_upload_.end(), offsets.begin(), offsets.end());
    grid_upload_.insert(grid_upload_.end(), cell_instances.begin(), cell_instances.end());
    ensure_capacity(grid_buffer_, grid_capacity_, math::max(uint32_t(grid_upload_.size()), 1u));
    if(!bgfx::isValid(grid_buffer_))
    {
        return;
    }
    bgfx::update(grid_buffer_, 0, bgfx::copy(grid_upload_.data(), uint32_t(grid_upload_.size() * sizeof(uint32_t))));
    const auto& origin = grid_.get_origin();
    const auto& dim = grid_.get_dim();
    grid_params_[0] = origin.x;
    grid_params_[1] = origin.y;
    grid_params_[2] = origin.z;
    grid_params_[3] = grid_.get_cell_size();
    grid_params_[4] = float(dim.x);
    grid_params_[5] = float(dim.y);
    grid_params_[6] = float(dim.z);
    // The instance list's base entry doubles as the enable flag (the offsets hold at least
    // the terminator, so it is never zero for a valid grid).
    grid_params_[7] = float(offsets.size());
}

void surface_cache_system::upload_instances()
{
    APP_SCOPE_PERF("GI/SurfaceCache/Upload Instances");
    // resize, not assign: every float of every record is written below, so an assign's
    // zero-fill would be fully overwritten every frame.
    instance_data_.resize(size_t(instances_.size()) * instance_vec4_stride * 4u);
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
            // Lane 8: x = flags (1 two-sided: the Lumen global SDF's coverage; 2 emissive light source: kept by the
            // Lumen cascade however small), yzw = the world length of each local axis (SdfInstanceWorldDistance's
            // per-axis bounds). MIRROR OF SdfLoadInstance.
            dst[32] = (inst.is_two_sided ? 1.0f : 0.0f) + (inst.is_emissive_light_source ? 2.0f : 0.0f);
            dst[33] = inst.axis_scale.x;
            dst[34] = inst.axis_scale.y;
            dst[35] = inst.axis_scale.z;
            // Lane 9: x = the coarse header (instance::coarse_header_index; the traced header when there is none).
            dst[36] = float(inst.coarse_header_index);
            dst[37] = 0.0f;
            dst[38] = 0.0f;
            dst[39] = 0.0f;
        });
    // Content hash over the exact bytes the GPU receives: any change to a transform, a flag,
    // bounds, or field index flips it. Eight bytes per round rather than a byte-serial
    // FNV chain, whose dependent multiplies would cost the main thread real time every frame
    // just to decide to skip an upload. FNV-1a over 64-bit lanes keeps the same "any byte
    // flips it" property at an eighth of the rounds.
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
            bgfx::destroy(instance_buffer_);
        }
        // Grow with slack so a scene gaining a few instances per frame does not recreate the
        // buffer every frame.
        instance_buffer_capacity_ = required_vec4 + required_vec4 / 2u + 64u;
        instance_buffer_ = bgfx::createDynamicVertexBuffer(instance_buffer_capacity_,
                                                           ANONYMOUS::get_vec4_buffer_layout(),
                                                           BGFX_BUFFER_COMPUTE_READ);
        recreated = true;
    }
    // Re-upload only what changed: a static scene keeps its instance set byte-identical frame
    // to frame, and re-staging it anyway is pure allocator pressure (see upload_instance_grid).
    if(!instance_data_.empty() && (recreated || fingerprint != instance_fingerprint_))
    {
        bgfx::update(instance_buffer_,
                     0,
                     bgfx::copy(instance_data_.data(), uint32_t(instance_data_.size() * sizeof(float))));
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
    summary.is_two_sided = mat && mat->get_cull_type() == cull_type::none;
    // Blending and emission live on pbr_material, not on the material base. A material of some
    // other kind - or a submesh with none at all - keeps the defaults: opaque, not emissive.
    const auto* pbr = mat ? mat->safe_cast<pbr_material>() : nullptr;
    if(pbr == nullptr)
    {
        return summary;
    }
    summary.is_pbr = true;
    summary.is_blended = pbr->get_alpha_mode() == alpha_mode::blend;
    // Linear decode matches the G-buffer path (picker colors are sRGB-encoded), times the intensity.
    const auto emissive_color = pbr->get_emissive_color().to_linear();
    const math::vec3 emissive = math::vec3(emissive_color.value.r, emissive_color.value.g, emissive_color.value.b) *
                                pbr->get_emissive_intensity();
    summary.emissive_luminance = 0.2126f * emissive.x + 0.7152f * emissive.y + 0.0722f * emissive.z;
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
            // nothing carries a submesh_component -- so every primitive would silently vanish
            // from GI while still rendering normally.
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
                const material::sptr submesh_material = resolve_submesh_material(mdl, *mesh_ptr, submesh_index);
                const material_summary& material = summarize_material(submesh_material);
                // THE ONE DEFINITION of "has a field", so the two paths below stay disjoint and no
                // submesh falls through both. Skinned submeshes never place a field, even when
                // the compiled asset carries one: the field is bind-pose geometry and pinning it
                // to the entity's root transform drags a rigid statue through the clipmap in a
                // pose the character is not in - deforming geometry receives GI without
                // contributing.
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
                const placed_field field{header_index,
                                         &sdf,
                                         acquired.coarse_header_index,
                                         acquired.coarse_header_index != sdf_atlas::invalid_index
                                             ? &mesh_ptr->get_sdf(submesh_index, acquired.coarse_mip_level)
                                             : nullptr};
                // Every placement of the submesh shares one card set (built on first sight).
                const bool two_sided = submesh_material && submesh_material->get_cull_type() == cull_type::none;
                const auto cards = lumen_cards_.acquire(mesh_ptr, mesh_handle.uid(), submesh_index, two_sided);
                // Called right after add_instance, so the placement's GI instance is the last one.
                const auto add_lumen_source = [&](uint64_t identity, const math::mat4& local_to_world)
                {
                    lumen_sources_.push_back({identity,
                                              uint32_t(instances_.size() - 1u),
                                              mesh_ptr,
                                              submesh_index,
                                              submesh_material,
                                              local_to_world,
                                              cards});
                };
                if(!submesh_transforms.has_transforms(submesh_index))
                {
                    add_instance(submesh_key, field, world_transform, mesh_ptr, material);
                    add_lumen_source(submesh_key, world_transform);
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
                                 field,
                                 *transform_ptr,
                                 mesh_ptr,
                                 material);
                    add_lumen_source(submesh_key | (uint64_t(instance_index) & 0xFFFFu), *transform_ptr);
                }
            }
        });
}

void surface_cache_system::update_global_lighting_state(scene& scn)
{
    float sun = 0.0f;
    scn.registry->view<light_component, active_component>().each(
        [&](auto /*entity*/, auto&& light_comp, auto&& /*active*/)
        {
            const auto& light = light_comp.get_light();
            if(light.type == light_type::directional && sun <= 0.0f)
            {
                const auto color = light.color.to_linear();
                sun = light.intensity * math::max(color.value.r, math::max(color.value.g, color.value.b));
            }
        });
    float sky = 0.0f;
    scn.registry->view<skylight_component, active_component>().each(
        [&](auto /*entity*/, auto&& skylight, auto&& /*active*/) { sky += math::max(skylight.get_irradiance_intensity(), 0.0f); });
    const auto is_global_change = [](float before, float now)
    {
        const float ratio = math::max(before, global_lighting_epsilon) / math::max(now, global_lighting_epsilon);
        return ratio > global_lighting_change_ratio || ratio < 1.0f / global_lighting_change_ratio;
    };
    has_global_lighting_change_ = is_global_change(global_sun_, sun) || is_global_change(global_sky_, sky);
    global_sun_ = sun;
    global_sky_ = sky;
}

void surface_cache_system::update_world(scene& scn)
{
    APP_SCOPE_PERF("GI/SurfaceCache/Update World");
    // An unsupported backend runs no dispatch that could consume what this uploads.
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
    // Diagnostic (experiment bit 1 << 31): the residency keeps the camera positions it last saw.
    constexpr uint32_t experiment_freeze_residency = 1u << 31u;
    if((experiment_flags_ & experiment_freeze_residency) == 0u || camera_positions_.empty())
    {
        camera_positions_.clear();
        scn.registry->view<transform_component, camera_component, active_component>().each(
            [&](auto /*entity*/, auto&& camera_transform, auto&& /*camera*/, auto&& /*active*/)
            {
                camera_positions_.push_back(camera_transform.get_transform_global().get_position());
            });
    }
    instances_.clear();
    lumen_sources_.clear();
    clipmap_instances_.clear();
    clipmap_keepalive_.clear();
    instance_order_hash_ = ANONYMOUS::instance_order_hash_seed;
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
    sweep_tracked_placements();
    // The lights the surface cache's direct lighting reads.
    light_buffer_.update(scn);
    update_global_lighting_state(scn);
    atlas_.flush();
    upload_instances();
    upload_instance_grid();
    // The cascade is deliberately NOT composed here. It is centred on a viewer, so it belongs to
    // the camera rather than to the world -- see surface_cache_view.
}

} // namespace unravel
