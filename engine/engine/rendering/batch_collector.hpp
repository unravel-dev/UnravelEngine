#pragma once

#include <algorithm>
#include <chrono>

namespace unravel
{

template<typename Key>
batch_group_t<Key>::batch_group_t(const Key& key)
    : key(key)
{
}

template<typename Key>
void batch_group_t<Key>::add_instance(const batch_instance& instance)
{
    instances.add_instance(instance);
}

template<typename Key>
void batch_group_t<Key>::reset()
{
    key = Key{};
    key_hash = 0;
    instances.clear();
    camera_distance = 0.0f;
    is_split_batch = false;
}

template<typename Key>
void batch_group_t<Key>::calculate_camera_distance(const math::vec3& camera_pos)
{
    if(instances.empty())
    {
        camera_distance = 0.0f;
        return;
    }

    math::vec3 average_position(0.0f);
    for(const auto& instance : instances)
    {
        if(!instance.world_transform_ptr)
        {
            continue;
        }
        const auto& transform = *instance.world_transform_ptr;
        average_position += math::vec3(transform[3][0], transform[3][1], transform[3][2]);
    }

    average_position /= static_cast<float>(instances.size());
    camera_distance = math::length(average_position - camera_pos);
}

template<typename Key>
auto batch_group_t<Key>::is_valid() const -> bool
{
    return key.is_valid() && !instances.empty();
}

template<typename Key>
auto batch_group_t<Key>::get_gpu_memory_size() const -> size_t
{
    return instances.get_gpu_memory_size();
}

template<typename Key>
batch_collector_t<Key>::batch_collector_t() = default;

template<typename Key>
void batch_collector_t<Key>::collect_renderable(const Key& key, const batch_instance& instance)
{
    if(!key.is_valid() || !instance.is_valid())
    {
        return;
    }

    if(!profiling_enabled_)
    {
        get_or_create_batch_group(key).add_instance(instance);
        return;
    }

    const auto start_time = std::chrono::high_resolution_clock::now();
    get_or_create_batch_group(key).add_instance(instance);
    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    stats_.collection_time_ms += static_cast<float>(duration.count()) / 1000.0f;
}

template<typename Key>
void batch_collector_t<Key>::collect_renderable(const Key& key, const math::mat4& world_transform)
{
    collect_renderable(key, batch_instance(&world_transform));
}

template<typename Key>
void batch_collector_t<Key>::prepare_batches(const submit_context& context)
{
    if(group_count_ == 0)
    {
        return;
    }

    using clock = std::chrono::high_resolution_clock;
    const bool is_profiling = profiling_enabled_ && context.enable_profiling;
    const auto start_time = is_profiling ? clock::now() : clock::time_point{};
    prepared_batches_.clear();
    prepared_batches_.reserve(group_count_);

    for(size_t index = 0; index < group_count_; ++index)
    {
        auto& group = groups_[index];
        if(group.is_valid())
        {
            prepared_batches_.push_back(&group);
        }
    }

    count_oversized_batches(context);

    if(context.enable_distance_sorting)
    {
        calculate_camera_distances(context.camera_position);
    }

    sort_batches(context);
    update_statistics();

    if(is_profiling)
    {
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start_time);
        stats_.preparation_time_ms = static_cast<float>(duration.count()) / 1000.0f;
    }
}

template<typename Key>
auto batch_collector_t<Key>::get_prepared_batches() const -> const batch_list_t&
{
    return prepared_batches_;
}

template<typename Key>
void batch_collector_t<Key>::clear()
{
    prepared_batches_.clear();
    stats_.reset();
    if(group_count_ == 0)
    {
        return;
    }
    for(size_t index = 0; index < group_count_; ++index)
    {
        groups_[index].reset();
    }
    group_count_ = 0;
    std::fill(buckets_.begin(), buckets_.end(), EMPTY_BUCKET);
}

template<typename Key>
auto batch_collector_t<Key>::get_stats() const -> const batch_stats&
{
    return stats_;
}

template<typename Key>
auto batch_collector_t<Key>::get_batch_count() const -> size_t
{
    return group_count_;
}

template<typename Key>
auto batch_collector_t<Key>::get_instance_count() const -> size_t
{
    size_t total = 0;
    for(size_t index = 0; index < group_count_; ++index)
    {
        total += groups_[index].instances.size();
    }
    return total;
}

template<typename Key>
auto batch_collector_t<Key>::has_batches() const -> bool
{
    return group_count_ != 0;
}

template<typename Key>
void batch_collector_t<Key>::set_max_instances_per_batch(uint32_t max_instances)
{
    max_instances_per_batch_ = max_instances;
}

