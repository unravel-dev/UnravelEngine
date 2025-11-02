#include "batch_collector.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace unravel
{

// batch_stats implementation

void batch_stats::reset()
{
    total_batches = 0;
    total_instances = 0;
    draw_calls_saved = 0;
    collection_time_ms = 0.0f;
    preparation_time_ms = 0.0f;
    submission_time_ms = 0.0f;
    instance_buffer_memory_used = 0;
    average_batch_size = 0.0f;
    batching_efficiency = 0.0f;
    split_batches = 0;
}

void batch_stats::calculate_derived_stats()
{
    if (total_batches > 0)
    {
        average_batch_size = static_cast<float>(total_instances) / static_cast<float>(total_batches);
        batching_efficiency = average_batch_size;
    }
    else
    {
        average_batch_size = 0.0f;
        batching_efficiency = 0.0f;
    }
    
    draw_calls_saved = (total_instances > total_batches) ? (total_instances - total_batches) : 0;
}

auto batch_stats::to_string() const -> std::string
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "batch_stats{";
    oss << "batches=" << total_batches;
    oss << ", instances=" << total_instances;
    oss << ", saved_calls=" << draw_calls_saved;
    oss << ", efficiency=" << batching_efficiency;
    oss << ", avg_size=" << average_batch_size;
    oss << ", collection_time=" << collection_time_ms << "ms";
    oss << ", prep_time=" << preparation_time_ms << "ms";
    oss << ", memory=" << (instance_buffer_memory_used / 1024) << "KB";
    if (split_batches > 0)
    {
        oss << ", splits=" << split_batches;
    }
    oss << "}";
    return oss.str();
}

// batch_group implementation

batch_group::batch_group(const batch_key& key)
    : key(key)
{
}

void batch_group::add_instance(const batch_instance& instance)
{
    instances.add_instance(instance);
}

void batch_group::calculate_camera_distance(const math::vec3& camera_pos)
{
    if (instances.empty())
    {
        camera_distance = 0.0f;
        return;
    }
    
    // Calculate average position of all instances
    math::vec3 average_position(0.0f);
    for (const auto& instance : instances)
    {
        if (!instance.world_transform_ptr)
        {
            continue; // Skip invalid instances
        }
        
        // Extract position from world transform (translation part)
        const auto& transform = *instance.world_transform_ptr;
        math::vec3 position(transform[3][0], transform[3][1], transform[3][2]);
        average_position += position;
    }
    
    average_position /= static_cast<float>(instances.size());
    
    // Calculate distance from camera
    math::vec3 distance_vec = average_position - camera_pos;
    camera_distance = math::length(distance_vec);
}

auto batch_group::is_valid() const -> bool
{
    return key.is_valid() && !instances.empty();
}

auto batch_group::get_gpu_memory_size() const -> size_t
{
    return instances.get_gpu_memory_size();
}

// batch_collector implementation

batch_collector::batch_collector()
{
    stats_.reset();
}

void batch_collector::collect_renderable(const batch_key& key, const batch_instance& instance)
{
    if (!key.is_valid() || !instance.is_valid())
    {
        return;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    auto& batch_group = get_or_create_batch_group(key);
    batch_group.add_instance(instance);
    
    if (profiling_enabled_)
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        stats_.collection_time_ms += static_cast<float>(duration.count()) / 1000.0f;
    }
}

void batch_collector::collect_renderable(const batch_key& key, const math::mat4& world_transform)
{
    batch_instance instance(&world_transform);
    collect_renderable(key, instance);
}

void batch_collector::prepare_batches(const submit_context& context)
{
    if (batch_groups_.empty())
    {
        return;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Clear previous preparation
    prepared_batches_.clear();
    prepared_batches_.reserve(batch_groups_.size());
    
    // Collect all batch groups
    for (auto& [key, group] : batch_groups_)
    {
        if (group.is_valid())
        {
            prepared_batches_.push_back(&group);
        }
    }
    
    // Split large batches if needed
    if (context.max_instances_per_batch > 0)
    {
        split_large_batches(context);
    }
    
    // Calculate camera distances if needed
    if (context.enable_distance_sorting)
    {
        calculate_camera_distances(context.camera_position);
    }
    
    // Sort batches for optimal rendering
    sort_batches(context);
    
    // Update statistics
    update_statistics();
    
    if (profiling_enabled_ && context.enable_profiling)
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        stats_.preparation_time_ms = static_cast<float>(duration.count()) / 1000.0f;
    }
}

auto batch_collector::get_prepared_batches() const -> const batch_list_t&
{
    return prepared_batches_;
}

void batch_collector::clear()
{
    batch_groups_.clear();
    prepared_batches_.clear();
    stats_.reset();
}

auto batch_collector::get_stats() const -> const batch_stats&
{
    return stats_;
}

