// Like mesh_sdf_baker, this translation unit depends only on math and the standard library so
// the composer can be validated without the ECS, the asset layer, or a GPU.
#include "global_sdf_clipmap.h"

#include <engine/profiler/profiler.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/lumen_constants.h>
#include <engine/rendering/gi/mesh_sdf_baker.h>
#include <engine/rendering/gi/sdf_instance_grid.h>

#include <concurrency/parallel.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <numeric>

namespace unravel
{
namespace
{

/// Returned outside every cascade level. Large enough that a trace takes one long step rather
/// than crawling, but finite so it never poisons arithmetic with an infinity.
///
/// Aliases the public constant rather than repeating the value: the composer writes it and the
/// sampler tests against it, and a mismatch would make "nothing reached this voxel" and "this
/// level does not cover the position" stop comparing equal.
constexpr float outside_clipmap_distance = global_sdf_clipmap::outside_distance;

/// Voxels per edge of a composition cull cell. Small enough that a cell holds only instances a
/// voxel could plausibly reach, large enough that the grid stays a small fraction of the volume
/// it accelerates -- at 4 voxels a 64^3 level bins into 16^3 cells.
constexpr uint32_t voxels_per_cull_cell = 4u;

/**
 * @brief Encodes a distance, given in voxels, into the R8 storage representation.
 * Identical convention to the mesh field so both decode the same way.
 */
auto encode_clipmap_distance(float distance_in_voxels, float encode_range) -> uint8_t
{
    const float normalized = distance_in_voxels / (2.0f * encode_range) + 0.5f;
    return uint8_t(math::clamp(normalized, 0.0f, 1.0f) * 255.0f + 0.5f);
}

auto decode_clipmap_distance(uint8_t encoded, float encode_range) -> float
{
    return (float(encoded) / 255.0f - 0.5f) * (2.0f * encode_range);
}

} // namespace

void global_sdf_clipmap::init(const settings& settings)
{
    settings_ = settings;
    settings_.resolution = math::max(settings_.resolution, 8u);
    settings_.base_extent = math::max(settings_.base_extent, 0.01f);
    settings_.level_scale = math::max(settings_.level_scale, 1.5f);
    // Level geometry is changing, so cached fingerprints describe boxes that no longer exist.
    cached_instances_revision_ = 0;
    update_counter_ = 0;
    last_compose_counter_.fill(-int64_t(gi::GI_CLIPMAP_EDIT_THROTTLE_FRAMES));
    last_content_seen_.fill(-int64_t(gi::GI_CLIPMAP_EDIT_THROTTLE_FRAMES));
    for(uint32_t i = 0; i < level_count; ++i)
    {
        auto& lvl = levels_[i];
        lvl.voxel_size = get_level_extent(i) / float(settings_.resolution);
        // The CPU composer's voxels only: a GPU-composed level lives in the GPU mirror alone. Start saturated
        // positive: an empty world reads as "nothing anywhere near", which is the conservative answer and keeps
        // traces marching instead of hitting at the origin.
        std::vector<uint8_t>().swap(lvl.voxels);
        if(!settings_.compose_on_gpu)
        {
            lvl.voxels.assign(size_t(settings_.resolution) * settings_.resolution * settings_.resolution, uint8_t(255));
        }
        // Deliberately not a valid snapped origin, so the first update always composes - in full: nothing composed
        // under the previous settings may be scrolled into the new layout.
        lvl.origin = math::vec3(std::numeric_limits<float>::max());
        lvl.content_fingerprint = 0;
        lvl.stale_updates = 0;
        lvl.composed_entries.clear();
        lvl.has_composed_entries = false;
        lvl.is_partial = false;
        lvl.scroll_shift = math::ivec3(0);
        lvl.partial_boxes.clear();
        cached_target_entries_[i].clear();
    }
}

auto global_sdf_clipmap::get_level_extent(uint32_t index) const -> float
{
    return settings_.base_extent * std::pow(settings_.level_scale, float(index));
}

auto global_sdf_clipmap::get_memory_usage() const -> size_t
{
    size_t total = 0;
    for(const auto& lvl : levels_)
    {
        total += lvl.voxels.size();
    }
    return total;
}

auto global_sdf_clipmap::compute_level_bounds(uint32_t index, const math::vec3& origin) const -> math::bbox
{
    const float extent = get_level_extent(index);
    return math::bbox(origin, origin + math::vec3(extent));
}

auto global_sdf_clipmap::compute_level_reach(uint32_t index) const -> float
{
    return settings_.encode_range * levels_[index].voxel_size;
}

auto global_sdf_clipmap::compute_instance_entry_hash(const global_sdf_instance& instance) -> uint64_t
{
    // Placement AND identity. A pure move changes no count and no membership, so hashing
    // either alone would miss it entirely -- the object would go on occluding and lighting
    // from where it used to be, with nothing downstream able to recover.
    uint64_t entry = 0xcbf29ce484222325ull;
    const auto* words = reinterpret_cast<const uint32_t*>(&instance.world_to_local);
    constexpr size_t word_count = sizeof(instance.world_to_local) / sizeof(uint32_t);
    for(size_t i = 0; i < word_count; ++i)
    {
        entry = (entry ^ uint64_t(words[i])) * 0x100000001b3ull;
    }
    entry = (entry ^ reinterpret_cast<uintptr_t>(instance.sdf)) * 0x100000001b3ull;
    entry = (entry ^ reinterpret_cast<uintptr_t>(instance.coarse_sdf)) * 0x100000001b3ull;
    return entry;
}

void global_sdf_clipmap::refresh_instance_entry_hashes(const std::vector<global_sdf_instance>& instances,
                                                       uint64_t instances_revision)
{
    // The per-instance entry is independent of the level; only the membership test is per
    // level. Hashing it once per content revision instead of once per level per frame is
    // what keeps a mover's frame from paying four walks of the whole instance list. Revision
    // 0 means "unknown" and refreshes every call.
    const bool current = instances_revision != 0 && instances_revision == instance_entry_hash_revision_ &&
                         instance_entry_hashes_.size() == instances.size();
    if(current)
    {
        return;
    }
    instance_entry_hashes_.resize(instances.size());
    for(size_t i = 0; i < instances.size(); ++i)
    {
        const auto& instance = instances[i];
        // is_sampleable, not is_valid: the thorough check walks every indirection entry. The
        // field was already validated in full when it became resident
        // (surface_cache_system::acquire_field), so re-proving it per frame buys nothing and
        // costs the brick count times four times the instance count, every frame.
        const bool sampleable = instance.sdf != nullptr && instance.sdf->is_sampleable();
        instance_entry_hashes_[i] = sampleable ? compute_instance_entry_hash(instance) : 0ull;
        instance_entry_sampleable_.resize(instances.size());
        instance_entry_sampleable_[i] = sampleable ? 1u : 0u;
    }
    instance_entry_hash_revision_ = instances_revision;
}

auto global_sdf_clipmap::compute_level_influence(uint32_t index) const -> float
{
    const float voxels = math::max(settings_.encode_range, float(gi::lumen::LUMEN_GLOBAL_SDF_COVERAGE_BAND_VOXELS));
    return voxels * levels_[index].voxel_size;
}

auto global_sdf_clipmap::collect_level_entries(const math::bbox& bounds,
                                               float reach,
                                               const std::vector<global_sdf_instance>& instances) const
    -> std::vector<composed_entry>
{
    std::vector<composed_entry> entries;
    // The entry hashes were refreshed for this instance list (refresh_instance_entry_hashes);
    // a list of another size means an unrefreshed call, which hashes in place - correct,
    // only slower.
    const bool cached = instance_entry_hashes_.size() == instances.size();
    for(size_t i = 0; i < instances.size(); ++i)
    {
        const auto& instance = instances[i];
        const bool sampleable = cached ? instance_entry_sampleable_[i] != 0u
                                       : (instance.sdf != nullptr && instance.sdf->is_sampleable());
        if(!sampleable)
        {
            continue;
        }
        math::bbox expanded = instance.world_bounds;
        expanded.inflate(reach);
        if(!expanded.intersect(bounds))
        {
            continue;
        }
        entries.push_back({cached ? instance_entry_hashes_[i] : compute_instance_entry_hash(instance),
                           instance.world_bounds});
    }
    // Sorted by hash: the scene traversal that produced the list has no guaranteed order, a reshuffle is not a
    // change, and two sorted sets diff in one walk (plan_partial_update).
    std::sort(entries.begin(),
              entries.end(),
              [](const composed_entry& a, const composed_entry& b)
              {
                  return a.hash < b.hash;
              });
    return entries;
}

auto global_sdf_clipmap::compute_entries_fingerprint(const std::vector<composed_entry>& entries) const -> uint64_t
{
    // Summed rather than chained, so the result does not depend on iteration order.
    uint64_t total = 0;
    for(const auto& entry : entries)
    {
        total += entry.hash;
    }
    // Mixed with the count so an empty level cannot collide with a populated one whose entries
    // happen to sum to zero, and with an object radius scale other than 1, which changes the objects the GPU composes
    // (a scale of 1 adds nothing, so an empty level keeps fingerprint 0).
    const uint64_t count = entries.size();
    const float radius_scale = settings_.object_radius_scale;
    // The float's bits through memcpy: std::bit_cast needs libstdc++ 11, older than some supported toolchains.
    uint32_t radius_scale_bits = 0;
    std::memcpy(&radius_scale_bits, &radius_scale, sizeof(radius_scale_bits));
    const uint64_t radius_scale_term = radius_scale == 1.0f ? 0u : uint64_t(radius_scale_bits) * 0xc2b2ae3d27d4eb4full;
    return total ^ (count * 0x9e3779b97f4a7c15ull) ^ radius_scale_term;
}

void global_sdf_clipmap::merge_overlapping_boxes(std::vector<voxel_box>& boxes)
{
    const auto volume = [](const math::ivec3& size) -> int64_t
    {
        return int64_t(size.x) * size.y * size.z;
    };
    bool merged = true;
    while(merged)
    {
        merged = false;
        for(size_t i = 0; i < boxes.size() && !merged; ++i)
        {
            for(size_t j = i + 1; j < boxes.size() && !merged; ++j)
            {
                const math::ivec3 box_min = math::min(boxes[i].min, boxes[j].min);
                const math::ivec3 box_max = math::max(boxes[i].min + boxes[i].size, boxes[j].min + boxes[j].size);
                if(volume(box_max - box_min) <= volume(boxes[i].size) + volume(boxes[j].size))
                {
                    boxes[i] = {box_min, box_max - box_min};
                    boxes.erase(boxes.begin() + std::ptrdiff_t(j));
                    merged = true;
                }
            }
        }
    }
}

auto global_sdf_clipmap::append_brick_boxes(const std::vector<voxel_box>& boxes,
                                            int brick_edge,
                                            uint32_t first_brick,
                                            std::vector<math::vec4>& table) -> uint32_t
{
    uint32_t bricks = 0;
    for(const auto& box : boxes)
    {
        const auto count = [brick_edge](int extent) -> uint32_t
        {
            return uint32_t((math::max(extent, 0) + brick_edge - 1) / brick_edge);
        };
        const uint32_t box_bricks = count(box.size.x) * count(box.size.y) * count(box.size.z);
        if(box_bricks == 0u)
        {
            continue;
        }
        table.emplace_back(float(box.min.x), float(box.min.y), float(box.min.z), float(first_brick + bricks));
        table.emplace_back(float(box.size.x), float(box.size.y), float(box.size.z), 0.0f);
        bricks += box_bricks;
    }
    return bricks;
}

auto global_sdf_clipmap::is_partial_update_due(uint32_t index, uint64_t update_index) -> bool
{
    const uint32_t per_update = math::clamp(uint32_t(gi::GI_CLIPMAP_PARTIAL_UPDATES_PER_FRAME), 1u, level_count);
    // The first per_update - 1 levels every update; the others at halving frequencies, the last at its predecessor's,
    // each with its own phase (2^n - 1), so about per_update levels update per frame.
    if(index + 1u < per_update)
    {
        return true;
    }
    const uint32_t rem = index - (per_update - 1u);
    const uint64_t frequency = index == level_count - 1u ? (uint64_t(1) << rem) : (uint64_t(2) << rem);
    const uint64_t phase = (uint64_t(1) << rem) - 1u;
    return update_index % frequency == phase;
}

auto global_sdf_clipmap::plan_partial_update(uint32_t index,
                                             const math::vec3& target_origin,
                                             const std::vector<composed_entry>& entries) const
    -> std::optional<level_plan>
{
    const auto& lvl = levels_[index];
    if(!settings_.partial_updates || !lvl.has_composed_entries)
    {
        return std::nullopt;
    }
    const int res = int(settings_.resolution);
    level_plan plan;
    plan.is_partial = true;
    if(target_origin != lvl.origin)
    {
        const math::vec3 shift_voxels = (target_origin - lvl.origin) / lvl.voxel_size;
        plan.scroll_shift = math::ivec3(int(std::lround(shift_voxels.x)),
                                        int(std::lround(shift_voxels.y)),
                                        int(std::lround(shift_voxels.z)));
        voxel_box overlap;
        std::array<voxel_box, 3> exposed;
        if(compute_scroll_boxes(plan.scroll_shift, settings_.resolution, overlap, exposed) == 0)
        {
            return std::nullopt;
        }
    }
    // The instances in exactly one of the two sets moved, appeared or left: both sets are sorted by hash, and a moved
    // instance is its old entry leaving and its new one arriving.
    const auto& before = lvl.composed_entries;
    std::vector<math::bbox> changed;
    size_t a = 0;
    size_t b = 0;
    while(a < before.size() || b < entries.size())
    {
        if(b == entries.size() || (a < before.size() && before[a].hash < entries[b].hash))
        {
            changed.push_back(before[a++].bounds);
        }
        else if(a == before.size() || entries[b].hash < before[a].hash)
        {
            changed.push_back(entries[b++].bounds);
        }
        else
        {
            ++a;
            ++b;
        }
        if(changed.size() > size_t(gi::GI_CLIPMAP_MAX_PARTIAL_INSTANCES))
        {
            return std::nullopt;
        }
    }
    // Each changed instance's reach in the new window, out to whole alignment blocks.
    const float influence = compute_level_influence(index);
    const int alignment = int(gi::GI_CLIPMAP_PARTIAL_BOX_ALIGNMENT);
    const auto window_limit = math::vec3(float(res));
    for(const auto& bounds : changed)
    {
        const math::vec3 low = math::clamp((bounds.min - math::vec3(influence) - target_origin) / lvl.voxel_size,
                                           math::vec3(0.0f),
                                           window_limit);
        const math::vec3 high = math::clamp((bounds.max + math::vec3(influence) - target_origin) / lvl.voxel_size,
                                            math::vec3(0.0f),
                                            window_limit);
        voxel_box box;
        bool is_empty = false;
        for(int axis = 0; axis < 3; ++axis)
        {
            const int box_min = (int(std::floor(low[axis])) / alignment) * alignment;
            const int box_max = math::min(((int(std::ceil(high[axis])) + alignment - 1) / alignment) * alignment, res);
            box.min[axis] = box_min;
            box.size[axis] = box_max - box_min;
            is_empty = is_empty || box.size[axis] <= 0;
        }
        if(!is_empty)
        {
            plan.partial_boxes.push_back(box);
        }
    }
    merge_overlapping_boxes(plan.partial_boxes);
    if(plan.partial_boxes.size() > size_t(gi::GI_CLIPMAP_MAX_PARTIAL_BOXES))
    {
        math::ivec3 box_min(res);
        math::ivec3 box_max(0);
        for(const auto& box : plan.partial_boxes)
        {
            box_min = math::min(box_min, box.min);
            box_max = math::max(box_max, box.min + box.size);
        }
        plan.partial_boxes.assign(1, voxel_box{box_min, box_max - box_min});
    }
    double voxels = 0.0;
    for(const auto& box : plan.partial_boxes)
    {
        voxels += double(box.size.x) * double(box.size.y) * double(box.size.z);
    }
    const double level_voxels = double(res) * double(res) * double(res);
    if(voxels > double(gi::GI_CLIPMAP_MAX_PARTIAL_FRACTION) * level_voxels)
    {
        return std::nullopt;
    }
    return plan;
}

auto global_sdf_clipmap::get_stale_level_count() const -> uint32_t
{
    uint32_t stale = 0;
    for(const auto& lvl : levels_)
    {
        if(lvl.stale_updates > 0)
        {
            ++stale;
        }
    }
    return stale;
}

auto global_sdf_clipmap::apply_settings(const settings& new_settings) -> bool
{
    // These change what a voxel MEANS. Everything else is read afresh by the next composition, so
    // assigning it is enough and costs nothing.
    const bool layout_changed = new_settings.resolution != settings_.resolution ||
                                new_settings.base_extent != settings_.base_extent ||
                                new_settings.level_scale != settings_.level_scale;
    if(layout_changed)
    {
        init(new_settings);
        return true;
    }
    // The objects the composition keeps changed: the fingerprints that include the scale are recomputed at the next
    // update, and no level may scroll its old voxels into place.
    if(new_settings.object_radius_scale != settings_.object_radius_scale)
    {
        cached_instances_revision_ = 0;
        for(auto& lvl : levels_)
        {
            lvl.has_composed_entries = false;
        }
    }
    settings_ = new_settings;
    return false;
}

auto global_sdf_clipmap::update(const std::vector<global_sdf_instance>& instances,
                                const math::vec3& camera_position,
                                uint64_t instances_revision) -> uint32_t
{
    APP_SCOPE_PERF("GI/Clipmap/Update");
    // One tick per update call - the clock of the edit-coalescing window and of the partial cadence below.
    ++update_counter_;
    // Whether the per-level set cache below may answer: only against the same
    // instance-content revision it was filled under, and never for revision 0 (unknown).
    const bool revision_cached =
        instances_revision != 0 && instances_revision == cached_instances_revision_;
    refresh_instance_entry_hashes(instances, instances_revision);
    // Pass one: decide what each level SHOULD be, how it gets there and how stale it is. Nothing is composed here,
    // so the budget below chooses between levels knowing all of them.
    std::array<math::vec3, level_count> target_origin{};
    std::array<std::optional<level_plan>, level_count> partial_plans{};
    for(uint32_t i = 0; i < level_count; ++i)
    {
        auto& lvl = levels_[i];
        if(!lvl.is_valid())
        {
            continue;
        }
        // Snap the centre to whole snaps of origin_snap_voxels voxels, then place the origin a half
        // extent away. Snapping is what makes the result a function of which snap cell the camera
        // is in rather than of its exact position - the basis of world stability - and a snap of
        // several voxels re-snaps (and recomposes) a level proportionally less often while the
        // camera moves, for half a snap of guaranteed-coverage margin at the window edge, which
        // the cross-fade band and the coarser level behind it absorb. (The resolution is even, so
        // a half extent is a whole number of voxels and the origin stays voxel-aligned.)
        const float extent = get_level_extent(i);
        const float snap_size = lvl.voxel_size * float(origin_snap_voxels);
        const math::vec3 snapped_center = math::floor(camera_position / snap_size) * snap_size;
        target_origin[i] = snapped_center - math::vec3(extent * 0.5f);
        // The instance set and its fingerprint are a pure function of (level bounds, instance content). With the
        // content revision and the target origin both unchanged, last frame's are the answer, and the full instance
        // walk is skipped.
        if(!(revision_cached && target_origin[i] == cached_target_origin_[i]))
        {
            cached_target_entries_[i] = collect_level_entries(compute_level_bounds(i, target_origin[i]),
                                                              compute_level_influence(i),
                                                              instances);
            cached_target_fingerprint_[i] = compute_entries_fingerprint(cached_target_entries_[i]);
        }
        cached_target_origin_[i] = target_origin[i];
        const uint64_t target_fingerprint = cached_target_fingerprint_[i];
        const bool origin_moved = target_origin[i] != lvl.origin;
        const bool contents_changed = target_fingerprint != lvl.content_fingerprint;
        const bool first_content = lvl.content_fingerprint == 0;
        const int64_t throttle = int64_t(gi::GI_CLIPMAP_EDIT_THROTTLE_FRAMES);
        const bool idle_start = update_counter_ - last_content_seen_[i] >= throttle;
        const bool window_open = update_counter_ - last_compose_counter_[i] >= throttle;
        if(contents_changed && !first_content)
        {
            last_content_seen_[i] = update_counter_;
        }
        if(!origin_moved && !contents_changed)
        {
            lvl.stale_updates = 0;
            continue;
        }
        // PARTIAL (level::is_partial): only the changed instances' reach and the exposed slabs, on the level's
        // cadence and outside the full-recompose budget; a level waiting for its turn is stale meanwhile.
        partial_plans[i] = plan_partial_update(i, target_origin[i], cached_target_entries_[i]);
        if(partial_plans[i])
        {
            ++lvl.stale_updates;
            continue;
        }
        // EDIT COALESCING (GI_CLIPMAP_EDIT_THROTTLE_FRAMES), leading-edge, for content changes that recompose the
        // whole level: the FIRST edit after a quiet stretch recomposes immediately (idle_start); a continuous stream
        // coalesces to the window cadence (window_open is the release valve; the diff persists after the stream ends
        // and keeps re-stamping last_content_seen_, so the FINAL state lands through window_open, within one window
        // of the last recompose). Origin re-snaps stay immediate, and the `stale_updates > 0` term latches a level
        // already marked stale so a budget-deferred compose cannot lose its place.
        const bool content_stale =
            contents_changed &&
            (first_content || idle_start || window_open || lvl.stale_updates > 0);
        if(origin_moved || content_stale)
        {
            ++lvl.stale_updates;
        }
        else
        {
            lvl.stale_updates = 0;
        }
    }
    cached_instances_revision_ = instances_revision;
    uint32_t composed = 0;
    for(uint32_t i = 0; i < level_count; ++i)
    {
        if(partial_plans[i] && is_partial_update_due(i, uint64_t(update_counter_)))
        {
            apply_level_plan(i, *partial_plans[i], target_origin[i], cached_target_fingerprint_[i],
                             cached_target_entries_[i], instances);
            ++composed;
        }
    }
    // Full recomposes: spend the budget on the levels that have waited longest, finest first on a tie.
    //
    // Age rather than index is what prevents starvation. The finest level re-snaps most often --
    // its voxel is the smallest -- so a strictly finest-first policy lets a moving camera keep
    // it permanently first in line and the coarse levels never rebuild at all, which is
    // indistinguishable from the cascade simply not working at distance.
    //
    // A level never composed since init is outside the budget, ahead of the others (UE composes every clipmap of a view
    // whose origins are not initialised, GlobalDistanceField.cpp:1154-1172): a new view's first frame traces the whole
    // cascade, and the radiance cache's full rebuild on that frame must not see the coarse levels empty.
    const auto is_uninitialised = [](const level& lvl) { return lvl.origin.x == std::numeric_limits<float>::max(); };
    const auto needs_full = [&](uint32_t i)
    {
        return levels_[i].is_valid() && levels_[i].stale_updates > 0 && !partial_plans[i];
    };
    uint32_t uninitialised = 0;
    for(uint32_t i = 0; i < level_count; ++i)
    {
        uninitialised += (needs_full(i) && is_uninitialised(levels_[i])) ? 1u : 0u;
    }
    const uint32_t budget = math::max(math::max(settings_.max_levels_per_update, 1u), uninitialised);
    uint32_t composed_full = 0;
    while(composed_full < budget)
    {
        uint32_t best = level_count;
        for(uint32_t i = 0; i < level_count; ++i)
        {
            if(!needs_full(i))
            {
                continue;
            }
            const bool is_first = is_uninitialised(levels_[i]);
            const bool best_is_first = best != level_count && is_uninitialised(levels_[best]);
            if(best == level_count || (is_first && !best_is_first) ||
               (is_first == best_is_first && levels_[i].stale_updates > levels_[best].stale_updates))
            {
                best = i;
            }
        }
        if(best == level_count)
        {
            break;
        }
        apply_level_plan(best, level_plan{}, target_origin[best], cached_target_fingerprint_[best],
                         cached_target_entries_[best], instances);
        ++composed_full;
        ++composed;
    }
    // Levels left stale keep their previous contents, which stay conservative: they were composed
    // for an origin that still overlaps this one, and for an instance set that has only changed
    // where something moved. Tracing remains correct, just briefly out of date.
    return composed;
}

void global_sdf_clipmap::apply_level_plan(uint32_t index,
                                          const level_plan& plan,
                                          const math::vec3& target_origin,
                                          uint64_t target_fingerprint,
                                          const std::vector<composed_entry>& entries,
                                          const std::vector<global_sdf_instance>& instances)
{
    auto& lvl = levels_[index];
    // A GPU mirror that has not consumed the previous recompose of this level cannot apply a partial one on top of
    // it: the level composes in full.
    const bool is_unconsumed = settings_.compose_on_gpu && (dirty_levels_ & (1u << index)) != 0u;
    const bool is_partial = plan.is_partial && !is_unconsumed;
    lvl.origin = target_origin;
    if(!settings_.compose_on_gpu)
    {
        if(is_partial)
        {
            std::vector<voxel_box> boxes = plan.partial_boxes;
            voxel_box overlap;
            std::array<voxel_box, 3> exposed;
            const uint32_t exposed_count =
                compute_scroll_boxes(plan.scroll_shift, settings_.resolution, overlap, exposed);
            scroll_level_voxels(index, plan.scroll_shift);
            boxes.insert(boxes.end(), exposed.begin(), exposed.begin() + exposed_count);
            if(!boxes.empty())
            {
                compose_level(index, instances, boxes);
            }
        }
        else
        {
            compose_level(index, instances);
        }
    }
    lvl.content_fingerprint = target_fingerprint;
    lvl.composed_entries = entries;
    lvl.has_composed_entries = true;
    lvl.is_partial = is_partial;
    lvl.scroll_shift = is_partial ? plan.scroll_shift : math::ivec3(0);
    lvl.partial_boxes = is_partial ? plan.partial_boxes : std::vector<voxel_box>{};
    ++lvl.compose_serial;
    lvl.stale_updates = 0;
    last_compose_counter_[index] = update_counter_;
    // The dirty bit is set either way -- it means "this level's contents are now stale on the
    // GPU", which is exactly as true when a dispatch is about to write them as when this
    // function just did. Keeping one meaning for the bit is what lets the two paths share all
    // the budget and staleness logic above.
    dirty_levels_ |= 1u << index;
}

void global_sdf_clipmap::scroll_level_voxels(uint32_t index, const math::ivec3& shift)
{
    auto& lvl = levels_[index];
    voxel_box overlap;
    std::array<voxel_box, 3> exposed;
    if(lvl.voxels.empty() || compute_scroll_boxes(shift, settings_.resolution, overlap, exposed) == 0)
    {
        return;
    }
    const size_t res = settings_.resolution;
    const std::vector<uint8_t> previous = lvl.voxels;
    for(int z = overlap.min.z; z < overlap.min.z + overlap.size.z; ++z)
    {
        for(int y = overlap.min.y; y < overlap.min.y + overlap.size.y; ++y)
        {
            for(int x = overlap.min.x; x < overlap.min.x + overlap.size.x; ++x)
            {
                const size_t destination = size_t(x) + size_t(y) * res + size_t(z) * res * res;
                const size_t source =
                    size_t(x + shift.x) + size_t(y + shift.y) * res + size_t(z + shift.z) * res * res;
                lvl.voxels[destination] = previous[source];
            }
        }
    }
}

auto global_sdf_clipmap::compute_scroll_boxes(const math::ivec3& shift,
                                              uint32_t resolution,
                                              voxel_box& out_overlap,
                                              std::array<voxel_box, 3>& out_exposed) -> uint32_t
{
    const int res = int(resolution);
    out_overlap = {};
    out_exposed = {};
    if(shift == math::ivec3(0))
    {
        return 0;
    }
    // The overlap in new-window coordinates: voxel v of the new window is voxel v + shift of
    // the old one, which exists while 0 <= v + shift < res.
    math::ivec3 overlap_min(0);
    math::ivec3 overlap_max(res);
    for(int axis = 0; axis < 3; ++axis)
    {
        overlap_min[axis] = math::max(0, -shift[axis]);
        overlap_max[axis] = math::min(res, res - shift[axis]);
        if(overlap_min[axis] >= overlap_max[axis])
        {
            return 0;
        }
    }
    out_overlap.min = overlap_min;
    out_overlap.size = overlap_max - overlap_min;
    // One slab per moved axis, covering the new window's full extent on the axes handled
    // AFTER it and only the overlap range on the axes handled before, so the three slabs and
    // the overlap are disjoint and tile the window.
    uint32_t count = 0;
    for(int axis = 0; axis < 3; ++axis)
    {
        if(shift[axis] == 0)
        {
            continue;
        }
        voxel_box slab;
        for(int other = 0; other < 3; ++other)
        {
            if(other < axis)
            {
                slab.min[other] = overlap_min[other];
                slab.size[other] = overlap_max[other] - overlap_min[other];
            }
            else
            {
                slab.min[other] = 0;
                slab.size[other] = res;
            }
        }
        // The exposed range on this axis is the complement of the overlap range.
        slab.min[axis] = shift[axis] > 0 ? overlap_max[axis] : 0;
        slab.size[axis] = shift[axis] > 0 ? res - overlap_max[axis] : overlap_min[axis];
        out_exposed[count] = slab;
        ++count;
    }
    return count;
}

void global_sdf_clipmap::compose_level(uint32_t index,
                                       const std::vector<global_sdf_instance>& instances,
                                       const std::vector<voxel_box>& boxes)
{
    APP_SCOPE_PERF("GI/Clipmap/Compose Level");
    auto& lvl = levels_[index];
    const uint32_t resolution = settings_.resolution;
    // The voxels to write: the boxes of a partial recompose, the whole level otherwise.
    std::vector<voxel_box> targets = boxes;
    if(targets.empty())
    {
        targets.push_back({math::ivec3(0), math::ivec3(int(resolution))});
    }
    const float voxel_size = lvl.voxel_size;
    const float encode_range = settings_.encode_range;
    const math::bbox level_bounds(lvl.origin,
                                  lvl.origin + math::vec3(float(resolution) * voxel_size));
    // Cull once, up front. Composition touches every voxel, so testing each instance's bounds
    // per voxel would repeat the same rejection millions of times.
    //
    // The bounds are expanded by the encode range: an instance just outside the level still
    // has to contribute, because voxels near the boundary are within encoding distance of it
    // and would otherwise read as empty space with a surface right next to them.
    const float reach = encode_range * voxel_size;
    std::vector<const global_sdf_instance*> relevant;
    relevant.reserve(instances.size());
    for(const auto& instance : instances)
    {
        // Same reasoning as the fingerprint above: the field was validated in full when it became
        // resident, so this only has to reject one carrying no data at all.
        if(instance.sdf == nullptr || !instance.sdf->is_sampleable())
        {
            continue;
        }
        math::bbox expanded = instance.world_bounds;
        expanded.inflate(reach);
        if(expanded.intersect(level_bounds))
        {
            relevant.push_back(&instance);
        }
    }
    const auto voxel_index = [resolution](int x, int y, int z) -> size_t
    {
        return size_t(x) + size_t(y) * resolution + size_t(z) * resolution * resolution;
    };
    if(relevant.empty())
    {
        const uint8_t saturated = encode_clipmap_distance(encode_range, encode_range);
        for(const auto& box : targets)
        {
            for(int z = box.min.z; z < box.min.z + box.size.z; ++z)
            {
                for(int y = box.min.y; y < box.min.y + box.size.y; ++y)
                {
                    for(int x = box.min.x; x < box.min.x + box.size.x; ++x)
                    {
                        lvl.voxels[voxel_index(x, y, z)] = saturated;
                    }
                }
            }
        }
        return;
    }
    // Bin the survivors into a grid over THIS LEVEL, so a voxel tests the handful of instances
    // that can reach it rather than every instance the level as a whole overlaps.
    //
    // The per-level cull above is not enough on its own: the coarsest level spans the whole scene,
    // so nearly every instance survives it and the inner loop below becomes voxels x instances,
    // which on a dense scene makes composition a visible hitch whenever the camera crosses a
    // coarse voxel.
    //
    // The bounds are inflated by the same reach: a voxel must find every instance within encoding
    // distance, not only the ones containing it. Cells are several voxels across, which keeps the
    // grid small next to the volume it accelerates.
    std::vector<math::bbox> reach_bounds;
    reach_bounds.reserve(relevant.size());
    for(const auto* instance : relevant)
    {
        math::bbox expanded = instance->world_bounds;
        expanded.inflate(reach);
        reach_bounds.push_back(expanded);
    }
    sdf_instance_grid cull;
    sdf_instance_grid::settings cull_settings;
    cull_settings.resolution = math::max(resolution / voxels_per_cull_cell, 1u);
    cull.init(cull_settings);
    cull.build(reach_bounds, level_bounds);
    const auto& cull_offsets = cull.get_cell_offsets();
    const auto& cull_instances = cull.get_cell_instances();
    const bool cull_ready = settings_.cull_composition && cull.is_valid();

    for(const auto& box : targets)
    {
        poolstl::for_each_par_if(true,
                      poolstl::iota_iter<int>(box.min.z),
                      poolstl::iota_iter<int>(box.min.z + box.size.z),
                      [&](int z)
                      {
                          // On the POOL thread's own lane. The enclosing scope runs on the main
                          // thread, which blocks on the futures and therefore reports as idle --
                          // a reading that makes a long composition look free. The real work only
                          // becomes visible with a marker inside the parallel body, and it is also
                          // the only way to tell genuine compute from time spent queued behind
                          // whatever else is sharing this pool.
                          APP_SCOPE_PERF_THREAD("GI/Clipmap/Compose Slice", "Pool Thread");
                          for(int y = box.min.y; y < box.min.y + box.size.y; ++y)
                          {
                              for(int x = box.min.x; x < box.min.x + box.size.x; ++x)
                              {
                                  const math::vec3 world_position =
                                      lvl.origin + (math::vec3(float(x), float(y), float(z)) + math::vec3(0.5f)) *
                                                       voxel_size;
                                  // Seeded at the encode range rather than at infinity. A voxel
                                  // stores distances in [-reach, reach] and saturates beyond, so an
                                  // instance further than that cannot change the byte written here
                                  // -- and starting at infinity forces the FIRST candidate to be
                                  // sampled in full before the reject below can do anything, which
                                  // on dense geometry is the majority of the remaining cost.
                                  //
                                  // Output-identical by construction: with nothing sampled this
                                  // encodes to exactly the saturated value infinity would have.
                                  float nearest = reach;
                                  // Candidates from this voxel's cell. Falling back to the full list
                                  // keeps composition correct if the grid could not be built, which
                                  // costs time rather than accuracy.
                                  const uint32_t cell = cull_ready ? cull.find_cell(world_position) : 0u;
                                  const size_t candidate_begin = cull_ready ? cull_offsets[cell] : 0u;
                                  const size_t candidate_end =
                                      cull_ready ? cull_offsets[cell + 1u] : relevant.size();
                                  for(size_t candidate = candidate_begin; candidate < candidate_end; ++candidate)
                                  {
                                      const auto* instance =
                                          cull_ready ? relevant[cull_instances[candidate]] : relevant[candidate];
                                      // Cheap reject before the field lookup: outside the instance's
                                      // bounds the distance to those bounds is already a valid
                                      // conservative answer, and usually a worse one than what
                                      // another instance contributes.
                                      const math::vec3 clamped = math::clamp(world_position,
                                                                             instance->world_bounds.min,
                                                                             instance->world_bounds.max);
                                      const float to_bounds = math::length(world_position - clamped);
                                      // The reject is only valid while `nearest` is a distance to a
                                      // surface the voxel is OUTSIDE of. Once it goes negative the
                                      // voxel is inside some instance, and `to_bounds` -- which is
                                      // zero inside any bounds and never negative -- compares greater
                                      // than every negative value, so this would skip every remaining
                                      // candidate. That makes the interior "first negative wins",
                                      // which depends on the order candidates happen to be visited in
                                      // and therefore on how they were binned: two correct traversals
                                      // of the same scene produce different voxels.
                                      //
                                      // test_clipmap_compose_shader_transcription_matches_cpu guards
                                      // this by comparing against a differently ordered gather. Strict,
                                      // for the same reason: at nearest == 0 (a voxel centre on a face)
                                      // an instance containing the voxel reads to_bounds 0 too.
                                      if(nearest >= 0.0f && to_bounds > nearest)
                                      {
                                          continue;
                                      }
                                      const math::vec4 local =
                                          instance->world_to_local * math::vec4(world_position, 1.0f);
                                      // Two-sided fields compose as zero-thickness sheets: the
                                      // global field's march thickens surfaces by its own expand.
                                      nearest = math::min(nearest,
                                                          sample_instance_distance(*instance->sdf,
                                                                                   math::vec3(local),
                                                                                   instance->axis_scale,
                                                                                   instance->local_to_world_scale,
                                                                                   true));
                                  }
                                  lvl.voxels[voxel_index(x, y, z)] =
                                      encode_clipmap_distance(nearest / voxel_size, encode_range);
                              }
                          }
                      });
    }
}

auto global_sdf_clipmap::sample_level(uint32_t index, const math::vec3& world_position) const -> float
{
    if(index >= level_count)
    {
        return outside_distance;
    }
    const auto& lvl = levels_[index];
    if(!lvl.has_voxels())
    {
        return outside_distance;
    }
    const uint32_t resolution = settings_.resolution;
    const math::vec3 grid = (world_position - lvl.origin) / lvl.voxel_size;
    // Trilinear needs a full voxel of margin, so a position in the outermost half voxel is not
    // addressable by this level at all.
    if(math::any(math::lessThan(grid, math::vec3(0.5f))) ||
       math::any(math::greaterThan(grid, math::vec3(float(resolution) - 0.5f))))
    {
        return outside_distance;
    }
    const math::vec3 sample_position = grid - math::vec3(0.5f);
    const math::ivec3 base = math::ivec3(math::floor(sample_position));
    const math::vec3 frac = sample_position - math::vec3(base);
    const auto fetch = [&](int x, int y, int z) -> float
    {
        const int cx = math::clamp(x, 0, int(resolution) - 1);
        const int cy = math::clamp(y, 0, int(resolution) - 1);
        const int cz = math::clamp(z, 0, int(resolution) - 1);
        const size_t offset = size_t(cx) + size_t(cy) * resolution + size_t(cz) * resolution * resolution;
        return decode_clipmap_distance(lvl.voxels[offset], settings_.encode_range);
    };
    const float c00 = math::mix(fetch(base.x, base.y, base.z), fetch(base.x + 1, base.y, base.z), frac.x);
    const float c10 =
        math::mix(fetch(base.x, base.y + 1, base.z), fetch(base.x + 1, base.y + 1, base.z), frac.x);
    const float c01 =
        math::mix(fetch(base.x, base.y, base.z + 1), fetch(base.x + 1, base.y, base.z + 1), frac.x);
    const float c11 =
        math::mix(fetch(base.x, base.y + 1, base.z + 1), fetch(base.x + 1, base.y + 1, base.z + 1), frac.x);
    const float distance_voxels =
        math::mix(math::mix(c00, c10, frac.y), math::mix(c01, c11, frac.y), frac.z);
    return distance_voxels * lvl.voxel_size;
}

auto global_sdf_clipmap::find_level(const math::vec3& world_position, float& out_blend) const -> uint32_t
{
    out_blend = 0.0f;
    // Finest level first: level 0 has the smallest voxels, so it gives the most accurate answer
    // wherever it reaches.
    for(uint32_t i = 0; i < level_count; ++i)
    {
        const auto& lvl = levels_[i];
        if(!lvl.has_voxels())
        {
            continue;
        }
        const float resolution = float(settings_.resolution);
        const math::vec3 grid = (world_position - lvl.origin) / lvl.voxel_size;
        if(math::any(math::lessThan(grid, math::vec3(0.5f))) ||
           math::any(math::greaterThan(grid, math::vec3(resolution - 0.5f))))
        {
            continue;
        }
        // The blend is driven by the distance to the nearest FACE of this level's addressable
        // box, in its own voxels, so the fade follows the box rather than a radius -- the box is
        // what the coverage test above actually uses.
        const math::vec3 to_low = grid - math::vec3(0.5f);
        const math::vec3 to_high = math::vec3(resolution - 0.5f) - grid;
        const math::vec3 nearest_face = math::min(to_low, to_high);
        const float edge_distance = math::min(nearest_face.x, math::min(nearest_face.y, nearest_face.z));
        const bool has_next = (i + 1u) < level_count && levels_[i + 1u].has_voxels();
        // The outermost level never fades. Beyond it there is only the give-up value, and mixing
        // toward that would report a distance far larger than the truth -- the one direction a
        // conservative field must never err in, since a trace would step straight through
        // whatever is out there.
        if(has_next && settings_.blend_voxels > 0.0f)
        {
            out_blend = 1.0f - math::clamp(edge_distance / settings_.blend_voxels, 0.0f, 1.0f);
        }
        return i;
    }
    return level_count;
}

auto global_sdf_clipmap::sample_ex(const math::vec3& world_position, float& out_voxel_size) const -> float
{
    out_voxel_size = math::max(levels_[0].voxel_size, 1e-6f);
    float blend = 0.0f;
    const uint32_t index = find_level(world_position, blend);
    if(index >= level_count)
    {
        return outside_distance;
    }
    out_voxel_size = levels_[index].voxel_size;
    const float fine = sample_level(index, world_position);
    if(blend <= 0.0f)
    {
        return fine;
    }
    const float coarse = sample_level(index + 1u, world_position);
    if(coarse >= outside_distance)
    {
        return fine;
    }
    // The reported size follows the blend for the same reason it is reported at all: inside the
    // band the value is a mixture of two levels, so anything scaled to "a voxel" has to be scaled
    // to the same mixture or it jumps at the boundary.
    out_voxel_size = math::mix(out_voxel_size, levels_[index + 1u].voxel_size, blend);
    return math::mix(fine, coarse, blend);
}

auto global_sdf_clipmap::sample(const math::vec3& world_position) const -> float
{
    float blend = 0.0f;
    const uint32_t index = find_level(world_position, blend);
    if(index >= level_count)
    {
        return outside_distance;
    }
    const float fine = sample_level(index, world_position);
    if(blend <= 0.0f)
    {
        return fine;
    }
    const float coarse = sample_level(index + 1u, world_position);
    if(coarse >= outside_distance)
    {
        // The next level should always cover here -- it is larger and shares a centre -- so this
        // only fires if snapping has pushed it off. Keeping the fine value is both conservative
        // and the better answer; blending toward the give-up value would not be.
        return fine;
    }
    // Convex combination of two conservative under-estimates, so the result under-estimates too.
    return math::mix(fine, coarse, blend);
}


} // namespace unravel
