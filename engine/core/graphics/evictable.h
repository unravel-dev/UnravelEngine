#pragma once

#include <cstdint>

namespace gfx
{

/// Residency state of a GPU resource tracked by the eviction system.
enum class evict_state : std::uint8_t
{
    resident, ///< GPU handle is live and usable.
    evicted,  ///< GPU handle was destroyed; a CPU-side backing is retained for restore.
};

/// Eviction eligibility of a tracked resource.
enum class evict_class : std::uint8_t
{
    non_evictable, ///< No CPU backing (render targets, uniforms, ...). Never registered.
    evictable,     ///< May be evicted by a sweep.
};

namespace detail
{
/// Monotonic frame counter, advanced once per presented frame. Written by eviction::set_frame and
/// read on the hot path; both occur on the render thread, so it needs no synchronization.
extern std::uint64_t g_eviction_frame;
} // namespace detail

class eviction_registry;

/// Abstract contract for a GPU resource that can release its GPU memory (evict) and
/// later recreate it from a retained CPU-side backing (restore).
///
/// Threading contract: @ref touch, eviction and restore all run on the graphics API (render)
/// thread, so the per-resource bookkeeping below needs no synchronization. The only cross-thread
/// access is registration/unregistration during resource create/destroy on worker threads, which
/// the eviction_registry serializes with its own mutex.
class ievictable
{
public:
    ievictable() = default;
    ievictable(const ievictable&) = delete;
    auto operator=(const ievictable&) -> ievictable& = delete;
    ievictable(ievictable&&) = delete;
    auto operator=(ievictable&&) -> ievictable& = delete;
    virtual ~ievictable() = default;

    /// Release the GPU handle. Returns the number of GPU bytes freed. Must be idempotent.
    virtual auto on_evict() -> std::uint64_t = 0;

    /// Recreate the GPU handle from the CPU backing. Returns true on success.
    virtual auto on_restore() -> bool = 0;

    /// Approximate GPU footprint of the resource in bytes.
    [[nodiscard]] virtual auto gpu_size() const -> std::uint64_t = 0;

    [[nodiscard]] auto get_evict_class() const -> evict_class
    {
        return evict_class_;
    }

    [[nodiscard]] auto get_evict_state() const -> evict_state
    {
        return evict_state_;
    }

    [[nodiscard]] auto get_last_use_frame() const -> std::uint64_t
    {
        return last_use_frame_;
    }

    [[nodiscard]] auto get_use_count() const -> std::uint64_t
    {
        return use_count_;
    }

    /// Hot path: mark the resource as used on the current frame. Must be called on the render
    /// thread (it is reached only through native_handle during submission). Strategies read these
    /// counters during a sweep, also on the render thread.
    void touch() const noexcept
    {
        last_use_frame_ = detail::g_eviction_frame;
        ++use_count_;
    }

protected:
    /// Frame the resource was last used on (heuristic, written from @ref touch).
    mutable std::uint64_t last_use_frame_{0};
    /// Number of times the resource was used (heuristic, written from @ref touch).
    mutable std::uint64_t use_count_{0};
    /// Current residency state. Written by the owning resource during evict/restore.
    evict_state evict_state_{evict_state::resident};
    /// Eligibility class. Set once when the resource opts into eviction.
    evict_class evict_class_{evict_class::non_evictable};

private:
    /// Index of the resource inside the registry bucket it currently lives in (O(1) removal).
    std::uint32_t evict_slot_{UINT32_MAX};
    /// Frame the resource was last evicted on (used for thrash detection).
    std::uint64_t evict_frame_{0};

    friend class eviction_registry;
};

} // namespace gfx
