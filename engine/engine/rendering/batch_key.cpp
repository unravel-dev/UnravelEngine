#include "batch_key.h"
#include "mesh.h"
#include "material.h"
#include "material_graph/graph_material.h"
#include <base/hash.hpp>
#include <cmath>
#include <compare>
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

shadow_batch_key::shadow_batch_key(std::shared_ptr<mesh> mesh, uint32_t lod, uint32_t submesh, cull_type cull_type)
    : mesh_ptr(std::move(mesh))
    , lod_index(lod)
    , submesh_index(submesh)
    , cull(cull_type)
{
}

auto shadow_batch_key::operator<=>(const shadow_batch_key& other) const -> std::strong_ordering
{
    if(mesh_ptr.get() != other.mesh_ptr.get())
    {
        return mesh_ptr.get() < other.mesh_ptr.get() ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if(lod_index != other.lod_index)
    {
        return lod_index < other.lod_index ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if(submesh_index != other.submesh_index)
    {
        return submesh_index < other.submesh_index ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if(cull != other.cull)
    {
        return cull < other.cull ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if(masked_graph.get() != other.masked_graph.get())
    {
        return masked_graph.get() < other.masked_graph.get() ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

auto shadow_batch_key::operator==(const shadow_batch_key& other) const -> bool
{
    return (*this <=> other) == 0;
}

auto shadow_batch_key::hash() const noexcept -> size_t
{
    size_t seed = 0;
    utils::hash_combine(seed, mesh_ptr.get());
    utils::hash_combine(seed, lod_index);
    utils::hash_combine(seed, submesh_index);
    utils::hash_combine(seed, static_cast<std::uint8_t>(cull));
    utils::hash_combine(seed, masked_graph.get());
    return seed;
}

auto shadow_batch_key::is_valid() const -> bool
{
    return static_cast<bool>(mesh_ptr);
}

auto shadow_batch_key::uses_graph_mask() const -> bool
{
    return masked_graph != nullptr;
}

auto shadow_batch_key::to_string() const -> std::string
{
    std::ostringstream oss;
    oss << "shadow_batch_key{mesh=" << static_cast<const void*>(mesh_ptr.get());
    oss << ", lod=" << lod_index << ", submesh=" << submesh_index;
    oss << ", masked_graph=" << (masked_graph ? "yes" : "no") << "}";
    return oss.str();
}

auto make_shadow_batch_key(const std::shared_ptr<mesh>& mesh_ptr,
                           uint32_t lod_index,
                           uint32_t submesh_index,
                           const std::shared_ptr<material>& material_ptr) -> shadow_batch_key
{
    const auto* graph = material_ptr ? material_ptr->safe_cast<graph_material>() : nullptr;
    if(graph != nullptr && !graph->casts_shadow())
    {
        return {};
    }
    const cull_type cull = material_ptr ? material_ptr->get_cull_type() : cull_type::counter_clockwise;
    shadow_batch_key key(mesh_ptr, lod_index, submesh_index, cull);
    if(graph != nullptr && graph->draws_own_shadow())
    {
        key.masked_graph = material_ptr;
    }
    return key;
}

} // namespace unravel
