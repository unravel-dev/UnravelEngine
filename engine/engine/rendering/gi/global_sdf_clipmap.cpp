// Like mesh_sdf_baker, this translation unit depends only on math and the standard library so
// the composer can be validated without the ECS, the asset layer, or a GPU.
#include "global_sdf_clipmap.h"

#include <engine/profiler/profiler.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/mesh_sdf_baker.h>
#include <engine/rendering/gi/sdf_instance_grid.h>

#include <concurrency/parallel.h>

#include <algorithm>
#include <atomic>
#include <array>
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
    for(uint32_t i = 0; i < level_count; ++i)
    {
        auto& lvl = levels_[i];
        lvl.voxel_size = get_level_extent(i) / float(settings_.resolution);
        lvl.voxels.assign(size_t(settings_.resolution) * settings_.resolution * settings_.resolution, 0u);
        // Start saturated positive: an empty world reads as "nothing anywhere near", which is
        // the conservative answer and keeps traces marching instead of hitting at the origin.
        std::fill(lvl.voxels.begin(), lvl.voxels.end(), uint8_t(255));
        // Deliberately not a valid snapped origin, so the first update always composes.
        lvl.origin = math::vec3(std::numeric_limits<float>::max());
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

auto global_sdf_clipmap::compute_level_fingerprint(const math::bbox& bounds,
                                                   float reach,
                                                   const std::vector<global_sdf_instance>& instances) const
    -> uint64_t
{
    uint64_t total = 0;
    uint64_t count = 0;
    for(const auto& instance : instances)
    {
        // is_sampleable, not is_valid: the thorough check walks every indirection entry, and this
        // loop runs for EVERY level on EVERY frame, whether or not anything ends up composing.
        // The field was already validated in full when it became resident
        // (surface_cache_system::acquire_field), so re-proving it per frame buys nothing and
        // costs the brick count times four times the instance count, every frame.
        if(instance.sdf == nullptr || !instance.sdf->is_sampleable())
        {
            continue;
        }
        math::bbox expanded = instance.world_bounds;
        expanded.inflate(reach);
        if(!expanded.intersect(bounds))
        {
            continue;
        }
        ++count;
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
        // Material too: albedo and emissive are BAKED into the attribute voxels at composition,
        // so a change nothing rehashes would keep bouncing the old colour forever. This is also
        // what publishes a lazily resolved texture-mean albedo (surface_cache_system) and any
        // material edit into the volume: the fingerprint moves, the level recomposes.
        const auto hash_vec3 = [&entry](const math::vec3& v)
        {
            const auto* vec_words = reinterpret_cast<const uint32_t*>(&v);
            for(size_t i = 0; i < 3; ++i)
            {
                entry = (entry ^ uint64_t(vec_words[i])) * 0x100000001b3ull;
            }
        };
        hash_vec3(instance.albedo);
        hash_vec3(instance.emissive);
        // The mean slot and its captured flag stand in for the mean VALUE, which lives only on
        // the GPU: the flag flipping once per capture is what publishes the mean into the
        // attribute voxels via a single recompose.
        entry = (entry ^ (uint64_t(instance.mean_slot) | (instance.mean_captured ? 0x100000000ull : 0ull))) *
                0x100000001b3ull;
        // Summed rather than chained, so the result does not depend on iteration order -- the
        // scene traversal that produced this list has no guaranteed order and a reshuffle is
        // not a change.
        total += entry;
    }
    // Mixed with the count so an empty level cannot collide with a populated one whose entries
    // happen to sum to zero.
    return total ^ (count * 0x9e3779b97f4a7c15ull);
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
    // Only these three change what a voxel MEANS. Everything else is read afresh by the next
    // composition, so assigning it is enough and costs nothing.
    const bool layout_changed = new_settings.resolution != settings_.resolution ||
                                new_settings.base_extent != settings_.base_extent ||
                                new_settings.level_scale != settings_.level_scale;
    if(layout_changed)
    {
        init(new_settings);
        return true;
    }
    settings_ = new_settings;
    return false;
}

auto global_sdf_clipmap::update(const std::vector<global_sdf_instance>& instances,
                                const math::vec3& camera_position) -> uint32_t
{
    APP_SCOPE_PERF("GI/Clipmap/Update");
    // Pass one: decide what each level SHOULD be, and how stale it is. Nothing is composed here,
    // so the budget below chooses between levels knowing all of them.
    std::array<math::vec3, level_count> target_origin{};
    std::array<uint64_t, level_count> target_fingerprint{};
    for(uint32_t i = 0; i < level_count; ++i)
    {
        auto& lvl = levels_[i];
        if(!lvl.is_valid())
        {
            continue;
        }
        // Snap the centre to this level's ATTRIBUTE-voxel grid (attr_downsample fine voxels),
        // then place the origin a half extent away. Snapping is what makes the result a function
        // of which voxel the camera is in rather than of its exact position - the basis of world
        // stability - and snapping at attribute granularity additionally makes the origin an
        // exact multiple of the attribute voxel, which is what lets the attribute and light
        // volumes address a world-anchored toroidal grid whose cells keep their identity across
        // re-snaps. (The resolution is even, so a half extent is a whole number of attribute
        // voxels and the origin inherits the alignment.)
        const float extent = get_level_extent(i);
        const float snap_size = lvl.voxel_size * float(attr_downsample);
        const math::vec3 snapped_center = math::floor(camera_position / snap_size) * snap_size;
        target_origin[i] = snapped_center - math::vec3(extent * 0.5f);
        target_fingerprint[i] = compute_level_fingerprint(compute_level_bounds(i, target_origin[i]),
                                                          compute_level_reach(i),
                                                          instances);
        const bool origin_moved = target_origin[i] != lvl.origin;
        const bool contents_changed = target_fingerprint[i] != lvl.content_fingerprint;
        if(origin_moved || contents_changed)
        {
            ++lvl.stale_updates;
        }
        else
        {
            lvl.stale_updates = 0;
        }
        // CONTENT epoch, for reactivity consumers (the world-probe fast window): fires once
        // per actual scene change - an instance moved, appeared, changed material - and is
        // deliberately suppressed while the origin moves, because a scrolling window changes
        // which instances the fingerprint sees every re-snap and camera motion would pin the
        // fast path permanently.
        if(!origin_moved && target_fingerprint[i] != seen_fingerprints_[i])
        {
            ++content_epoch_;
        }
        seen_fingerprints_[i] = target_fingerprint[i];
    }
    // Pass two: spend the budget on the levels that have waited longest, finest first on a tie.
    //
    // Age rather than index is what prevents starvation. The finest level re-snaps most often --
    // its voxel is the smallest -- so a strictly finest-first policy lets a moving camera keep
    // it permanently first in line and the coarse levels never rebuild at all, which is
    // indistinguishable from the cascade simply not working at distance.
    const uint32_t budget = math::max(settings_.max_levels_per_update, 1u);
    uint32_t composed = 0;
    while(composed < budget)
    {
        uint32_t best = level_count;
        for(uint32_t i = 0; i < level_count; ++i)
        {
            if(!levels_[i].is_valid() || levels_[i].stale_updates == 0)
            {
                continue;
            }
            if(best == level_count || levels_[i].stale_updates > levels_[best].stale_updates)
            {
                best = i;
            }
        }
        if(best == level_count)
        {
            break;
        }
        auto& lvl = levels_[best];
        lvl.origin = target_origin[best];
        lvl.content_fingerprint = target_fingerprint[best];
        lvl.stale_updates = 0;
        // The dirty bit is set either way -- it means "this level's contents are now stale on the
        // GPU", which is exactly as true when a dispatch is about to write them as when this
        // function just did. Keeping one meaning for the bit is what lets the two paths share all
        // the budget and staleness logic above.
        if(!settings_.compose_on_gpu)
        {
            compose_level(best, instances);
        }
        dirty_levels_ |= 1u << best;
        ++composed;
    }
    // Levels left stale keep their previous contents, which stay conservative: they were composed
    // for an origin that still overlaps this one, and for an instance set that has only changed
    // where something moved. Tracing remains correct, just briefly out of date.
    return composed;
}

void global_sdf_clipmap::compose_level(uint32_t index, const std::vector<global_sdf_instance>& instances)
{
    APP_SCOPE_PERF("GI/Clipmap/Compose Level");
    auto& lvl = levels_[index];
    const uint32_t resolution = settings_.resolution;
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
    if(relevant.empty())
    {
        std::fill(lvl.voxels.begin(), lvl.voxels.end(), encode_clipmap_distance(encode_range, encode_range));
        return;
    }
    // Bin the survivors into a grid over THIS LEVEL, so a voxel tests the handful of instances
    // that can reach it rather than every instance the level as a whole overlaps.
    //
    // The per-level cull above is not enough on its own: the coarsest level spans the whole scene,
    // so nearly every instance survives it and the inner loop below becomes voxels x instances --
    // measured at 64^3 x ~1600 for a city block, about a second of aggregate CPU for one level,
    // which is what made composition a visible hitch whenever the camera crossed a coarse voxel.
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
    last_compose_stats_ = {};
    last_compose_stats_.level = index;
    last_compose_stats_.relevant_instances = uint32_t(relevant.size());
    last_compose_stats_.cull_cells = uint32_t(cull.get_cell_count());
    last_compose_stats_.cull_references = uint32_t(cull.get_cell_instances().size());
    for(size_t cell = 0; cull_ready && cell + 1u < cull_offsets.size(); ++cell)
    {
        last_compose_stats_.max_candidates_in_cell =
            math::max(last_compose_stats_.max_candidates_in_cell, cull_offsets[cell + 1u] - cull_offsets[cell]);
    }
    // Summed once per slice rather than per voxel: 64 atomic adds are free, one per voxel would
    // be its own measurement problem.
    std::atomic<uint64_t> candidate_tests{0};
    std::atomic<uint64_t> field_samples{0};

    poolstl::for_each_par_if(true,
                  poolstl::iota_iter<uint32_t>(0),
                  poolstl::iota_iter<uint32_t>(resolution),
                  [&](uint32_t z)
                  {
                      // On the POOL thread's own lane. The enclosing scope runs on the main
                      // thread, which blocks on the futures and therefore reports ~98% idle --
                      // a reading that makes a 45 ms composition look free. The real work only
                      // becomes visible with a marker inside the parallel body, and it is also
                      // the only way to tell genuine compute from time spent queued behind
                      // whatever else is sharing this pool.
                      APP_SCOPE_PERF_THREAD("GI/Clipmap/Compose Slice", "Pool Thread");
                      uint64_t slice_tests = 0;
                      uint64_t slice_samples = 0;
                      for(uint32_t y = 0; y < resolution; ++y)
                      {
                          for(uint32_t x = 0; x < resolution; ++x)
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
                                  ++slice_tests;
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
                                  // Found by test_clipmap_compose_shader_transcription_matches_cpu,
                                  // which compared this against a differently ordered gather and
                                  // disagreed in BOTH directions -- the signature of order
                                  // dependence rather than of a missing instance.
                                  if(nearest >= 0.0f && to_bounds >= nearest)
                                  {
                                      continue;
                                  }
                                  ++slice_samples;
                                  const math::vec4 local =
                                      instance->world_to_local * math::vec4(world_position, 1.0f);
                                  const float local_distance = sample_mesh_sdf(*instance->sdf, math::vec3(local));
                                  nearest = math::min(nearest, local_distance * instance->local_to_world_scale);
                              }
                              const uint32_t offset = x + y * resolution + z * resolution * resolution;
                              lvl.voxels[offset] = encode_clipmap_distance(nearest / voxel_size, encode_range);
                          }
                      }
                      candidate_tests += slice_tests;
                      field_samples += slice_samples;
                  });
    last_compose_stats_.candidate_tests = candidate_tests.load();
    last_compose_stats_.field_samples = field_samples.load();
    compose_level_attributes(index, instances);
}

