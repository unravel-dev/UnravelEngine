#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/radiance_cache.h>

#include <graphics/graphics.h>

#include <cstdint>

namespace unravel
{

/**
 * @brief GPU mirror of @ref radiance_cache.
 *
 * Two buffers rather than one interleaved structure. The keys are read and compare-exchanged by
 * every probe of every lookup, while the payload is only touched once a slot is resolved;
 * keeping the keys in their own tightly packed buffer means a probe chain walks four adjacent
 * uints instead of striding over 48 bytes of payload per step.
 *
 * Entries are never freed. A slot is reclaimed by being overwritten when its age loses to a
 * newer key, so there is no free list, no compaction pass, and no fragmentation to accumulate.
 */
class radiance_cache_gpu
{
public:
    /// vec4 elements of payload per entry. Must match GI_CACHE_DATA_STRIDE in the shader.
    ///   [0] rgb = accumulated OUTGOING radiance, a = sample count
    ///   [1] xyz = world position the entry represents, w = frame last touched
    ///   [2] xyz = world normal, w = level
    ///   [3] rgb = diffuse albedo of the surface
    ///   [4] rgb = emissive radiance of the surface
    static constexpr uint32_t data_vec4_stride = 5;

    auto init(uint32_t capacity) -> bool;
    void shutdown();

    auto is_valid() const -> bool
    {
        return bgfx::isValid(keys_) && bgfx::isValid(data_);
    }

    /**
     * @brief Zeroes every key, which is what marks a slot free.
     *
     * The payload is deliberately left alone: an entry is only ever read after its key has
     * matched, so stale payload behind a cleared key is unreachable, and clearing 48 bytes per
     * entry instead of 4 would cost twelve times the bandwidth for no observable difference.
     */
    void clear(gfx::view_id view);

    auto get_keys_buffer() const -> gfx::dynamic_index_buffer_handle
    {
        return keys_;
    }

    auto get_data_buffer() const -> gfx::dynamic_vertex_buffer_handle
    {
        return data_;
    }

    auto get_capacity() const -> uint32_t
    {
        return capacity_;
    }

    /**
     * @brief The parameters every key is derived from.
     *
     * They live here, on the cache, rather than on the passes that use them, because a writer and
     * a reader that disagree on any of them derive different keys from the same surface and
     * silently never find each other's entries. That failure looks like an empty cache rather
     * than an error, so a single owner is the only reliable guard against it.
     *
     * `capacity` is informational -- the allocation is fixed by @ref init.
     */
    auto get_settings() const -> const radiance_cache::settings&
    {
        return settings_;
    }

private:
    gfx::dynamic_index_buffer_handle keys_{bgfx::kInvalidHandle};
    gfx::dynamic_vertex_buffer_handle data_{bgfx::kInvalidHandle};
    uint32_t capacity_ = 0;
    radiance_cache::settings settings_;
    bool needs_clear_ = true;
};

} // namespace unravel
