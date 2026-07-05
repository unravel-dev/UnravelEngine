#include "batch_key.h"
#include "mesh.h"
#include "material.h"
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

shadow_batch_key::shadow_batch_key(std::shared_ptr<mesh> mesh,
                                   uint32_t lod,
                                   uint32_t submesh,
                                   cull_type cull_type,
                                   std::optional<shadow_cutout_state> cutout_state)
    : mesh_ptr(std::move(mesh))
    , lod_index(lod)
    , submesh_index(submesh)
    , cull(cull_type)
    , cutout(std::move(cutout_state))
{
}

namespace
{
auto compare_float(float lhs, float rhs) -> std::strong_ordering
{
    if(lhs < rhs)
    {
        return std::strong_ordering::less;
    }
    if(lhs > rhs)
    {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

auto compare_shadow_cutout(const std::optional<shadow_cutout_state>& a,
                           const std::optional<shadow_cutout_state>& b) -> std::strong_ordering
{
    if(a.has_value() != b.has_value())
    {
        return a.has_value() ? std::strong_ordering::greater : std::strong_ordering::less;
    }
    if(!a.has_value())
    {
        return std::strong_ordering::equal;
    }

    const shadow_cutout_state& lhs = *a;
    const shadow_cutout_state& rhs = *b;
    if(lhs.color_map.get() != rhs.color_map.get())
    {
        return lhs.color_map.get() < rhs.color_map.get() ? std::strong_ordering::less
                                                         : std::strong_ordering::greater;
    }
    if(const auto cmp = compare_float(lhs.alpha_test_value, rhs.alpha_test_value); cmp != 0)
    {
        return cmp;
    }
    if(const auto cmp = compare_float(lhs.base_color.value.r, rhs.base_color.value.r); cmp != 0)
    {
        return cmp;
    }
    if(const auto cmp = compare_float(lhs.base_color.value.g, rhs.base_color.value.g); cmp != 0)
    {
        return cmp;
    }
    if(const auto cmp = compare_float(lhs.base_color.value.b, rhs.base_color.value.b); cmp != 0)
    {
        return cmp;
    }
    if(const auto cmp = compare_float(lhs.base_color.value.a, rhs.base_color.value.a); cmp != 0)
    {
        return cmp;
    }
    if(const auto cmp = compare_float(lhs.tiling.x, rhs.tiling.x); cmp != 0)
    {
        return cmp;
    }
    return compare_float(lhs.tiling.y, rhs.tiling.y);
}
} // namespace

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
    return compare_shadow_cutout(cutout, other.cutout);
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
    utils::hash_combine(seed, cutout.has_value());
    if(cutout.has_value())
    {
        utils::hash_combine(seed, cutout->color_map.get());
        utils::hash_combine(seed, cutout->alpha_test_value);
        utils::hash_combine(seed, cutout->base_color.value.r);
        utils::hash_combine(seed, cutout->base_color.value.g);
        utils::hash_combine(seed, cutout->base_color.value.b);
        utils::hash_combine(seed, cutout->base_color.value.a);
        utils::hash_combine(seed, cutout->tiling.x);
        utils::hash_combine(seed, cutout->tiling.y);
    }
    return seed;
}

auto shadow_batch_key::is_valid() const -> bool
{
    return static_cast<bool>(mesh_ptr);
}

auto shadow_batch_key::uses_alpha_cutout() const -> bool
{
    return cutout.has_value();
}

auto shadow_batch_key::to_string() const -> std::string
{
    std::ostringstream oss;
    oss << "shadow_batch_key{mesh=" << static_cast<const void*>(mesh_ptr.get());
    oss << ", lod=" << lod_index << ", submesh=" << submesh_index;
    oss << ", cutout=" << (cutout.has_value() ? "yes" : "no") << "}";
    return oss.str();
}

auto make_shadow_batch_key(const std::shared_ptr<mesh>& mesh_ptr,
                           uint32_t lod_index,
                           uint32_t submesh_index,
                           const std::shared_ptr<material>& material_ptr) -> shadow_batch_key
{
    cull_type cull = cull_type::counter_clockwise;
    std::optional<shadow_cutout_state> cutout;
    if(material_ptr)
    {
        cull = material_ptr->get_cull_type();
        if(material_ptr->is<pbr_material>())
        {
            const auto& pbr = static_cast<const pbr_material&>(*material_ptr);
            if(!pbr.casts_shadow())
            {
                return {};
            }
            if(pbr.uses_alpha_cutout())
            {
                cutout = pbr.make_shadow_cutout_state();
            }
        }
    }
    return shadow_batch_key(mesh_ptr, lod_index, submesh_index, cull, cutout);
}

} // namespace unravel
