#include "skinning_palette_buffer.h"
#include "model.h"

#include <engine/profiler/profiler.h>

#include <base/hash.hpp>

#include <concurrency/parallel.h>

#include <algorithm>
#include <cassert>

namespace unravel
{
namespace
{
/// Rows the buffer starts with (64 KiB).
constexpr uint32_t INITIAL_CAPACITY_ROWS = 4096;
/// How much the buffer grows by when a frame needs more rows than it holds.
constexpr uint32_t CAPACITY_GROWTH_FACTOR = 2;
/// Fewer palettes than this are packed on the calling thread: the dispatch would cost more than the packing.
constexpr size_t PARALLEL_PACK_MIN_PALETTES = 64;

/// bgfx's release callback for an upload: the render thread is done with the rows.
void release_upload_storage(void* /*data*/, void* user_data)
{
    static_cast<std::atomic<bool>*>(user_data)->store(false, std::memory_order_release);
}
} // namespace

void skinning_palette_rows::clear()
{
    rows_.clear();
}

auto skinning_palette_rows::reserve(size_t bone_count) -> uint32_t
{
    if(bone_count == 0)
    {
        return INVALID_ROW;
    }
    const auto first = static_cast<uint32_t>(rows_.size());
    rows_.resize(rows_.size() + bone_count * ROWS_PER_BONE);
    return first;
}

void skinning_palette_rows::pack(uint32_t first_row, hpp::span<const math::mat4> bones)
{
    math::vec4* rows = rows_.data() + first_row;
    for(const auto& bone : bones)
    {
        pack_bone(bone, rows);
        rows += ROWS_PER_BONE;
    }
}

auto skinning_palette_rows::append(hpp::span<const math::mat4> bones) -> uint32_t
{
    const uint32_t first = reserve(bones.size());
    if(first != INVALID_ROW)
    {
        pack(first, bones);
    }
    return first;
}

auto skinning_palette_rows::get_rows() const -> hpp::span<const math::vec4>
{
    return {rows_.data(), rows_.size()};
}

auto skinning_palette_rows::get_row_count() const -> uint32_t
{
    return static_cast<uint32_t>(rows_.size());
}

void skinning_palette_rows::swap_storage(std::vector<math::vec4>& storage)
{
    rows_.swap(storage);
}

void skinning_palette_rows::pack_bone(const math::mat4& bone, math::vec4* rows)
{
    for(int row = 0; row < static_cast<int>(ROWS_PER_BONE); ++row)
    {
        rows[row] = math::vec4(bone[0][row], bone[1][row], bone[2][row], bone[3][row]);
    }
}

auto skinning_palette_rows::unpack_bone(const math::vec4* rows) -> math::mat4
{
    math::mat4 bone(1.0f);
    for(int row = 0; row < static_cast<int>(ROWS_PER_BONE); ++row)
    {
        for(int column = 0; column < 4; ++column)
        {
            bone[column][row] = rows[row][column];
        }
    }
    return bone;
}

skinning_palette_buffer::skinning_palette_buffer()
{
    layout_.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
}

skinning_palette_buffer::~skinning_palette_buffer()
{
    assert(!bgfx::isValid(buffer_) && "destroy() must run before the graphics backend shuts down");
}

void skinning_palette_buffer::begin_frame()
{
    rows_.clear();
    first_rows_.clear();
    owners_.clear();
    pending_.clear();
    flushed_ = false;
}

auto skinning_palette_buffer::owner_key_hash::operator()(const owner_key& key) const noexcept -> std::size_t
{
    std::size_t hash = std::hash<const void*>{}(key.registry);
    utils::hash_combine(hash, entt::to_integral(key.entity));
    return hash;
}

auto skinning_palette_buffer::acquire(entt::const_handle owner,
                                      const std::vector<pose_mat4>& palettes,
                                      const std::vector<pose_mat4>* previous) -> model_rows
{
    assert(!flushed_ && "palettes acquired after this frame's upload never reach the GPU");
    auto& entry = owners_[owner_key{owner.registry(), owner.entity()}];
    reserve_rows(entry, palettes, previous);
    model_rows result;
    result.current = {first_rows_.data() + entry.first_current, entry.palette_count};
    if(entry.has_previous)
    {
        result.previous = {first_rows_.data() + entry.first_previous, entry.palette_count};
    }
    return result;
}

void skinning_palette_buffer::reserve_rows(owner_entry& entry,
                                           const std::vector<pose_mat4>& palettes,
                                           const std::vector<pose_mat4>* previous)
{
    if(!entry.has_current)
    {
        entry.first_current = static_cast<uint32_t>(first_rows_.size());
        entry.palette_count = static_cast<uint32_t>(palettes.size());
        for(const auto& palette : palettes)
        {
            first_rows_.push_back(queue_palette(palette.transforms));
        }
        entry.has_current = true;
    }
    if(previous != nullptr && !entry.has_previous)
    {
        entry.first_previous = static_cast<uint32_t>(first_rows_.size());
        for(uint32_t index = 0; index < entry.palette_count; ++index)
        {
            const bool has_matching_previous = index < previous->size() &&
                                               (*previous)[index].transforms.size() == palettes[index].transforms.size();
            const uint32_t row = has_matching_previous ? queue_palette((*previous)[index].transforms)
                                                       : first_rows_[entry.first_current + index];
            first_rows_.push_back(row);
        }
        entry.has_previous = true;
    }
}

auto skinning_palette_buffer::queue_palette(hpp::span<const math::mat4> bones) -> uint32_t
{
    const uint32_t first_row = rows_.reserve(bones.size());
    if(first_row != skinning_palette_rows::INVALID_ROW)
    {
        pending_.push_back({bones, first_row});
    }
    return first_row;
}

void skinning_palette_buffer::pack_pending()
{
    if(pending_.empty())
    {
        return;
    }
    APP_SCOPE_PERF("Rendering/Skinning Palettes/Pack");
    // Every job owns its own rows, so the jobs need no synchronisation.
    poolstl::for_each_par_if(pending_.size() >= PARALLEL_PACK_MIN_PALETTES,
                             pending_.begin(),
                             pending_.end(),
                             [&](const pack_job& job)
                             {
                                 rows_.pack(job.first_row, job.bones);
                             });
    pending_.clear();
}

void skinning_palette_buffer::bind()
{
    ensure_buffer();
    bgfx::setBuffer(SKINNING_PALETTE_STAGE, buffer_, bgfx::Access::Read);
}

void skinning_palette_buffer::flush()
{
    APP_SCOPE_PERF("Rendering/Skinning Palettes/Upload");
    pack_pending();
    flushed_ = true;
    uploaded_row_count_ = rows_.get_row_count();
    if(uploaded_row_count_ == 0)
    {
        return;
    }
    ensure_buffer();
    // The rows go to bgfx by reference: the storage stays with this frame's upload until bgfx releases it, and the rows
    // continue in a free storage.
    auto& upload = take_free_storage();
    rows_.swap_storage(upload.rows);
    if(uploaded_row_count_ > capacity_rows_)
    {
        // bgfx resizes the buffer to the update that overflows it: the update carries the headroom, so a crowd that
        // grows a little every frame does not recreate the buffer every frame.
        capacity_rows_ = std::max(uploaded_row_count_, capacity_rows_ * CAPACITY_GROWTH_FACTOR);
        upload.rows.resize(capacity_rows_);
    }
    upload.in_flight.store(true, std::memory_order_relaxed);
    const auto size = static_cast<uint32_t>(upload.rows.size() * sizeof(math::vec4));
    bgfx::update(buffer_, 0, bgfx::makeRef(upload.rows.data(), size, &release_upload_storage, &upload.in_flight));
}

auto skinning_palette_buffer::take_free_storage() -> upload_storage&
{
    for(auto& upload : uploads_)
    {
        if(!upload->in_flight.load(std::memory_order_acquire))
        {
            return *upload;
        }
    }
    // Every storage is still on its way to the GPU (frames in flight): one more.
    uploads_.push_back(std::make_unique<upload_storage>());
    return *uploads_.back();
}

void skinning_palette_buffer::destroy()
{
    if(bgfx::isValid(buffer_))
    {
        bgfx::destroy(buffer_);
        buffer_ = BGFX_INVALID_HANDLE;
    }
}

auto skinning_palette_buffer::get_uploaded_row_count() const -> uint32_t
{
    return uploaded_row_count_;
}

auto skinning_palette_buffer::get_rows() const -> hpp::span<const math::vec4>
{
    return rows_.get_rows();
}

void skinning_palette_buffer::ensure_buffer()
{
    if(bgfx::isValid(buffer_))
    {
        return;
    }
    buffer_ = bgfx::createDynamicVertexBuffer(INITIAL_CAPACITY_ROWS,
                                              layout_,
                                              BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_ALLOW_RESIZE);
    capacity_rows_ = INITIAL_CAPACITY_ROWS;
}

} // namespace unravel
