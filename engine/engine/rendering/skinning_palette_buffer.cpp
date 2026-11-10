#include "skinning_palette_buffer.h"
#include "model.h"

#include <engine/profiler/profiler.h>
#include <engine/rendering/gpu_resource_utils.h>

#include <base/hash.hpp>

#include <concurrency/parallel.h>

#include <algorithm>
#include <cassert>

namespace unravel
{
namespace
{
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

skinning_palette_buffer::skinning_palette_buffer() = default;

skinning_palette_buffer::~skinning_palette_buffer()
{
    assert(!bgfx::isValid(rows_uniform_) && "destroy() must run before the graphics backend shuts down");
}

void skinning_palette_buffer::init()
{
    storage_.set_storage(get_vertex_shader_buffer_storage());
    storage_.init();
    if(!bgfx::isValid(rows_uniform_))
    {
        rows_uniform_ = bgfx::createUniform("u_skin_rows", bgfx::UniformType::Vec4);
    }
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
    owner_entry unowned;
    auto& entry = owner ? owners_[owner_key{owner.registry(), owner.entity()}] : unowned;
    reserve_rows(entry, palettes, previous);
    model_rows result;
    result.current = {first_rows_.data() + entry.first_current, entry.palette_count};
    if(entry.has_previous)
    {
        result.previous = {first_rows_.data() + entry.first_previous, entry.palette_count};
    }
    return result;
}

auto skinning_palette_buffer::acquire_packed(entt::const_handle owner,
                                             const std::vector<pose_mat4>& palettes,
                                             const std::vector<pose_mat4>* previous) -> model_rows
{
    const auto first_job = static_cast<std::ptrdiff_t>(pending_.size());
    const auto rows = acquire(owner, palettes, previous);
    const auto jobs_begin = pending_.begin() + first_job;
    std::for_each(jobs_begin,
                  pending_.end(),
                  [&](const pack_job& job)
                  {
                      rows_.pack(job.first_row, job.bones);
                  });
    pending_.erase(jobs_begin, pending_.end());
    return rows;
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
    ensure_storage();
    storage_.bind(SKINNING_PALETTE_STAGE);
}

void skinning_palette_buffer::bind(uint32_t first_row, uint32_t previous_first_row)
{
    bind();
    const math::vec4 rows(float(first_row), float(previous_first_row), 0.0f, 0.0f);
    bgfx::setUniform(rows_uniform_, math::value_ptr(rows));
}

void skinning_palette_buffer::flush()
{
    APP_SCOPE_PERF("Rendering/Skinning Palettes/Upload");
    pack_pending();
    flushed_ = true;
    uploaded_row_count_ = rows_.get_row_count();
    if(uploaded_row_count_ == 0)
    {
        upload_retired({});
        return;
    }
    ensure_storage();
    if(!storage_.is_valid())
    {
        upload_retired(rows_.get_rows());
        return;
    }
    // The rows go to bgfx by reference: the storage stays with this frame's upload until bgfx releases it, and the rows
    // continue in a free storage.
    auto& upload = take_free_storage();
    rows_.swap_storage(upload.rows);
    upload_retired({upload.rows.data(), uploaded_row_count_});
    upload.in_flight.store(true, std::memory_order_relaxed);
    storage_.update_by_reference(uploaded_row_count_, upload.rows.data(), &release_upload_storage, &upload.in_flight);
}

void skinning_palette_buffer::upload_retired(hpp::span<const math::vec4> rows)
{
    // bgfx keeps a destroyed resource until the frame that used it is rendered.
    for(auto& retired : retired_)
    {
        retired.update(0, static_cast<uint32_t>(rows.size()), rows.data());
        retired.destroy();
    }
    retired_.clear();
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
    upload_retired({});
    storage_.destroy();
    gpu_resource::destroy_handle(rows_uniform_);
}

auto skinning_palette_buffer::get_uploaded_row_count() const -> uint32_t
{
    return uploaded_row_count_;
}

auto skinning_palette_buffer::get_rows() const -> hpp::span<const math::vec4>
{
    return rows_.get_rows();
}

void skinning_palette_buffer::ensure_storage()
{
    const uint32_t reserved = rows_.get_row_count();
    const uint32_t capacity = storage_.get_capacity();
    if(storage_.is_valid() && reserved <= capacity)
    {
        return;
    }
    shader_buffer grown(SKINNING_PALETTE_SAMPLER, shader_buffer_element::float4, storage_.get_storage());
    grown.init();
    grown.reserve(gpu_resource::grow_capacity(capacity, reserved, SKINNING_PALETTE_INITIAL_ROWS));
    if(storage_.is_valid())
    {
        retired_.push_back(std::move(storage_));
    }
    else
    {
        storage_.destroy();
    }
    storage_ = std::move(grown);
}

} // namespace unravel
