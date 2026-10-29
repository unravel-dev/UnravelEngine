#pragma once

#include <math/math.h>

#include <vector>

namespace unravel
{
class material;

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

    /// Previous-frame world transform for this instance (velocity runs only; null = not a
    /// mover / no velocity this frame). Points into model_component's frame-stamped prev
    /// pose storage - same frame-lifetime contract as world_transform_ptr. Consumed by the
    /// velocity pass's instanced submit; the G-buffer instanced path ignores it.
    const math::mat4* prev_world_transform_ptr = nullptr;

    /// The resolved material of a material instance that shares its shared material's draw (the batch key's
    /// material): its per-instance values go into this instance's data. Null to draw with the batch's material.
    /// Frame lifetime, like world_transform_ptr.
    const material* material_override = nullptr;

    /// LOD blending parameters (x = transition factor: +[0,1] = fade out, -[0,1] = fade in; y,z reserved)
    math::vec3 lod_params = math::vec3(0.0f, 0.0f, 0.0f);

    /// Skinned instances only: the first row of the submesh's bone palette in this frame's palette buffer
    /// (skinning_palette_buffer). world_transform_ptr then points at the owner's world matrix, which only sorting reads.
    uint32_t palette_row = 0;
    /// Skinned instances only: the first row of the previous-frame palette (velocity runs), else palette_row.
    uint32_t prev_palette_row = 0;

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

    /// The data of an instance at @p world with LOD parameter @p lod_param.
    instance_vertex_data(const math::mat4& world, float lod_param);

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
 * @brief GPU instance data for the velocity pass's instanced movers submit: the same
 * world matrix layout the G-buffer instanced path uses (LOD packed in [3][3], consumed as
 * i_data0..3) followed by the previous-frame world matrix (i_data4..7). 128-byte stride;
 * packed only for mover instances, so static batches never pay for it.
 */
struct velocity_instance_vertex_data
{
    static auto packed_size() -> size_t
    {
        return 128;
    }

    /// Current world matrix, LOD parameter packed in [3][3] (i_data0..3) - identical to
    /// instance_vertex_data so the raster position matches the G-buffer batched draw.
    math::mat4 world_matrix;
    /// Previous-frame world matrix (i_data4..7); [3][3] content is never read.
    math::mat4 prev_world_matrix;

    velocity_instance_vertex_data() = default;

    explicit velocity_instance_vertex_data(const batch_instance& instance)
        : world_matrix(instance.world_transform_ptr ? *instance.world_transform_ptr : math::mat4(1.0f))
        , prev_world_matrix(instance.prev_world_transform_ptr ? *instance.prev_world_transform_ptr
                            : instance.world_transform_ptr   ? *instance.world_transform_ptr
                                                             : math::mat4(1.0f))
    {
        // Same packing as instance_vertex_data so the raster position bit-matches the
        // G-buffer batched draw.
        world_matrix[3][3] = instance.lod_params.x;
    }

    /// The data of an instance at @p world that was at @p previous last frame, with LOD parameter @p lod_param.
    velocity_instance_vertex_data(const math::mat4& world, const math::mat4& previous, float lod_param)
        : world_matrix(world)
        , prev_world_matrix(previous)
    {
        world_matrix[3][3] = lod_param;
    }
};

/**
 * @brief GPU instance data of a batch whose instances carry per-instance material values: the world matrix packed as
 * instance_vertex_data packs it (i_data0..3, LOD in [3][3]) followed by the material's instance constants (i_data4..7,
 * material::write_instance_constants) - the instance's own material, else the batch's. 128-byte stride, packed only
 * for batches that have such instances.
 */
struct instance_material_vertex_data
{
    static constexpr size_t CONSTANT_COUNT = 4;

    static auto packed_size() -> size_t
    {
        return 128;
    }

    /// Identical to instance_vertex_data, so the raster position matches the velocity pass's.
    math::mat4 world_matrix;
    /// The per-instance material values (i_data4..7).
    math::vec4 constants[CONSTANT_COUNT]{};

    instance_material_vertex_data() = default;
    instance_material_vertex_data(const batch_instance& instance, const material& batch_material);
};

/**
 * @brief GPU instance data of an instanced skinned draw (i_data0): the first row of the instance's bone palette, the
 * first row of its previous-frame palette and the LOD parameter. The rows are float values, exact up to 2^24.
 * 16-byte stride; the G-buffer, velocity and shadow passes all read this layout.
 */
struct skinned_instance_vertex_data
{
    static auto packed_size() -> size_t
    {
        return 16;
    }

    /// x = palette row, y = previous palette row, z = LOD parameter, w unused.
    math::vec4 palette;

    skinned_instance_vertex_data() = default;
    explicit skinned_instance_vertex_data(const batch_instance& instance);
};

/**
 * @brief skinned_instance_vertex_data followed by the material's instance constants (i_data1..4,
 * material::write_instance_constants) - the instance's own material, else the batch's. 80-byte stride, packed only for
 * batches whose instances carry per-instance material values.
 */
struct skinned_instance_material_vertex_data
{
    static auto packed_size() -> size_t
    {
        return 80;
    }

    math::vec4 palette;
    math::vec4 constants[instance_material_vertex_data::CONSTANT_COUNT]{};

    skinned_instance_material_vertex_data() = default;
    skinned_instance_material_vertex_data(const batch_instance& instance, const material& batch_material);
};

/**
 * @brief GPU instance data of a baked animation draw (baked_animation_store): the world matrix as its top three rows
 * (i_data0..2, row i = (m[0][i], m[1][i], m[2][i], m[3][i])) and the instance's frame rows (i_data3). Every submesh
 * of the draw reads it: the shaders add the submesh's palette to the frame row (baked_animation_store::bind) and put
 * the model-space palette under the world matrix. 64-byte stride; the G-buffer and shadow passes read this layout.
 */
struct baked_instance_vertex_data
{
    static auto packed_size() -> size_t
    {
        return 64;
    }

    math::vec4 world[3];
    /// x = first row of the instance's frame, y = of its previous frame, z = LOD parameter, w unused.
    math::vec4 palette;

    baked_instance_vertex_data() = default;
    /// The data of an instance at @p world whose frame starts at @p row (@p previous_row last frame).
    baked_instance_vertex_data(const math::mat4& world, uint32_t row, uint32_t previous_row);
};

/**
 * @brief baked_instance_vertex_data followed by the previous-frame world matrix rows (i_data4..6), for the velocity
 * pass. 112-byte stride.
 */
struct baked_velocity_instance_vertex_data
{
    static auto packed_size() -> size_t
    {
        return 112;
    }

    /// Identical to baked_instance_vertex_data, so the raster position matches the G-buffer's.
    math::vec4 world[3];
    /// As baked_instance_vertex_data::palette.
    math::vec4 palette;
    math::vec4 prev_world[3];

    baked_velocity_instance_vertex_data() = default;
    /// baked_instance_vertex_data() with the instance's matrix of last frame, @p previous.
    baked_velocity_instance_vertex_data(const math::mat4& world,
                                        const math::mat4& previous,
                                        uint32_t row,
                                        uint32_t previous_row);
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