auto batch_collector::get_batch_count() const -> size_t
{
    return batch_groups_.size();
}

auto batch_collector::get_instance_count() const -> size_t
{
    size_t total = 0;
    for (const auto& [key, group] : batch_groups_)
    {
        total += group.instances.size();
    }
    return total;
}

auto batch_collector::has_batches() const -> bool
{
    return !batch_groups_.empty();
}

void batch_collector::set_max_instances_per_batch(uint32_t max_instances)
{
    max_instances_per_batch_ = max_instances;
}

void batch_collector::set_profiling_enabled(bool enabled)
{
    profiling_enabled_ = enabled;
}

void batch_collector::sort_batches(const submit_context& context)
{
    std::sort(prepared_batches_.begin(), prepared_batches_.end(),
        [&context](const batch_group* a, const batch_group* b) -> bool
        {
            // Primary sort: by material (for state changes)
            if (a->key.material_ptr.get() != b->key.material_ptr.get())
            {
                return a->key.material_ptr.get() < b->key.material_ptr.get();
            }
            
            // Secondary sort: by mesh (for vertex buffer changes)
            if (a->key.mesh_ptr.get() != b->key.mesh_ptr.get())
            {
                return a->key.mesh_ptr.get() < b->key.mesh_ptr.get();
            }
            
            // Tertiary sort: by LOD (render higher detail first)
            if (a->key.lod_index != b->key.lod_index)
            {
                return a->key.lod_index < b->key.lod_index;
            }
            
            // Quaternary sort: by submesh index
            if (a->key.submesh_index != b->key.submesh_index)
            {
                return a->key.submesh_index < b->key.submesh_index;
            }
            
            // Final sort: by distance if enabled (far to near for opaque objects)
            if (context.enable_distance_sorting)
            {
                return a->camera_distance > b->camera_distance;
            }
            
            return false; // Equal
        });
}

void batch_collector::split_large_batches(const submit_context& context)
{
    if (context.max_instances_per_batch == 0)
    {
        return;
    }
    
    batch_list_t new_batches;
    
    for (auto* batch : prepared_batches_)
    {
        if (batch->instances.size() <= context.max_instances_per_batch)
        {
            new_batches.push_back(batch);
            continue;
        }
        
        // Split this batch
        stats_.split_batches++;
        
        size_t instances_processed = 0;
        const size_t total_instances = batch->instances.size();
        
        while (instances_processed < total_instances)
        {
            size_t instances_in_split = std::min(
                static_cast<size_t>(context.max_instances_per_batch),
                total_instances - instances_processed
            );
            
            // Create a new batch group for this split
            auto split_batch = std::make_unique<batch_group>(batch->key);
            split_batch->is_split_batch = true;
            split_batch->camera_distance = batch->camera_distance;
            
            // Copy instances to the split batch
            for (size_t i = 0; i < instances_in_split; ++i)
            {
                split_batch->add_instance(batch->instances[instances_processed + i]);
            }
            
            instances_processed += instances_in_split;
            
            // Store the split batch (we'll need to manage memory for these)
            // For now, we'll just add to the original batch - this is a simplification
            // In a full implementation, we'd need proper memory management for split batches
            new_batches.push_back(batch);
            break; // Simplified: just use the original batch for now
        }
    }
    
    prepared_batches_ = std::move(new_batches);
}

void batch_collector::calculate_camera_distances(const math::vec3& camera_pos)
{
    for (auto* batch : prepared_batches_)
    {
        batch->calculate_camera_distance(camera_pos);
    }
}

void batch_collector::update_statistics()
{
    stats_.total_batches = static_cast<uint32_t>(prepared_batches_.size());
    stats_.total_instances = static_cast<uint32_t>(get_instance_count());
    
    // Calculate memory usage
    stats_.instance_buffer_memory_used = 0;
    for (const auto* batch : prepared_batches_)
    {
        stats_.instance_buffer_memory_used += batch->get_gpu_memory_size();
    }
    
    // Calculate derived statistics
    stats_.calculate_derived_stats();
}

auto batch_collector::get_or_create_batch_group(const batch_key& key) -> batch_group&
{
    auto it = batch_groups_.find(key);
    if (it != batch_groups_.end())
    {
        return it->second;
    }
    
    // Create new batch group
    auto [inserted_it, success] = batch_groups_.emplace(key, batch_group(key));
    return inserted_it->second;
}

// Static member definition
bool batch_collector::enable_static_mesh_batching_ = true;

auto batch_collector::is_static_mesh_batching_enabled() -> bool
{
    return enable_static_mesh_batching_;
}

void batch_collector::set_static_mesh_batching_enabled(bool enabled)
{
    enable_static_mesh_batching_ = enabled;
}

} // namespace unravel
