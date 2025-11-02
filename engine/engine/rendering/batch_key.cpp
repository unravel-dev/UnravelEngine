#include "batch_key.h"
#include "mesh.h"
#include "material.h"
#include <base/hash.hpp>
#include <sstream>

namespace unravel
{

batch_key::batch_key(std::shared_ptr<mesh> mesh_ptr,
                    std::shared_ptr<material> material_ptr,
                    uint32_t lod_index,
                    uint32_t submesh_index)
    : mesh_ptr(std::move(mesh_ptr))
    , material_ptr(std::move(material_ptr))
    , lod_index(lod_index)
    , submesh_index(submesh_index)
{
}

batch_key::batch_key(const asset_handle<mesh>& mesh_handle,
                    const asset_handle<material>& material_handle,
                    uint32_t lod_index,
                    uint32_t submesh_index)
    : mesh_ptr(mesh_handle.get())
    , material_ptr(material_handle.get())
    , lod_index(lod_index)
    , submesh_index(submesh_index)
{
}

auto batch_key::hash() const noexcept -> size_t
{
    // Use hash_combine utility to combine all key components
    size_t seed = 0;
    utils::hash_combine(seed, mesh_ptr.get());
    utils::hash_combine(seed, material_ptr.get());
    utils::hash_combine(seed, lod_index);
    utils::hash_combine(seed, submesh_index);
    
    return seed;
}

auto batch_key::is_valid() const -> bool
{
    return mesh_ptr && material_ptr;
}

auto batch_key::to_string() const -> std::string
{
    std::ostringstream oss;
    oss << "batch_key{";
    oss << "mesh=" << static_cast<const void*>(mesh_ptr.get());
    oss << ", material=" << static_cast<const void*>(material_ptr.get());
    oss << ", lod=" << lod_index;
    oss << ", submesh=" << submesh_index;
    oss << "}";
    return oss.str();
}

} // namespace unravel
