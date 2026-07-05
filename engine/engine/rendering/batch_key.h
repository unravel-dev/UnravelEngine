#pragma once

#include <engine/assets/asset_handle.h>

#include <graphics/texture.h>
#include <math/math.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "material.h"

namespace unravel
{
class mesh;
class material;

/**
 * @brief Batch key structure for grouping compatible draw calls.
 * 
 * Objects with identical batch keys can be rendered together in a single
 * instanced draw call, significantly reducing CPU overhead.
 * Uses shared_ptr to support both asset handles and material/mesh instances.
 */
struct batch_key
{
    /// Shared pointer to the mesh geometry
    std::shared_ptr<mesh> mesh_ptr;
    
    /// Shared pointer to the material
    std::shared_ptr<material> material_ptr;
    
    /// Level of detail index
    uint32_t lod_index = 0;
    
    /// Submesh index within the mesh
    uint32_t submesh_index = 0;
    
    /**
     * @brief Default constructor
     */
    batch_key() = default;
    
    /**
     * @brief Constructor with shared pointers
     * @param mesh_ptr Shared pointer to mesh geometry
     * @param material_ptr Shared pointer to material
     * @param lod_index Level of detail index (0 = highest detail)
     * @param submesh_index Submesh index within the mesh (0-based)
     */
    batch_key(std::shared_ptr<mesh> mesh_ptr,
              std::shared_ptr<material> material_ptr,
              uint32_t lod_index,
              uint32_t submesh_index);
    
    /**
     * @brief Constructor with asset handles (convenience)
     * @param mesh_handle Asset handle to mesh geometry
     * @param material_handle Asset handle to material
     * @param lod_index Level of detail index (0 = highest detail)
     * @param submesh_index Submesh index within the mesh (0-based)
     */
    batch_key(const asset_handle<mesh>& mesh_handle,
              const asset_handle<material>& material_handle,
              uint32_t lod_index,
              uint32_t submesh_index);
    
    /**
     * @brief Three-way comparison operator for sorting and equality
     * @param other Other batch key to compare with
     * @return Comparison result
     */
    auto operator<=>(const batch_key& other) const = default;
    
    /**
     * @brief Equality comparison operator
     * @param other Other batch key to compare with
     * @return True if keys are equal
     */
    auto operator==(const batch_key& other) const -> bool = default;
    
    /**
     * @brief Generate hash value for this batch key
     * @return Hash value suitable for use in hash tables
     */
    auto hash() const noexcept -> size_t;
    
    /**
     * @brief Check if this batch key is valid
     * @return True if both mesh and material pointers are valid
     */
    auto is_valid() const -> bool;
    
    /**
     * @brief Convert batch key to string for debugging
     * @return String representation of the batch key
     */
    auto to_string() const -> std::string;
};

/// Per-draw state for alpha-cutout shadow batches (texture + clip threshold).
struct shadow_cutout_state
{
    std::shared_ptr<gfx::texture> color_map;
    float alpha_test_value = 1.0f;
    math::color base_color{};
    math::vec2 tiling{1.0f, 1.0f};
};

/**
 * @brief Batch key for shadow depth passes — geometry-first, optional cutout bucket.
 *
 * Opaque shadows batch on (mesh, lod, submesh, cull). Alpha-cutout shadows additionally
 * split on @ref shadow_cutout_state so instances sharing the same clip texture/threshold batch.
 */
struct shadow_batch_key
{
    std::shared_ptr<mesh> mesh_ptr;
    uint32_t lod_index = 0;
    uint32_t submesh_index = 0;
    cull_type cull = cull_type::counter_clockwise;
    std::optional<shadow_cutout_state> cutout;

    shadow_batch_key() = default;

    shadow_batch_key(std::shared_ptr<mesh> mesh,
                     uint32_t lod,
                     uint32_t submesh,
                     cull_type cull_type,
                     std::optional<shadow_cutout_state> cutout_state = std::nullopt);

    auto operator<=>(const shadow_batch_key& other) const -> std::strong_ordering;
    auto operator==(const shadow_batch_key& other) const -> bool;

    [[nodiscard]] auto hash() const noexcept -> size_t;
    [[nodiscard]] auto is_valid() const -> bool;
    [[nodiscard]] auto uses_alpha_cutout() const -> bool;
    [[nodiscard]] auto to_string() const -> std::string;
};

/// Build a shadow batch key from mesh geometry and optional material cutout metadata.
auto make_shadow_batch_key(const std::shared_ptr<mesh>& mesh_ptr,
                           uint32_t lod_index,
                           uint32_t submesh_index,
                           const std::shared_ptr<material>& material_ptr) -> shadow_batch_key;

} // namespace unravel

/**
 * @brief Hash specialization for batch_key to enable use in std::unordered_map
 */
namespace std
{
template<>
struct hash<unravel::batch_key>
{
    auto operator()(const unravel::batch_key& key) const noexcept -> size_t
    {
        return key.hash();
    }
};

template<>
struct hash<unravel::shadow_batch_key>
{
    auto operator()(const unravel::shadow_batch_key& key) const noexcept -> size_t
    {
        return key.hash();
    }
};
}
