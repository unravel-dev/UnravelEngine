#include "batch_instance.h"

#include <sstream>
#include <iomanip>
#include <cassert>

namespace unravel
{

// batch_instance implementation

batch_instance::batch_instance(const math::mat4* world_transform_ptr)
    : world_transform_ptr(world_transform_ptr)
{
}

batch_instance::batch_instance(const math::mat4* world_transform_ptr, const math::vec3& lod_params)
    : world_transform_ptr(world_transform_ptr)
    , lod_params(lod_params)
{
}

auto batch_instance::is_valid() const -> bool
{
    return world_transform_ptr != nullptr;
}

auto batch_instance::to_string() const -> std::string
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "batch_instance{";
    
    if (world_transform_ptr)
    {
        const auto& transform = *world_transform_ptr;
        oss << "transform=[" << transform[0][0] << "," << transform[0][1] << "," << transform[0][2] << "," << transform[0][3] << "; ";
        oss << transform[1][0] << "," << transform[1][1] << "," << transform[1][2] << "," << transform[1][3] << "; ";
        oss << transform[2][0] << "," << transform[2][1] << "," << transform[2][2] << "," << transform[2][3] << "; ";
        oss << transform[3][0] << "," << transform[3][1] << "," << transform[3][2] << "," << transform[3][3] << "], ";
    }
    else
    {
        oss << "transform=null, ";
    }
    
    oss << "lod=[" << lod_params.x << "," << lod_params.y << "," << lod_params.z << "]}";
    return oss.str();
}

// instance_vertex_data implementation

instance_vertex_data::instance_vertex_data(const batch_instance& instance)
    : world_matrix(instance.world_transform_ptr ? *instance.world_transform_ptr : math::mat4(1.0f))
{
    // Pack LOD parameter into matrix[3][3] position (normally 1.0 for homogeneous coordinates)
    world_matrix[3][3] = instance.lod_params.x;
}

void instance_vertex_data::set_lod_param(float lod_param)
{
    world_matrix[3][3] = lod_param;
}

auto instance_vertex_data::get_lod_param() const -> float
{
    return world_matrix[3][3];
}

auto instance_vertex_data::is_valid() const -> bool
{
    // Check all matrix components for finite values
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            if (!std::isfinite(world_matrix[i][j]))
            {
                return false;
            }
        }
    }
    
    return true;
}

auto instance_vertex_data::to_string() const -> std::string
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "instance_vertex_data{";
    oss << "matrix=[";
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0) 
        {
            oss << "; ";
        }
        oss << "[" << world_matrix[i][0] << "," << world_matrix[i][1] << "," << world_matrix[i][2] << "," << world_matrix[i][3] << "]";
    }
    oss << "], lod=" << get_lod_param() << "}";
    return oss.str();
}

// batch_instance_collection implementation

void batch_instance_collection::reserve(size_t count)
{
    instances_.reserve(count);
}

void batch_instance_collection::add_instance(const batch_instance& instance)
{
    instances_.push_back(instance);
}

void batch_instance_collection::add_instance(const math::mat4& world_transform)
{
    instances_.emplace_back(&world_transform);
}

void batch_instance_collection::clear()
{
    instances_.clear();
}

auto batch_instance_collection::size() const -> size_t
{
    return instances_.size();
}

auto batch_instance_collection::empty() const -> bool
{
    return instances_.empty();
}

auto batch_instance_collection::operator[](size_t index) -> batch_instance&
{
    return instances_[index];
}

auto batch_instance_collection::operator[](size_t index) const -> const batch_instance&
{
    return instances_[index];
}

auto batch_instance_collection::begin() -> iterator
{
    return instances_.begin();
}

auto batch_instance_collection::end() -> iterator
{
    return instances_.end();
}

auto batch_instance_collection::begin() const -> const_iterator
{
    return instances_.begin();
}

auto batch_instance_collection::end() const -> const_iterator
{
    return instances_.end();
}

auto batch_instance_collection::to_vertex_data() const -> std::vector<instance_vertex_data>
{
    std::vector<instance_vertex_data> vertex_data;
    vertex_data.reserve(instances_.size());
    
    for (const auto& instance : instances_)
    {
        vertex_data.emplace_back(instance);
    }
    
    return vertex_data;
}

auto batch_instance_collection::get_gpu_memory_size() const -> size_t
{
    return instances_.size() * sizeof(instance_vertex_data);
}

// Utility functions

auto pack_instance_data(const batch_instance& instance) -> instance_vertex_data
{
    return instance_vertex_data(instance);
}

void unpack_instance_data(const instance_vertex_data& vertex_data, 
                         math::mat4& out_transform, 
                         math::vec3& out_lod_params)
{
    // Direct matrix copy
    out_transform = vertex_data.world_matrix;
    
    // Extract LOD parameter from matrix[3][3] and restore homogeneous coordinate
    out_lod_params.x = vertex_data.get_lod_param();
    out_transform[3][3] = 1.0f; // Restore homogeneous coordinate
    
    // Set default values for other LOD parameters
    out_lod_params.y = -1.0f;
    out_lod_params.z = 0.0f;
}

auto pack_instances_bulk(const std::vector<batch_instance>& instances) -> std::vector<instance_vertex_data>
{
    std::vector<instance_vertex_data> vertex_data;
    vertex_data.reserve(instances.size());
    
    for (const auto& instance : instances)
    {
        vertex_data.emplace_back(instance);
    }
    
    return vertex_data;
}

auto validate_instance_data_layout() -> bool
{
    // Verify structure sizes and alignment
    static_assert(sizeof(instance_vertex_data) == sizeof(math::mat4), 
                  "instance_vertex_data must be exactly one mat4");
    
    static_assert(alignof(instance_vertex_data) >= alignof(math::mat4),
                  "instance_vertex_data must be properly aligned for mat4");
    
    // Runtime checks
    bool size_check = sizeof(instance_vertex_data) == 64; // 4 * 16 bytes (mat4)
    bool alignment_check = (sizeof(instance_vertex_data) % 16) == 0;
    
    return size_check && alignment_check;
}

} // namespace unravel
