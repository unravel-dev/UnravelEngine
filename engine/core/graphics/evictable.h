#pragma once

#include <atomic>
#include <cstdint>

namespace gfx
{

/// Residency state of a GPU resource tracked by the eviction system.
enum class evict_state : std::uint8_t
{
    resident, ///< The full GPU copy is live.
    reduced,  ///< A GPU copy without its top mips is live (a texture's level above 0).
    evicted,  ///< No GPU copy; a CPU-side backing is kept to create one again.
};

/// Eviction eligibility of a tracked resource.
enum class evict_class : std::uint8_t
{
    non_evictable, ///< No CPU backing (render targets, uniforms, ...). Never registered.
    evictable,     ///< May be evicted by a sweep.
};

/// How a draw reads a texture: whether a copy without its top mips may stand in.
enum class texture_read : std::uint8_t
{
    full,      ///< Needs every mip: texture sizes, texel fetches, mip levels with a meaning.
    any_level, ///< Samples by UV with hardware mip selection.
};

namespace detail
{
/// Monotonic frame counter, advanced once per presented frame by eviction::set_frame on the API
/// thread. Read by every native_handle, on any thread: relaxed atomic, a plain load where it is read.
extern std::atomic<std::uint64_t> g_eviction_frame;

/// The frame g_eviction_frame holds now.
inline auto eviction_frame() noexcept -> std::uint64_t
{
    return g_eviction_frame.load(std::memory_order_relaxed);
}
} // namespace detail

class eviction_registry;

/// Abstract contract for a GPU resource that can release its GPU memory (evict), recreate it from a
/// retained CPU-side backing, and, for a texture, hold a copy without its top mips (a level).
///
/// Level 0 is the full resource; level n lacks the n largest mips. Only the registry changes a
/// resource's state and level, through the callbacks below, and it calls them only while it holds its
/// mutex: a resource that leaves the registry before its own members go (texture's destructor) is
/// never called half destroyed.
///
/// Threading contract: @ref touch, eviction, level changes and restores all run on the graphics API
/// (render) thread, so the per-resource bookkeeping below needs no synchronization. The only
/// cross-thread access is registration/unregistration during resource create/destroy on worker
/// threads, which the eviction_registry serializes with its own mutex.
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

    /// Make the GPU copy the one at @p level: create it from the CPU backing, then release the copy it
    /// replaces. Returns false, keeping the current copy, when it cannot. Resources without levels take
    /// only level 0.
    virtual auto on_set_level(std::uint8_t level) -> bool = 0;

    /// Approximate GPU footprint of the full resource in bytes.
    [[nodiscard]] virtual auto gpu_size() const -> std::uint64_t = 0;

    /// Approximate GPU footprint at @p level; 0 for a level the resource does not have.
    [[nodiscard]] virtual auto gpu_size_at(std::uint8_t level) const -> std::uint64_t
    {
        return level == 0 ? gpu_size() : 0;
    }

    /// The level that keeps only mips whose longer side is at most @p tail_size, which the resource
    /// keeps on the GPU instead of being evicted; 0 when it has no such tail.
    [[nodiscard]] virtual auto get_tail_level(std::uint32_t tail_size) const -> std::uint8_t
    {
        (void)tail_size;
        return 0;
    }

    [[nodiscard]] auto get_evict_class() const -> evict_class
    {
        return evict_class_;
    }

    /// Lock-free: the registry writes it on the API thread, any thread may read it.
    [[nodiscard]] auto get_evict_state() const -> evict_state
    {
        return evict_state_.load(std::memory_order_relaxed);
    }

    /// Top mips the GPU copy lacks: 0 while resident or evicted.
    [[nodiscard]] auto get_level() const -> std::uint8_t
    {
        return level_;
    }

    [[nodiscard]] auto get_last_use_frame() const -> std::uint64_t
    {
        return last_use_frame_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] auto get_last_full_use_frame() const -> std::uint64_t
    {
        return last_full_use_frame_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] auto get_use_count() const -> std::uint64_t
    {
        return use_count_.load(std::memory_order_relaxed);
    }

    /// Whether a read of @p read kind needs the registry first: any read of an evicted resource, and a
    /// full read of a reduced one.
    [[nodiscard]] auto needs_residency_for(texture_read read) const -> bool
    {
        const evict_state state = get_evict_state();
        return state == evict_state::evicted || (state == evict_state::reduced && read == texture_read::full);
    }

    /// Hot path: mark the resource as used on the current frame. Reached through native_handle on
    /// any thread while the registry's passes read these counters on the API thread, so they are
    /// relaxed atomics: plain loads and stores, no lock. The use count is a heuristic and may miss
    /// a count when two threads read at once.
    void touch(texture_read read = texture_read::full) const noexcept
    {
        const std::uint64_t frame = detail::eviction_frame();
        last_use_frame_.store(frame, std::memory_order_relaxed);
        if(read == texture_read::full)
        {
            last_full_use_frame_.store(frame, std::memory_order_relaxed);
        }
        use_count_.store(use_count_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

protected:
    /// Frame the resource was last used on (heuristic, written from @ref touch).
    mutable std::atomic<std::uint64_t> last_use_frame_{0};
    /// Frame the resource was last read with every mip needed (written from @ref touch).
    mutable std::atomic<std::uint64_t> last_full_use_frame_{0};
    /// Number of times the resource was used (heuristic, written from @ref touch).
    mutable std::atomic<std::uint64_t> use_count_{0};
    /// Eligibility class. Set once when the resource opts into eviction.
    evict_class evict_class_{evict_class::non_evictable};

private:
    /// Current residency state. Written by the registry under its mutex; read by native_handle on any thread.
    std::atomic<evict_state> evict_state_{evict_state::resident};
    /// Top mips the GPU copy lacks. Written by the registry.
    std::uint8_t level_{0};
    /// Index of the resource inside the registry bucket it currently lives in (O(1) removal).
    std::uint32_t evict_slot_{UINT32_MAX};
    /// GPU bytes the registry counts for the resource now: its size at its level, 0 while evicted.
    std::uint64_t accounted_bytes_{0};
    /// The full size the registry counted when it placed the resource, taken back when it moves.
    std::uint64_t accounted_full_bytes_{0};
    /// Frame the registry last took memory from the resource; 0 when it never did.
    std::uint64_t drop_frame_{0};
    /// Frame of the last read the drop could see: any read for a whole eviction, a full read for a level
    /// dropped. A read that needs the memory back measures its gap from here.
    std::uint64_t drop_read_frame_{0};
    /// Frames between reads the resource showed when a read last needed what a drop took, while that
    /// gap was short (see eviction_registry::k_reuse_ages); the planner keeps it that much longer.
    std::uint64_t reuse_frames_{0};
    /// Whether the last drop has not been answered by a read yet: only the first read after a drop
    /// measures the gap.
    bool awaiting_read_{false};

    friend class eviction_registry;
};

} // namespace gfx
