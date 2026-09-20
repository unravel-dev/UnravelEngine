#pragma once

#include "batch_key.h"
#include "batch_instance.h"

#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace unravel
{

struct batch_stats
{
    uint32_t total_batches = 0;
    uint32_t total_instances = 0;
    uint32_t draw_calls_saved = 0;
    float collection_time_ms = 0.0f;
    float preparation_time_ms = 0.0f;
    float submission_time_ms = 0.0f;
    size_t instance_buffer_memory_used = 0;
    float average_batch_size = 0.0f;
    float batching_efficiency = 0.0f;
    uint32_t split_batches = 0;

    void reset();
    void calculate_derived_stats();
    auto to_string() const -> std::string;
};

struct submit_context
{
    uint16_t view_id = 0;
    math::vec3 camera_position = math::vec3(0.0f);
    bool enable_distance_sorting = false;
    uint32_t max_instances_per_batch = 1024;
    bool enable_profiling = true;
};

template<typename Key>
struct batch_group_t
{
    Key key;
    batch_instance_collection instances;
    float camera_distance = 0.0f;
    bool is_split_batch = false;
    /// Mixed hash of @ref key, cached by the owning collector: probing compares it before the
    /// key itself, and growing the index never hashes a key again.
    size_t key_hash = 0;

    batch_group_t() = default;
    explicit batch_group_t(const Key& key);

    void add_instance(const batch_instance& instance);
    /// Back to an unused slot. The key is dropped - it owns mesh / material / texture
    /// references that must not outlive the frame - while the instance storage keeps its
    /// capacity for the next frame.
    void reset();
    void calculate_camera_distance(const math::vec3& camera_pos);
    auto is_valid() const -> bool;
    auto get_gpu_memory_size() const -> size_t;
};

using batch_group = batch_group_t<batch_key>;

namespace batch_collector_detail
{
inline auto static_mesh_batching_enabled() -> bool&
{
    static bool enabled = true;
    return enabled;
}

/// Final avalanche over a key hash (the splitmix64 finalizer). The group index masks the LOW
/// bits of the hash, which hash_combine over aligned pointers and small indices spreads poorly.
inline auto mix_hash(size_t hash) -> size_t
{
    constexpr uint64_t FIRST_MULTIPLIER = 0xbf58476d1ce4e5b9ULL;
    constexpr uint64_t SECOND_MULTIPLIER = 0x94d049bb133111ebULL;
    uint64_t mixed = static_cast<uint64_t>(hash);
    mixed = (mixed ^ (mixed >> 30)) * FIRST_MULTIPLIER;
    mixed = (mixed ^ (mixed >> 27)) * SECOND_MULTIPLIER;
    mixed ^= mixed >> 31;
    return static_cast<size_t>(mixed);
}
} // namespace batch_collector_detail

/**
 * @brief Groups renderables by batch key for instanced submission.
 *
 * Frame protocol: clear() -> collect_renderable() ... -> prepare_batches() -> any number of
 * passes over get_prepared_batches() -> clear(). Collecting a NEW key after prepare_batches()
 * drops the prepared list: its pointers address the group slots, which may move when a slot
 * is added.
 *
 * Steady state allocates nothing. Groups live in slots that are recycled together with their
 * instance storage, and the key lookup is an index table a clear() merely refills - the
 * node-based map this replaced freed and rebuilt every node and every instance vector each
 * frame, once per shadow face per light on the shadow path. Storage grows to the largest frame
 * seen and is released with the collector; a cleared collector holds no asset references.
 */
template<typename Key>
class batch_collector_t
{
public:
    using batch_list_t = std::vector<batch_group_t<Key>*>;

    batch_collector_t();

    static auto is_static_mesh_batching_enabled() -> bool
    {
        return batch_collector_detail::static_mesh_batching_enabled();
    }

    static void set_static_mesh_batching_enabled(bool enabled)
    {
        batch_collector_detail::static_mesh_batching_enabled() = enabled;
    }

    ~batch_collector_t() = default;

    batch_collector_t(const batch_collector_t&) = delete;
    auto operator=(const batch_collector_t&) -> batch_collector_t& = delete;
    batch_collector_t(batch_collector_t&&) noexcept = default;
    auto operator=(batch_collector_t&&) noexcept -> batch_collector_t& = default;

    void collect_renderable(const Key& key, const batch_instance& instance);
    void collect_renderable(const Key& key, const math::mat4& world_transform);
    void prepare_batches(const submit_context& context);
    auto get_prepared_batches() const -> const batch_list_t&;
    void clear();
    auto get_stats() const -> const batch_stats&;
    auto get_batch_count() const -> size_t;
    auto get_instance_count() const -> size_t;
    auto has_batches() const -> bool;
    void set_max_instances_per_batch(uint32_t max_instances);
    void set_profiling_enabled(bool enabled);

private:
    static constexpr uint32_t EMPTY_BUCKET = std::numeric_limits<uint32_t>::max();
    /// Power of two: the index masks hashes instead of dividing them.
    static constexpr size_t MIN_BUCKET_COUNT = 64;
    /// The index keeps at least this many buckets per group (load factor 0.5), which keeps
    /// the linear probe sequences short.
    static constexpr size_t BUCKETS_PER_GROUP = 2;

    /// Group slots. [0, group_count_) are this frame's batches in first-collected order, the
    /// slots behind them are unused ones kept for their instance storage. Collection order is
    /// stable from frame to frame, so a slot mostly meets the same key again and its capacity
    /// already fits.
    std::vector<batch_group_t<Key>> groups_;
    size_t group_count_ = 0;
    /// Open-addressing (linear probing) index over the active slots: a group index or
    /// EMPTY_BUCKET. It holds no keys, so emptying it is a fill.
    std::vector<uint32_t> buckets_;
    batch_list_t prepared_batches_;
    batch_stats stats_;
    uint32_t max_instances_per_batch_ = 1024;
    /// Opt-in: the collection timer reads the clock twice per collected instance, which at
    /// tens of thousands of instances per frame costs more than the work it measures. The
    /// pass-level cost is covered by the APP_SCOPE_PERF scopes of the callers.
    bool profiling_enabled_ = false;

    void sort_batches(const submit_context& context);
    void count_oversized_batches(const submit_context& context);
    void calculate_camera_distances(const math::vec3& camera_pos);
    void update_statistics();
    auto get_or_create_batch_group(const Key& key) -> batch_group_t<Key>&;
    void grow_buckets();
};

using batch_collector = batch_collector_t<batch_key>;
using shadow_batch_collector = batch_collector_t<shadow_batch_key>;

} // namespace unravel

#include "batch_collector.hpp"
