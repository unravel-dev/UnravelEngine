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

    const auto start_time = std::chrono::high_resolution_clock::now();
    get_or_create_batch_group(key).add_instance(instance);

    if(profiling_enabled_)
    {
        const auto end_time = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        stats_.collection_time_ms += static_cast<float>(duration.count()) / 1000.0f;
    }
}

template<typename Key>
void batch_collector_t<Key>::collect_renderable(const Key& key, const math::mat4& world_transform)
{
    collect_renderable(key, batch_instance(&world_transform));
}

template<typename Key>
void batch_collector_t<Key>::prepare_batches(const submit_context& context)
{
    if(batch_groups_.empty())
    {
        return;
    }

    const auto start_time = std::chrono::high_resolution_clock::now();
    prepared_batches_.clear();
    prepared_batches_.reserve(batch_groups_.size());

    for(auto& [key, group] : batch_groups_)
    {
        if(group.is_valid())
        {
            prepared_batches_.push_back(&group);
        }
    }

    split_large_batches(context);

    if(context.enable_distance_sorting)
    {
        calculate_camera_distances(context.camera_position);
    }

    sort_batches(context);
    update_statistics();

    if(profiling_enabled_ && context.enable_profiling)
    {
        const auto end_time = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
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
    batch_groups_.clear();
    prepared_batches_.clear();
    stats_.reset();
}

template<typename Key>
auto batch_collector_t<Key>::get_stats() const -> const batch_stats&
{
    return stats_;
}

template<typename Key>
auto batch_collector_t<Key>::get_batch_count() const -> size_t
{
    return batch_groups_.size();
}

template<typename Key>
auto batch_collector_t<Key>::get_instance_count() const -> size_t
{
    size_t total = 0;
    for(const auto& [key, group] : batch_groups_)
    {
        total += group.instances.size();
    }
    return total;
}

template<typename Key>
auto batch_collector_t<Key>::has_batches() const -> bool
{
    return !batch_groups_.empty();
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
void batch_collector_t<Key>::split_large_batches(const submit_context& context)
{
    if(context.max_instances_per_batch == 0)
    {
        return;
    }

    batch_list_t new_batches;
    for(auto* batch : prepared_batches_)
    {
        if(batch->instances.size() <= context.max_instances_per_batch)
        {
            new_batches.push_back(batch);
            continue;
        }

        stats_.split_batches++;
        new_batches.push_back(batch);
    }
    prepared_batches_ = std::move(new_batches);
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
    auto it = batch_groups_.find(key);
    if(it != batch_groups_.end())
    {
        return it->second;
    }
    auto [inserted_it, success] = batch_groups_.emplace(key, batch_group_t<Key>(key));
    return inserted_it->second;
}

} // namespace unravel
