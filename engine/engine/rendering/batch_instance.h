#pragma once

#include <math/math.h>

#include <vector>

namespace unravel
{

/**
 * @brief Instance data for a single object in a batch.
 * 
 * Contains all per-instance data needed for rendering, including
 * world transform and LOD parameters.
 */
struct batch_instance
{
    /// Pointer to world transformation matrix for this instance (valid only during frame processing)
    const math::mat4* world_transform_ptr = nullptr;
    
    /// LOD blending parameters (x = transition factor: +[0,1] = fade out, -[0,1] = fade in; y,z reserved)
    math::vec3 lod_params = math::vec3(0.0f, 0.0f, 0.0f);
    
    /// Padding to ensure alignment (unused, reserved for future use)
    float padding = 0.0f;
    
    /**
     * @brief Default constructor
     */
    batch_instance() = default;
    
    /**
     * @brief Constructor with world transform pointer
     * @param world_transform_ptr Pointer to world transformation matrix (must remain valid until batch submission)
     */
    explicit batch_instance(const math::mat4* world_transform_ptr);
    
    /**
     * @brief Constructor with world transform pointer and LOD parameters
     * @param world_transform_ptr Pointer to world transformation matrix (must remain valid until batch submission)
     * @param lod_params LOD blending parameters
     */
    batch_instance(const math::mat4* world_transform_ptr, const math::vec3& lod_params);
    
    /**
     * @brief Check if this instance has valid data
     * @return True if the instance appears to have valid transformation data
     */
    auto is_valid() const -> bool;
    
    /**
     * @brief Get string representation for debugging
     * @return String representation of the instance data
     */
    auto to_string() const -> std::string;
};

/**
 * @brief GPU-friendly vertex data for instancing.
 * 
 * This structure matches the shader input layout and packs the instance
 * data into a single 4x4 matrix for efficient copying. The LOD parameter
 * is packed into the matrix[3][3] position (which is normally 1.0 for homogeneous coordinates).
 * The matrix will be interpreted as 4 vec4 attributes (i_data0 through i_data3) by the shader.
 */
struct instance_vertex_data
{
    static auto packed_size() -> size_t
    {
        return 64;
    }
    /// World transformation matrix with LOD parameter packed in [3][3] (i_data0, i_data1, i_data2, i_data3)
    math::mat4 world_matrix;
    
    /**
     * @brief Default constructor
     */
    instance_vertex_data() = default;
    
    /**
     * @brief Constructor from batch instance
     * @param instance The batch instance to convert
     */
    explicit instance_vertex_data(const batch_instance& instance);
    
    /**
     * @brief Set the LOD parameter (stored in matrix[3][3])
     * @param lod_param LOD parameter value
     */
    void set_lod_param(float lod_param);
    
    /**
     * @brief Get the LOD parameter (from matrix[3][3])
     * @return LOD parameter value
     */
    auto get_lod_param() const -> float;
    
    /**
     * @brief Check if the vertex data is valid
     * @return True if the data appears valid
     */
    auto is_valid() const -> bool;
    
    /**
     * @brief Get string representation for debugging
     * @return String representation of the vertex data
     */
    auto to_string() const -> std::string;
};

/**
 * @brief Collection of batch instances with utilities.
 * 
 * Provides convenient methods for managing collections of instances
 * and converting them to GPU-friendly format.
 */
class batch_instance_collection
{
public:
    using container_type = std::vector<batch_instance>;
    using iterator = container_type::iterator;
    using const_iterator = container_type::const_iterator;
    
    /**
     * @brief Default constructor
     */
    batch_instance_collection() = default;
    
    /**
     * @brief Reserve space for instances
     * @param count Number of instances to reserve space for
     */
    void reserve(size_t count);
    
    /**
     * @brief Add an instance to the collection
     * @param instance Instance to add
     */
    void add_instance(const batch_instance& instance);
    
    /**
     * @brief Add an instance with just world transform
     * @param world_transform World transformation matrix
     */
    void add_instance(const math::mat4& world_transform);
    
    /**
     * @brief Clear all instances
     */
    void clear();
    
    /**
     * @brief Get number of instances
     * @return Number of instances in the collection
     */
    auto size() const -> size_t;
    
    /**
     * @brief Check if collection is empty
     * @return True if no instances are stored
     */
    auto empty() const -> bool;
    
    /**
     * @brief Get instance by index
     * @param index Index of the instance
     * @return Reference to the instance
     */
    auto operator[](size_t index) -> batch_instance&;
    
    /**
     * @brief Get instance by index (const)
     * @param index Index of the instance
     * @return Const reference to the instance
     */
    auto operator[](size_t index) const -> const batch_instance&;
    
    /**
     * @brief Get iterator to beginning
     * @return Iterator to first instance
     */
    auto begin() -> iterator;
    
    /**
     * @brief Get iterator to end
     * @return Iterator past last instance
     */
    auto end() -> iterator;
    
    /**
     * @brief Get const iterator to beginning
     * @return Const iterator to first instance
     */
    auto begin() const -> const_iterator;
    
    /**
     * @brief Get const iterator to end
     * @return Const iterator past last instance
     */
    auto end() const -> const_iterator;
    
    /**
     * @brief Convert all instances to GPU vertex data
     * @return Vector of GPU-friendly vertex data
     */
    auto to_vertex_data() const -> std::vector<instance_vertex_data>;
    
    /**
     * @brief Get memory size required for GPU data
     * @return Size in bytes needed for GPU vertex buffer
     */
    auto get_gpu_memory_size() const -> size_t;

private:
    container_type instances_;
};

// Utility functions

/**
 * @brief Pack batch instance data into GPU vertex format
 * @param instance The batch instance to pack
 * @return GPU-friendly vertex data
 */
auto pack_instance_data(const batch_instance& instance) -> instance_vertex_data;

/**
 * @brief Unpack GPU vertex data back to transform matrix and LOD params
 * @param vertex_data The GPU vertex data to unpack
 * @param out_transform Output matrix to store the unpacked transform
 * @param out_lod_params Output vector to store the unpacked LOD parameters
 */
void unpack_instance_data(const instance_vertex_data& vertex_data, 
                         math::mat4& out_transform, 
                         math::vec3& out_lod_params);

/**
 * @brief Pack multiple instances into GPU vertex data
 * @param instances Vector of batch instances
 * @return Vector of GPU vertex data
 */
auto pack_instances_bulk(const std::vector<batch_instance>& instances) -> std::vector<instance_vertex_data>;

/**
 * @brief Validate that instance data is properly aligned and sized
 * @return True if the structures have correct size and alignment
 */
auto validate_instance_data_layout() -> bool;

} // namespace unravel
