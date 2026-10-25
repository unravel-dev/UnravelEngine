/*
 * RmlUi world meshes shared between geometries with the same content
 */

#pragma once

#include "RmlUi_WorldMeshPool.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace unravel
{

/**
 * @brief Shares built and uploaded world meshes between geometries with the same vertices and indices.
 *
 * RmlUi compiles a new geometry whenever an element's text or size changes, and the same content keeps
 * coming back: the same health number, the same bar width, the same label on many documents. The cache
 * finds a mesh by a hash of its content, checks the content itself, and keeps the mesh a while after the
 * last geometry holding it is gone, so content that comes back costs neither a build nor an upload.
 */
class RmlUi_WorldMeshCache
{
public:
    explicit RmlUi_WorldMeshCache(RmlUi_WorldMeshPool& pool);
    RmlUi_WorldMeshCache(const RmlUi_WorldMeshCache&) = delete;
    auto operator=(const RmlUi_WorldMeshCache&) -> RmlUi_WorldMeshCache& = delete;

    /// The resident mesh for @p vertices and @p indices built for @p later_edges (build_world_mesh), built
    /// and uploaded on a miss; null for content with nothing to draw.
    auto get(Rml::Span<const Rml::Vertex> vertices,
             Rml::Span<const int> indices,
             const std::vector<RmlUi_WorldOutlineEdge>& later_edges) -> std::shared_ptr<const RmlUi_ResidentWorldMesh>;
    /// Drops the meshes no geometry has held for a while; call once per frame.
    void trim(uint32_t frame);
    void clear();

private:
    struct entry
    {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
        std::vector<RmlUi_WorldOutlineEdge> later_edges;
        /// Null for content with nothing to draw, which is remembered all the same.
        std::shared_ptr<const RmlUi_ResidentWorldMesh> mesh;
        uint32_t last_used = 0;
    };

    static auto is_same_content(const entry& cached,
                                Rml::Span<const Rml::Vertex> vertices,
                                Rml::Span<const int> indices,
                                const std::vector<RmlUi_WorldOutlineEdge>& later_edges) -> bool;

    RmlUi_WorldMeshPool* pool_ = nullptr;
    std::unordered_multimap<uint64_t, entry> entries_;
    uint32_t frame_ = 0;
};

} // namespace unravel
