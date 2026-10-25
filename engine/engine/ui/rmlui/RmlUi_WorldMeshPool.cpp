/*
 * Persistent GPU storage for RmlUi world meshes: a few large buffers that many meshes share
 */

#include "RmlUi_WorldMeshPool.h"

#include <algorithm>
#include <utility>

namespace unravel
{
namespace
{
/// Vertices and indices a page holds unless a mesh needs more: about 4 MB of GPU memory per page.
constexpr uint32_t PAGE_VERTEX_CAPACITY = 1u << 15;
constexpr uint32_t PAGE_INDEX_CAPACITY = 1u << 17;
/// Frames a released range waits before a new mesh may take it: the frames still drawing it are done by then.
constexpr uint32_t RELEASE_DELAY_FRAMES = 3;
} // namespace

RmlUi_WorldMeshPool::~RmlUi_WorldMeshPool()
{
    destroy();
}

void RmlUi_WorldMeshPool::init(const bgfx::VertexLayout& vertex_layout, const bgfx::VertexLayout& shape_layout)
{
    vertex_layout_ = vertex_layout;
    shape_layout_ = shape_layout;
}

auto RmlUi_WorldMeshPool::upload(const RmlUi_WorldMesh& mesh) -> hpp::optional<allocation>
{
    const auto vertex_count = static_cast<uint32_t>(mesh.vertices.size());
    const auto index_count = static_cast<uint32_t>(mesh.indices.size());
    if(vertex_count == 0 || index_count == 0)
    {
        return hpp::nullopt;
    }
    allocation placed;
    bool is_placed = false;
    for(uint32_t page_index = 0; page_index < pages_.size() && !is_placed; ++page_index)
    {
        placed.page = page_index;
        is_placed = try_allocate(pages_[page_index], vertex_count, index_count, placed);
    }
    if(!is_placed)
    {
        if(!add_page(std::max(vertex_count, PAGE_VERTEX_CAPACITY), std::max(index_count, PAGE_INDEX_CAPACITY)))
        {
            return hpp::nullopt;
        }
        placed.page = static_cast<uint32_t>(pages_.size() - 1);
        is_placed = try_allocate(pages_.back(), vertex_count, index_count, placed);
    }
    if(!is_placed)
    {
        return hpp::nullopt;
    }
    const page& target = pages_[placed.page];
    bgfx::update(target.vertex_buffer, placed.first_vertex, bgfx::copy(mesh.vertices.data(), vertex_layout_.getSize(vertex_count)));
    bgfx::update(target.shape_buffer, placed.first_vertex, bgfx::copy(mesh.shapes.data(), shape_layout_.getSize(vertex_count)));
    bgfx::update(target.index_buffer, placed.first_index, bgfx::copy(mesh.indices.data(), index_count * sizeof(int)));
    return placed;
}

void RmlUi_WorldMeshPool::release(const allocation& released)
{
    pending_releases_.push_back({frame_, released});
}

void RmlUi_WorldMeshPool::bind(const allocation& drawn) const
{
    const page& source = pages_[drawn.page];
    bgfx::setVertexBuffer(0, source.vertex_buffer, drawn.first_vertex, drawn.vertex_count);
    bgfx::setVertexBuffer(1, source.shape_buffer, drawn.first_vertex, drawn.vertex_count);
    bgfx::setIndexBuffer(source.index_buffer, drawn.first_index, drawn.index_count);
}

void RmlUi_WorldMeshPool::recycle(uint32_t frame)
{
    frame_ = frame;
    const auto is_due = [&](const pending_release& pending) { return frame_ - pending.frame >= RELEASE_DELAY_FRAMES; };
    for(const pending_release& pending : pending_releases_)
    {
        if(is_due(pending) && pending.released.page < pages_.size())
        {
            page& target = pages_[pending.released.page];
            give_range(target.free_vertices, {pending.released.first_vertex, pending.released.vertex_count});
            give_range(target.free_indices, {pending.released.first_index, pending.released.index_count});
        }
    }
    pending_releases_.erase(std::remove_if(pending_releases_.begin(), pending_releases_.end(), is_due), pending_releases_.end());
}

void RmlUi_WorldMeshPool::destroy()
{
    std::for_each(pages_.begin(), pages_.end(), destroy_buffers);
    pages_.clear();
    pending_releases_.clear();
}

void RmlUi_WorldMeshPool::destroy_buffers(page& destroyed)
{
    if(bgfx::isValid(destroyed.vertex_buffer))
    {
        bgfx::destroy(destroyed.vertex_buffer);
        destroyed.vertex_buffer = BGFX_INVALID_HANDLE;
    }
    if(bgfx::isValid(destroyed.shape_buffer))
    {
        bgfx::destroy(destroyed.shape_buffer);
        destroyed.shape_buffer = BGFX_INVALID_HANDLE;
    }
    if(bgfx::isValid(destroyed.index_buffer))
    {
        bgfx::destroy(destroyed.index_buffer);
        destroyed.index_buffer = BGFX_INVALID_HANDLE;
    }
}

auto RmlUi_WorldMeshPool::add_page(uint32_t vertex_capacity, uint32_t index_capacity) -> bool
{
    page added;
    added.vertex_buffer = bgfx::createDynamicVertexBuffer(vertex_capacity, vertex_layout_);
    added.shape_buffer = bgfx::createDynamicVertexBuffer(vertex_capacity, shape_layout_);
    added.index_buffer = bgfx::createDynamicIndexBuffer(index_capacity, BGFX_BUFFER_INDEX32);
    if(!bgfx::isValid(added.vertex_buffer) || !bgfx::isValid(added.shape_buffer) || !bgfx::isValid(added.index_buffer))
    {
        destroy_buffers(added);
        return false;
    }
    added.free_vertices.push_back({0, vertex_capacity});
    added.free_indices.push_back({0, index_capacity});
    pages_.push_back(std::move(added));
    return true;
}

auto RmlUi_WorldMeshPool::try_allocate(page& target, uint32_t vertex_count, uint32_t index_count, allocation& placed) -> bool
{
    const hpp::optional<uint32_t> first_vertex = take_range(target.free_vertices, vertex_count);
    if(!first_vertex)
    {
        return false;
    }
    const hpp::optional<uint32_t> first_index = take_range(target.free_indices, index_count);
    if(!first_index)
    {
        give_range(target.free_vertices, {*first_vertex, vertex_count});
        return false;
    }
    placed.first_vertex = *first_vertex;
    placed.vertex_count = vertex_count;
    placed.first_index = *first_index;
    placed.index_count = index_count;
    return true;
}

auto RmlUi_WorldMeshPool::take_range(std::vector<range>& free_ranges, uint32_t count) -> hpp::optional<uint32_t>
{
    const auto found = std::find_if(free_ranges.begin(), free_ranges.end(), [&](const range& free) { return free.count >= count; });
    if(found == free_ranges.end())
    {
        return hpp::nullopt;
    }
    const uint32_t first = found->first;
    found->first += count;
    found->count -= count;
    if(found->count == 0)
    {
        free_ranges.erase(found);
    }
    return first;
}

void RmlUi_WorldMeshPool::give_range(std::vector<range>& free_ranges, range returned)
{
    if(returned.count == 0)
    {
        return;
    }
    auto next = std::lower_bound(free_ranges.begin(),
                                 free_ranges.end(),
                                 returned.first,
                                 [](const range& free, uint32_t first) { return free.first < first; });
    next = free_ranges.insert(next, returned);
    if(next + 1 != free_ranges.end() && next->first + next->count == (next + 1)->first)
    {
        next->count += (next + 1)->count;
        free_ranges.erase(next + 1);
    }
    if(next != free_ranges.begin() && (next - 1)->first + (next - 1)->count == next->first)
    {
        (next - 1)->count += next->count;
        free_ranges.erase(next);
    }
}

RmlUi_ResidentWorldMesh::RmlUi_ResidentWorldMesh(RmlUi_WorldMeshPool& pool, RmlUi_WorldMeshPool::allocation placed, RmlUi_WorldOutline outline)
    : pool_(&pool)
    , allocation_(placed)
    , outline_(std::move(outline))
{
}

RmlUi_ResidentWorldMesh::~RmlUi_ResidentWorldMesh()
{
    pool_->release(allocation_);
}

} // namespace unravel
