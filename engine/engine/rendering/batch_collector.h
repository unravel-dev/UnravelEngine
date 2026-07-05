#pragma once

#include "batch_key.h"
#include "batch_instance.h"

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

    batch_group_t() = default;
    explicit batch_group_t(const Key& key);

    void add_instance(const batch_instance& instance);
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
} // namespace batch_collector_detail

template<typename Key>
class batch_collector_t
{
public:
    using batch_map_t = std::unordered_map<Key, batch_group_t<Key>>;
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
    batch_map_t batch_groups_;
    batch_list_t prepared_batches_;
    batch_stats stats_;
    uint32_t max_instances_per_batch_ = 1024;
    bool profiling_enabled_ = true;

    void sort_batches(const submit_context& context);
    void split_large_batches(const submit_context& context);
    void calculate_camera_distances(const math::vec3& camera_pos);
    void update_statistics();
    auto get_or_create_batch_group(const Key& key) -> batch_group_t<Key>&;
};

using batch_collector = batch_collector_t<batch_key>;
using shadow_batch_collector = batch_collector_t<shadow_batch_key>;

} // namespace unravel

#include "batch_collector.hpp"
