/*
 * Persistent GPU storage for RmlUi world meshes: a few large buffers that many meshes share
 */

#pragma once

#include "RmlUi_WorldMesh.h"

#include <bgfx/bgfx.h>
#include <hpp/optional.hpp>

#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief Keeps world meshes (RmlUi_WorldMesh) in a few large dynamic vertex and index buffers.
 *
 * A mesh is uploaded once and drawn from its range in later frames, and documents need no buffer handles
 * of their own, so many of them stay far below bgfx's handle limits. A released range is reused only a
 * few frames later, once the frames that drew it are done.
 */
class RmlUi_WorldMeshPool
{
public:
    /// Where a mesh lives: its page and its vertex and index ranges there.
    struct allocation
    {
        uint32_t page = 0;
        uint32_t first_vertex = 0;
        uint32_t vertex_count = 0;
        uint32_t first_index = 0;
        uint32_t index_count = 0;
    };

    RmlUi_WorldMeshPool() = default;
    ~RmlUi_WorldMeshPool();
    RmlUi_WorldMeshPool(const RmlUi_WorldMeshPool&) = delete;
    auto operator=(const RmlUi_WorldMeshPool&) -> RmlUi_WorldMeshPool& = delete;

    void init(const bgfx::VertexLayout& vertex_layout, const bgfx::VertexLayout& shape_layout);
    /// Uploads @p mesh into free ranges, adding a page when none has room; none for an empty mesh.
    auto upload(const RmlUi_WorldMesh& mesh) -> hpp::optional<allocation>;
    /// Gives the ranges of @p released back once the frames that may still draw them are done.
    void release(const allocation& released);
    /// Binds the ranges of @p drawn for the next draw.
    void bind(const allocation& drawn) const;
    /// Frees the ranges released long enough before @p frame; call once per frame.
    void recycle(uint32_t frame);
    /// Destroys the GPU buffers; the meshes in them must be released first.
    void destroy();

private:
    struct range
    {
        uint32_t first = 0;
        uint32_t count = 0;
    };

    struct page
    {
        bgfx::DynamicVertexBufferHandle vertex_buffer = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle shape_buffer = BGFX_INVALID_HANDLE;
        bgfx::DynamicIndexBufferHandle index_buffer = BGFX_INVALID_HANDLE;
        /// Sorted by first, never touching: neighbours merge when a range comes back.
        std::vector<range> free_vertices;
        std::vector<range> free_indices;
    };

    struct pending_release
    {
        uint32_t frame = 0;
        allocation released;
    };

    auto add_page(uint32_t vertex_capacity, uint32_t index_capacity) -> bool;
    static void destroy_buffers(page& destroyed);
    auto try_allocate(page& target, uint32_t vertex_count, uint32_t index_count, allocation& placed) -> bool;
    static auto take_range(std::vector<range>& free_ranges, uint32_t count) -> hpp::optional<uint32_t>;
    static void give_range(std::vector<range>& free_ranges, range returned);

    bgfx::VertexLayout vertex_layout_;
    bgfx::VertexLayout shape_layout_;
    std::vector<page> pages_;
    std::vector<pending_release> pending_releases_;
    uint32_t frame_ = 0;
};

/// A world mesh uploaded into the pool. Its outline stays on the CPU, for a draw before it to compare its
/// own with; its ranges go back to the pool when the last holder lets go.
class RmlUi_ResidentWorldMesh
{
public:
    RmlUi_ResidentWorldMesh(RmlUi_WorldMeshPool& pool, RmlUi_WorldMeshPool::allocation placed, RmlUi_WorldOutline outline);
    ~RmlUi_ResidentWorldMesh();
    RmlUi_ResidentWorldMesh(const RmlUi_ResidentWorldMesh&) = delete;
    auto operator=(const RmlUi_ResidentWorldMesh&) -> RmlUi_ResidentWorldMesh& = delete;

    auto get_allocation() const -> const RmlUi_WorldMeshPool::allocation&
    {
        return allocation_;
    }
    auto get_outline() const -> const RmlUi_WorldOutline&
    {
        return outline_;
    }

private:
    RmlUi_WorldMeshPool* pool_ = nullptr;
    RmlUi_WorldMeshPool::allocation allocation_;
    RmlUi_WorldOutline outline_;
};

} // namespace unravel
