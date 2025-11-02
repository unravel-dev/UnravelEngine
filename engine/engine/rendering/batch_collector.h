#pragma once

#include "batch_key.h"
#include "batch_instance.h"

#include <unordered_map>
#include <vector>

namespace unravel
{

/**
 * @brief Statistics for batch collection and rendering performance
 */
struct batch_stats
{
    /// Total number of batches created
    uint32_t total_batches = 0;
    
    /// Total number of instances across all batches
    uint32_t total_instances = 0;
    
    /// Number of draw calls saved by batching (instances - batches)
    uint32_t draw_calls_saved = 0;
    
    /// Time spent collecting instances (milliseconds)
    float collection_time_ms = 0.0f;
    
    /// Time spent preparing batches (milliseconds)
    float preparation_time_ms = 0.0f;
    
    /// Time spent submitting batches (milliseconds)
    float submission_time_ms = 0.0f;
    
    /// Memory used for instance buffers (bytes)
    size_t instance_buffer_memory_used = 0;
    
    /// Average number of instances per batch
    float average_batch_size = 0.0f;
    
    /// Batching efficiency (instances / batches)
    float batching_efficiency = 0.0f;
    
    /// Number of batches that were split due to size limits
    uint32_t split_batches = 0;
    
    /**
     * @brief Reset all statistics to zero
     */
    void reset();
    
    /**
     * @brief Calculate derived statistics (efficiency, averages)
     */
    void calculate_derived_stats();
    
    /**
     * @brief Get string representation for debugging
     * @return Formatted statistics string
     */
    auto to_string() const -> std::string;
};

/**
 * @brief Context information for batch submission
 */
struct submit_context
{
    /// View ID for rendering
    uint16_t view_id = 0;
    
    /// Camera position for distance-based sorting
    math::vec3 camera_position = math::vec3(0.0f);
    
    /// Enable distance-based sorting for transparency
    bool enable_distance_sorting = false;
    
    /// Maximum instances per batch (0 = no limit)
    uint32_t max_instances_per_batch = 1024;
    
    /// Enable performance profiling
    bool enable_profiling = true;
};

/**
 * @brief A group of instances that can be rendered together
 */
struct batch_group
{
    /// Key identifying this batch (mesh, material, LOD, submesh)
    batch_key key;
    
    /// Collection of instances in this batch
    batch_instance_collection instances;
    
    /// Distance from camera (for sorting)
    float camera_distance = 0.0f;
    
    /// Whether this batch was split from a larger one
    bool is_split_batch = false;
    
    /**
     * @brief Default constructor
     */
    batch_group() = default;
    
    /**
     * @brief Constructor with key
     * @param key Batch key for this group
     */
    explicit batch_group(const batch_key& key);
    
    /**
     * @brief Add an instance to this batch
     * @param instance Instance to add
     */
    void add_instance(const batch_instance& instance);
    
    /**
     * @brief Calculate distance from camera for sorting
     * @param camera_pos Camera position
     */
    void calculate_camera_distance(const math::vec3& camera_pos);
    
    /**
     * @brief Check if this batch is valid for rendering
     * @return True if batch has valid key and instances
     */
    auto is_valid() const -> bool;
    
    /**
     * @brief Get memory size required for GPU data
     * @return Size in bytes needed for instance buffer
     */
    auto get_gpu_memory_size() const -> size_t;
};

/**
 * @brief Core batch collection system for grouping compatible draw calls
 */
class batch_collector
{
public:
    using batch_map_t = std::unordered_map<batch_key, batch_group>;
    using batch_list_t = std::vector<batch_group*>;
    
    /**
     * @brief Default constructor
     */
    batch_collector();
    
    /**
     * @brief Check if static mesh batching is enabled globally
     * @return True if static mesh batching is enabled
     */
    static auto is_static_mesh_batching_enabled() -> bool;
    
    /**
     * @brief Enable or disable static mesh batching globally
     * @param enabled True to enable batching, false to disable
     */
    static void set_static_mesh_batching_enabled(bool enabled);
    
    /**
     * @brief Destructor
     */
    ~batch_collector() = default;
    
    // Non-copyable but movable
    batch_collector(const batch_collector&) = delete;
    auto operator=(const batch_collector&) -> batch_collector& = delete;
    batch_collector(batch_collector&&) noexcept = default;
    auto operator=(batch_collector&&) noexcept -> batch_collector& = default;
    
    /**
     * @brief Collect a renderable object for batching
     * @param key Batch key identifying the renderable
     * @param instance Instance data for the object
     */
    void collect_renderable(const batch_key& key, const batch_instance& instance);
    
    /**
     * @brief Collect a renderable with just world transform
     * @param key Batch key identifying the renderable
     * @param world_transform World transformation matrix
     */
    void collect_renderable(const batch_key& key, const math::mat4& world_transform);
    
    /**
     * @brief Prepare batches for rendering (sort, split, optimize)
     * @param context Submit context with rendering parameters
     */
    void prepare_batches(const submit_context& context);
    
    /**
     * @brief Get prepared batches for rendering
     * @return Vector of batch groups ready for submission
     */
    auto get_prepared_batches() const -> const batch_list_t&;
    
    /**
     * @brief Clear all collected data and reset for next frame
     */
    void clear();
    
    /**
     * @brief Get current statistics
     * @return Current batch statistics
     */
    auto get_stats() const -> const batch_stats&;
    
    /**
     * @brief Get number of collected batches
     * @return Number of unique batch keys collected
     */
    auto get_batch_count() const -> size_t;
    
    /**
     * @brief Get total number of instances collected
     * @return Total instances across all batches
     */
    auto get_instance_count() const -> size_t;
    
    /**
     * @brief Check if any batches have been collected
     * @return True if there are batches to render
     */
    auto has_batches() const -> bool;
    
    /**
     * @brief Set maximum instances per batch
     * @param max_instances Maximum instances (0 = no limit)
     */
    void set_max_instances_per_batch(uint32_t max_instances);
    
    /**
     * @brief Enable or disable performance profiling
     * @param enabled Whether to enable profiling
     */
    void set_profiling_enabled(bool enabled);

private:
    /// Map of batch keys to batch groups
    batch_map_t batch_groups_;
    
    /// Sorted list of batches ready for rendering
    batch_list_t prepared_batches_;
    
    /// Current statistics
    batch_stats stats_;
    
    /// Maximum instances per batch (0 = no limit)
    uint32_t max_instances_per_batch_ = 1024;
    
    /// Whether profiling is enabled
    bool profiling_enabled_ = true;
    
    /// Global static mesh batching enable/disable flag
    static bool enable_static_mesh_batching_;
    
    /**
     * @brief Sort batches for optimal rendering order
     * @param context Submit context with sorting parameters
     */
    void sort_batches(const submit_context& context);
    
    /**
     * @brief Split large batches that exceed instance limits
     * @param context Submit context with limits
     */
    void split_large_batches(const submit_context& context);
    
    /**
     * @brief Calculate camera distances for all batches
     * @param camera_pos Camera position
     */
    void calculate_camera_distances(const math::vec3& camera_pos);
    
    /**
     * @brief Update statistics after preparation
     */
    void update_statistics();
    
    /**
     * @brief Get or create batch group for key
     * @param key Batch key
     * @return Reference to batch group
     */
    auto get_or_create_batch_group(const batch_key& key) -> batch_group&;
};

} // namespace unravel