template<typename Key>
void batch_collector_t<Key>::set_profiling_enabled(bool enabled)
{
    profiling_enabled_ = enabled;
}

template<typename Key>
void batch_collector_t<Key>::sort_batches(const submit_context& context)
{
    if constexpr(std::is_same_v<Key, batch_key>)
    {
        std::sort(prepared_batches_.begin(),
                  prepared_batches_.end(),
                  [&context](const batch_group_t<Key>* a, const batch_group_t<Key>* b) -> bool
                  {
                      if(a->key.material_ptr.get() != b->key.material_ptr.get())
                      {
                          return a->key.material_ptr.get() < b->key.material_ptr.get();
                      }
                      if(a->key.mesh_ptr.get() != b->key.mesh_ptr.get())
                      {
                          return a->key.mesh_ptr.get() < b->key.mesh_ptr.get();
                      }
                      if(a->key.lod_index != b->key.lod_index)
                      {
                          return a->key.lod_index < b->key.lod_index;
                      }
                      if(a->key.submesh_index != b->key.submesh_index)
                      {
                          return a->key.submesh_index < b->key.submesh_index;
                      }
                      if(context.enable_distance_sorting)
                      {
                          return a->camera_distance > b->camera_distance;
                      }
                      return false;
                  });
    }
    else
    {
        std::sort(prepared_batches_.begin(),
                  prepared_batches_.end(),
                  [&context](const batch_group_t<Key>* a, const batch_group_t<Key>* b) -> bool
                  {
                      if(a->key != b->key)
                      {
                          return a->key < b->key;
                      }
                      if(context.enable_distance_sorting)
                      {
                          return a->camera_distance > b->camera_distance;
                      }
                      return false;
                  });
    }
}

template<typename Key>
void batch_collector_t<Key>::count_oversized_batches(const submit_context& context)
{
    // The instanced submit chunks an oversized batch by itself; this only feeds the stat.
    if(context.max_instances_per_batch == 0)
    {
        return;
    }
    const auto is_oversized = [&context](const batch_group_t<Key>* batch) -> bool
    {
        return batch->instances.size() > context.max_instances_per_batch;
    };
    stats_.split_batches +=
        static_cast<uint32_t>(std::count_if(prepared_batches_.begin(), prepared_batches_.end(), is_oversized));
}

template<typename Key>
void batch_collector_t<Key>::calculate_camera_distances(const math::vec3& camera_pos)
{
    for(auto* batch : prepared_batches_)
    {
        batch->calculate_camera_distance(camera_pos);
    }
}

template<typename Key>
void batch_collector_t<Key>::update_statistics()
{
    stats_.total_batches = static_cast<uint32_t>(prepared_batches_.size());
    stats_.total_instances = static_cast<uint32_t>(get_instance_count());

    stats_.instance_buffer_memory_used = 0;
    for(const auto* batch : prepared_batches_)
    {
        stats_.instance_buffer_memory_used += batch->get_gpu_memory_size();
    }
    stats_.calculate_derived_stats();
}

template<typename Key>
auto batch_collector_t<Key>::get_or_create_batch_group(const Key& key) -> batch_group_t<Key>&
{
    if((group_count_ + 1) * BUCKETS_PER_GROUP > buckets_.size())
    {
        grow_buckets();
    }
    const size_t key_hash = batch_collector_detail::mix_hash(key.hash());
    const size_t mask = buckets_.size() - 1;
    size_t bucket = key_hash & mask;
    while(buckets_[bucket] != EMPTY_BUCKET)
    {
        auto& group = groups_[buckets_[bucket]];
        if(group.key_hash == key_hash && group.key == key)
        {
            return group;
        }
        bucket = (bucket + 1) & mask;
    }
    // A new group: the slots may move now, and the prepared list would miss it anyway.
    prepared_batches_.clear();
    if(group_count_ == groups_.size())
    {
        groups_.emplace_back();
    }
    auto& group = groups_[group_count_];
    group.key = key;
    group.key_hash = key_hash;
    buckets_[bucket] = static_cast<uint32_t>(group_count_);
    ++group_count_;
    return group;
}

template<typename Key>
void batch_collector_t<Key>::grow_buckets()
{
    const size_t bucket_count = std::max(MIN_BUCKET_COUNT, buckets_.size() * 2);
    buckets_.assign(bucket_count, EMPTY_BUCKET);
    const size_t mask = bucket_count - 1;
    for(size_t index = 0; index < group_count_; ++index)
    {
        size_t bucket = groups_[index].key_hash & mask;
        while(buckets_[bucket] != EMPTY_BUCKET)
        {
            bucket = (bucket + 1) & mask;
        }
        buckets_[bucket] = static_cast<uint32_t>(index);
    }
}

} // namespace unravel
