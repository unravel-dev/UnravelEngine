/*
 * RmlUi world meshes shared between geometries with the same content
 */

#include "RmlUi_WorldMeshCache.h"

#include <bx/hash.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace unravel
{
namespace
{
/// Frames a mesh no geometry holds stays cached: long enough for a changing value to come back to it.
constexpr uint32_t UNUSED_MESH_FRAMES = 240;
/// Frames between two passes over the cache for meshes to drop.
constexpr uint32_t TRIM_INTERVAL_FRAMES = 30;

template<typename Value>
auto get_bytes(Rml::Span<const Value> values) -> int32_t
{
    return static_cast<int32_t>(values.size() * sizeof(Value));
}

auto hash_content(Rml::Span<const Rml::Vertex> vertices,
                  Rml::Span<const int> indices,
                  const std::vector<RmlUi_WorldOutlineEdge>& later_edges) -> uint64_t
{
    bx::HashMurmur3_64 hash;
    hash.begin();
    hash.add(vertices.data(), get_bytes(vertices));
    hash.add(indices.data(), get_bytes(indices));
    hash.add(later_edges.data(), static_cast<int32_t>(later_edges.size() * sizeof(RmlUi_WorldOutlineEdge)));
    return hash.end();
}
} // namespace

RmlUi_WorldMeshCache::RmlUi_WorldMeshCache(RmlUi_WorldMeshPool& pool)
    : pool_(&pool)
{
}

auto RmlUi_WorldMeshCache::get(Rml::Span<const Rml::Vertex> vertices,
                               Rml::Span<const int> indices,
                               const std::vector<RmlUi_WorldOutlineEdge>& later_edges)
    -> std::shared_ptr<const RmlUi_ResidentWorldMesh>
{
    const uint64_t key = hash_content(vertices, indices, later_edges);
    const auto [first, last] = entries_.equal_range(key);
    for(auto cached = first; cached != last; ++cached)
    {
        if(is_same_content(cached->second, vertices, indices, later_edges))
        {
            cached->second.last_used = frame_;
            return cached->second.mesh;
        }
    }
    RmlUi_WorldMesh mesh = build_world_mesh(vertices, indices, Rml::Span<const RmlUi_WorldOutlineEdge>(later_edges.data(), later_edges.size()));
    entry added;
    added.vertices.assign(vertices.begin(), vertices.end());
    added.indices.assign(indices.begin(), indices.end());
    added.later_edges = later_edges;
    added.last_used = frame_;
    if(const hpp::optional<RmlUi_WorldMeshPool::allocation> placed = pool_->upload(mesh))
    {
        added.mesh = std::make_shared<const RmlUi_ResidentWorldMesh>(*pool_, *placed, std::move(mesh.outline));
    }
    return entries_.emplace(key, std::move(added))->second.mesh;
}

void RmlUi_WorldMeshCache::trim(uint32_t frame)
{
    if(frame - frame_ < TRIM_INTERVAL_FRAMES)
    {
        return;
    }
    frame_ = frame;
    for(auto cached = entries_.begin(); cached != entries_.end();)
    {
        const bool is_held = cached->second.mesh && cached->second.mesh.use_count() > 1;
        if(is_held)
        {
            cached->second.last_used = frame_;
        }
        if(!is_held && frame_ - cached->second.last_used > UNUSED_MESH_FRAMES)
        {
            cached = entries_.erase(cached);
            continue;
        }
        ++cached;
    }
}

void RmlUi_WorldMeshCache::clear()
{
    entries_.clear();
}

auto RmlUi_WorldMeshCache::is_same_content(const entry& cached,
                                           Rml::Span<const Rml::Vertex> vertices,
                                           Rml::Span<const int> indices,
                                           const std::vector<RmlUi_WorldOutlineEdge>& later_edges) -> bool
{
    return cached.vertices.size() == vertices.size() && cached.indices.size() == indices.size() &&
           std::memcmp(cached.vertices.data(), vertices.data(), static_cast<size_t>(get_bytes(vertices))) == 0 &&
           std::memcmp(cached.indices.data(), indices.data(), static_cast<size_t>(get_bytes(indices))) == 0 &&
           cached.later_edges == later_edges;
}

} // namespace unravel