void global_sdf_clipmap::compose_level_attributes(uint32_t index,
                                                  const std::vector<global_sdf_instance>& instances)
{
    APP_SCOPE_PERF("GI/Clipmap/Compose Attributes");
    auto& lvl = levels_[index];
    const uint32_t attr_resolution = get_attr_resolution();
    const float attr_voxel_size = lvl.voxel_size * float(attr_downsample);
    const size_t attr_count = size_t(attr_resolution) * attr_resolution * attr_resolution;
    lvl.attr_albedo.assign(attr_count, 0u);
    lvl.attr_emissive.assign(attr_count, math::vec3(0.0f));
    lvl.attr_surface_list.clear();
    // Candidates only matter within the surface band plus one attribute voxel of margin: the
    // field's zero crossing and the voxel centre can sit up to a voxel apart, and an instance
    // further out than that cannot be the nearest surface to a voxel the field calls surface.
    const float band = float(gi::GI_SURFACE_VOXEL_BAND) * attr_voxel_size;
    const float attr_reach = band + attr_voxel_size;
    // TOROIDAL addressing (mirrors cs_gi_clipmap_attributes.sc): storage index is the world
    // cell wrapped by the resolution, so a cell keeps its slot - and its accumulated light
    // radiance on the GPU - across level re-snaps. The origin is attr-voxel aligned by the
    // snap, so the window base is exact integer cells.
    const int res = int(attr_resolution);
    const auto wrap = [res](int v) -> int { return ((v % res) + res) % res; };
    const math::ivec3 window_base(int(std::floor(lvl.origin.x / attr_voxel_size + 0.5f)),
                                  int(std::floor(lvl.origin.y / attr_voxel_size + 0.5f)),
                                  int(std::floor(lvl.origin.z / attr_voxel_size + 0.5f)));
    const math::ivec3 base_slot(wrap(window_base.x), wrap(window_base.y), wrap(window_base.z));
    for(uint32_t z = 0; z < attr_resolution; ++z)
    {
        for(uint32_t y = 0; y < attr_resolution; ++y)
        {
            for(uint32_t x = 0; x < attr_resolution; ++x)
            {
                const math::ivec3 slot_offset(wrap(int(x) - base_slot.x),
                                              wrap(int(y) - base_slot.y),
                                              wrap(int(z) - base_slot.z));
                const math::ivec3 cell = window_base + slot_offset;
                const math::vec3 center =
                    (math::vec3(cell) + math::vec3(0.5f)) * attr_voxel_size;
                // The COMPOSED field's band judges surfaceness, alone. Deep interiors are
                // excluded by it already - they read the bake's conservative empty-inside
                // distances, well outside the band (measured on the thick-box fixture with no
                // other gate). A gradient gate briefly existed here to trim the saturated ring
                // just inside surface bricks, and was REMOVED for cause: a thin wall's field is
                // a VALLEY - it rises on both sides, the central difference along the wall
                // normal cancels to ~0 - so the gate's plateau signature matched every wall
                // thinner than two attribute voxels, which in built content is most of them at
                // every cascade. That presented as unattributed (yellow) surfaces everywhere
                // and starved the whole bounce loop through the "honest darkness" reads. The
                // ring it protected against costs a few over-lit sub-surface voxels; the walls
                // it rejected cost the system its energy.
                const float field_distance = sample_level(index, center);
                if(field_distance >= outside_distance || std::fabs(field_distance) > band)
                {
                    continue;
                }
                // TOP-2 attribution, blended by proximity - not winner-take-all. A coarse voxel
                // genuinely CONTAINS a mixture of the surfaces inside it, and linear albedo
                // mixing is the correct prefilter for diffuse; the argmin this replaced painted
                // whole coarse voxels one instance's colour (measured: red halos around distant
                // awnings once coarse faces became measurable). Both slots are tracked
                // min-style with index tie-breaks, so the update is IDEMPOTENT under repeated
                // candidate visits - the property that keeps this loop and the shader's
                // duplicate-visiting grid walk in exact agreement, which is what argmin was
                // originally chosen for.
                float m1 = attr_reach;
                float m2 = attr_reach;
                size_t i1 = instances.size();
                size_t i2 = instances.size();
                for(size_t candidate = 0; candidate < instances.size(); ++candidate)
                {
                    const auto& instance = instances[candidate];
                    if(instance.sdf == nullptr || !instance.sdf->is_sampleable())
                    {
                        continue;
                    }
                    if(candidate == i1 || candidate == i2)
                    {
                        continue;
                    }
                    const math::vec3 clamped =
                        math::clamp(center, instance.world_bounds.min, instance.world_bounds.max);
                    if(math::length(center - clamped) >= m2)
                    {
                        continue;
                    }
                    const math::vec4 local = instance.world_to_local * math::vec4(center, 1.0f);
                    // Compete at TRUE surface distance (mirrors cs_gi_clipmap_attributes.sc): a
                    // two-sided shell reads |distance to sheet| - half_thickness, so raw |d| put
                    // its zero isosurface - a phantom skin half a metre off the cloth at
                    // production bake scales - ahead of honest signed fields wherever the skin
                    // crossed them, painting curtain and rope albedo onto Sponza's stone. Adding
                    // back the applied half-thickness (zero for signed fields) restores the
                    // unsigned sheet distance.
                    const float shell_bias =
                        instance.sdf->is_two_sided ? instance.sdf->two_sided_thickness : 0.0f;
                    const float magnitude =
                        std::fabs((sample_mesh_sdf(*instance.sdf, math::vec3(local)) + shell_bias) *
                                  instance.local_to_world_scale);
                    if(magnitude < m1 || (magnitude == m1 && (i1 >= instances.size() || candidate < i1)))
                    {
                        m2 = m1;
                        i2 = i1;
                        m1 = magnitude;
                        i1 = candidate;
                    }
                    else if(magnitude < m2 ||
                            (magnitude == m2 && (i2 >= instances.size() || candidate < i2)))
                    {
                        m2 = magnitude;
                        i2 = candidate;
                    }
                }
                if(i1 >= instances.size())
                {
                    // The field says surface but no instance is attributable within reach. Leave
                    // the voxel dark: energy loss, never a fabricated material - the same
                    // asymmetry Lumen chooses for missing card coverage.
                    continue;
                }
                // Single-source voxels copy EXACTLY: (a * w) / w is not an identity in
                // float, and a one-ULP wobble flips quantisation on boundary values (0.8
                // lands precisely on the 204.5 rounding edge - measured as 48 wrong-material
                // voxels on a one-box fixture).
                const auto& first = instances[i1];
                math::vec3 blended_albedo = first.albedo;
                math::vec3 blended_emissive = first.emissive;
                if(i2 < instances.size())
                {
                    // COVERAGE-scaled proximity (mirrors cs_gi_clipmap_attributes.sc): a thin
                    // shell's volume fraction inside the cell is bounded by its thickness over
                    // the cell size - a rope equidistant with the floor is a sliver of the
                    // cell, not half of it. Solids keep weight 1.
                    const auto shell_coverage = [&](const global_sdf_instance& inst) -> float
                    {
                        if(!inst.sdf->is_two_sided)
                        {
                            return 1.0f;
                        }
                        return math::clamp(2.0f * inst.sdf->two_sided_thickness *
                                               inst.local_to_world_scale / attr_voxel_size,
                                           0.0f,
                                           1.0f);
                    };
                    const float w1 = (attr_reach - m1) * shell_coverage(first);
                    const float w2 = (attr_reach - m2) * shell_coverage(instances[i2]);
                    const float w_sum = math::max(w1 + w2, 1e-6f);
                    blended_albedo = (first.albedo * w1 + instances[i2].albedo * w2) / w_sum;
                    blended_emissive = (first.emissive * w1 + instances[i2].emissive * w2) / w_sum;
                }
                const auto quantize = [](float v) -> uint32_t
                { return uint32_t(math::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
                const size_t offset =
                    size_t(x) + size_t(y) * attr_resolution + size_t(z) * attr_resolution * attr_resolution;
                lvl.attr_albedo[offset] = quantize(blended_albedo.x) | (quantize(blended_albedo.y) << 8u) |
                                          (quantize(blended_albedo.z) << 16u) | (255u << 24u);
                lvl.attr_emissive[offset] = blended_emissive;
                lvl.attr_surface_list.push_back(pack_surface_voxel(x, y, z, index));
            }
        }
    }
}

auto global_sdf_clipmap::sample_level(uint32_t index, const math::vec3& world_position) const -> float
{
    if(index >= level_count)
    {
        return outside_distance;
    }
    const auto& lvl = levels_[index];
    if(!lvl.is_valid())
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
        if(!lvl.is_valid())
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
        const bool has_next = (i + 1u) < level_count && levels_[i + 1u].is_valid();
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
