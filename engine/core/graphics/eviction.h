#pragma once

#include "evictable.h"
#include "graphics.h"

#include <memory>
#include <vector>

/// GPU resource eviction / paging service.
///
/// The registry itself is a static singleton defined entirely in eviction.cpp; only the free
/// functions below are exposed. Resources opt into management through @ref gfx::handle_impl by
/// retaining a CPU-side backing, after which they are tracked here and may be evicted (GPU memory
/// released) and restored on demand.
namespace gfx::eviction
{

using backing_buffer = std::shared_ptr<std::vector<std::uint8_t>>;

inline auto make_backing(const void* data, std::uint32_t size) -> backing_buffer
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    return std::make_shared<std::vector<std::uint8_t>>(bytes, bytes + size);
}

inline void release_backing_ref(void* /*data*/, void* user_data)
{
    delete static_cast<backing_buffer*>(user_data);
}

inline auto make_backing_ref(const backing_buffer& backing) -> const memory_view*
{
    auto* bgfx_ref = new backing_buffer(backing);
    return gfx::make_ref((*bgfx_ref)->data(),
                         static_cast<std::uint32_t>((*bgfx_ref)->size()),
                         release_backing_ref,
                         bgfx_ref);
}

/// Result of @ref init. Determines whether the system tracks resources at all.
enum class init_status : std::uint8_t
{
    ok,          ///< Initialized and active.
    unnecessary, ///< Eviction is not needed for this backend (driver manages residency).
    unsupported, ///< Backend does not report a GPU memory budget; eviction is disabled.
    failed,      ///< Initialization failed (graphics not ready).
};

/// Selection policy used by a sweep to choose eviction victims.
enum class strategy : std::uint8_t
{
    lru,           ///< Least recently used first (smallest last-use frame).
    lfu,           ///< Least frequently used first (smallest use count).
    largest_first, ///< Largest resources first (fastest headroom recovery).
    age_ttl,       ///< Every resource idle for longer than @ref config::max_idle_frames.
};

/// Outcome of a @ref reclaim_for call. Callers may log @ref insufficient as a warning but should
/// still proceed with the allocation: a bgfx-side OOM is non-fatal (the handle returns invalid and
/// the resource is just absent) and we prefer the engine to surface its own message rather than
/// fail silently inside the eviction layer.
enum class reclaim_result : std::uint8_t
{
    headroom,     ///< Already enough headroom; nothing was done.
    reclaimed,    ///< Eviction (and possibly a command-buffer pump) freed enough room.
    insufficient, ///< Eviction could not free enough room; the allocation may exhaust device memory.
};

/// Human-readable name for @ref reclaim_result (for diagnostics / logging).
constexpr auto to_string(reclaim_result r) -> const char*
{
    switch(r)
    {
        case reclaim_result::headroom:     return "headroom";
        case reclaim_result::reclaimed:    return "reclaimed";
        case reclaim_result::insufficient: return "insufficient";
    }
    return "unknown";
}

/// Controls whether @ref reclaim_for pumps the GPU command buffer after evicting.
enum class reclaim_kind : std::uint8_t
{
    /// CPU-backed resources (file/memory textures). Check-only via @ref would_allocation_fit;
    /// never runs an eviction sweep from the load path.
    evictable,
    /// Non-evictable, synchronously allocated GPU resources (render targets). Evict and flush so
    /// destroyed handles release VRAM before the imminent create on the API thread.
    immediate,
};

/// Snapshot of the active memory budget the eviction system enforces. The driver
/// (@ref unravel::update_eviction) populates one of these every frame from the backend stats and
/// the user-facing settings; @ref reclaim_for then reads it back via @ref current_budget.
///
/// Field semantics, expressed in absolute bytes (no fractions of anything):
///   - @ref hard_limit_bytes      — never cross this; an allocation projected past it must flush.
///   - @ref soft_budget_bytes     — begin evicting once usage exceeds this (defines the "begin work" line).
///   - @ref target_bytes          — evict down to this watermark (hysteresis; should be < soft_budget).
///   - @ref safety_margin_bytes   — added on top of pending allocations when deciding whether
///                                  to evict; keeps reclaim_for from edging exactly to the limit.
///
/// All-zero state means "no budget reported" — every entry point treats this as "do nothing".
struct budget_state
{
    std::uint64_t hard_limit_bytes = 0;
    std::uint64_t soft_budget_bytes = 0;
    std::uint64_t target_bytes = 0;
    std::uint64_t safety_margin_bytes = 0;
};

/// Configuration for a single eviction pass.
struct config
{
    /// Victim selection policy.
    strategy strat = strategy::lru;
    /// Run a budget-driven pass only while resident bytes exceed this value. 0 disables the budget,
    /// in which case only @ref strategy::age_ttl performs any work.
    std::uint64_t budget_bytes = 0;
    /// Evict down to this watermark (<= budget) to provide hysteresis. 0 falls back to budget_bytes.
    std::uint64_t target_bytes = 0;
    /// Never evict a resource used within this many frames (anti-thrash protection).
    std::uint32_t min_age_frames = 60;
    /// For @ref strategy::age_ttl: evict anything idle for more than this many frames.
    std::uint32_t max_idle_frames = 0;
    /// Maximum number of resources to evict in a single pass (bounds latency). 0 means unlimited.
    std::uint32_t max_evictions = 0;
};

/// Cumulative and last-pass statistics reported by the eviction system.
struct stats
{
    std::uint64_t resident_count = 0;        ///< Resources currently resident on the GPU.
    std::uint64_t resident_bytes = 0;        ///< GPU bytes currently resident.
    std::uint64_t evicted_count = 0;         ///< Resources currently evicted.
    std::uint64_t evicted_bytes = 0;         ///< GPU bytes currently reclaimed.
    std::uint64_t registered_count = 0;      ///< Total tracked resources (resident + evicted).
    std::uint64_t total_evictions = 0;       ///< Lifetime number of evictions.
    std::uint64_t total_restores = 0;        ///< Lifetime number of restores.
    std::uint64_t total_bytes_evicted = 0;   ///< Lifetime GPU bytes evicted.
    std::uint64_t total_bytes_restored = 0;  ///< Lifetime GPU bytes restored.
    std::uint64_t failed_restores = 0;       ///< Restores that failed to recreate a handle.
    std::uint64_t thrash_events = 0;         ///< Restores within min_age_frames of their eviction.
    std::uint64_t budget_bytes = 0;          ///< Active budget reported by the driver (0 = none).
    std::uint64_t target_bytes = 0;          ///< Watermark the driver evicts down to.
    std::uint64_t budget_used_bytes = 0;     ///< Metric compared against the budget (e.g. GPU mem used).
    std::uint64_t last_pass_scanned = 0;     ///< Candidates considered by the most recent sweep.
    std::uint64_t last_pass_evicted = 0;     ///< Resources evicted by the most recent sweep.
    std::uint64_t last_pass_freed_bytes = 0; ///< GPU bytes reclaimed by the most recent sweep.
    std::uint64_t pending_release_bytes = 0; ///< Destroys queued in bgfx but not yet reflected in gpuMemoryUsed.
    double last_pass_ms = 0.0;               ///< Duration of the most recent sweep in milliseconds.
    double last_restore_ms = 0.0;            ///< Duration of the most recent restore in milliseconds.
};

/// Initialize the system and set the default config used by the argument-less @ref evict.
/// Must be called after the graphics backend has presented at least one frame so the GPU memory
/// budget is available. When the backend does not report a budget the system stays disabled and
/// every entry point becomes a no-op (resources are neither tracked nor CPU-backed).
auto init(const config& cfg = {}) -> init_status;

/// Whether eviction is supported and active on the current backend. Cheap; safe to call from the
/// resource creation path to decide whether to retain a CPU backing.
auto is_supported() -> bool;

/// Stop tracking and release internal bookkeeping. Resources are not destroyed (they own
/// themselves); any evicted resource restores itself on next use.
void shutdown();

/// Run a single eviction pass with an explicit config. Must be called on the graphics API thread.
/// Returns a snapshot of statistics taken after the pass.
auto evict(const config& cfg) -> stats;

/// Run a single eviction pass using the config supplied to @ref init.
auto evict() -> stats;

/// Evict resources (in @p strat order) until at least @p free_bytes have been reclaimed, the
/// candidate pool is exhausted, or @p max_evictions is reached. This is the byte-pressure driven
/// entry point used when a budget is computed externally (e.g. from the GPU memory stats).
/// @p min_age_frames protects recently used resources (0 disables the protection). Must run on the
/// graphics API thread.
auto evict_bytes(std::uint64_t free_bytes,
                 strategy strat = strategy::lru,
                 std::uint32_t min_age_frames = 0,
                 std::uint32_t max_evictions = 0) -> stats;

/// Evict every evictable resource regardless of budget or age. Intended for testing/diagnostics.
/// Must run on the graphics API thread.
auto evict_all() -> stats;

/// Synchronously guarantee headroom for an imminent GPU allocation of @p bytes (@ref reclaim_kind::immediate).
/// Reads the active @ref budget_state and live backend stats:
///   - If projected occupancy stays under the soft budget, returns @ref reclaim_result::headroom.
///   - Otherwise evicts resident victims (using @ref config::strat and @ref config::min_age_frames from
///     @ref init) down toward @ref budget_state::target_bytes, then flushes so VRAM is free before create.
/// @ref reclaim_kind::evictable is projection-only; prefer @ref would_allocation_fit at load sites.
/// Restore-on-bind must not call this; frame_begin eviction already shaped the pool.
/// Must run on the graphics API thread. A no-op when eviction is unsupported or no budget is set.
auto reclaim_for(std::uint64_t bytes, reclaim_kind kind = reclaim_kind::immediate) -> reclaim_result;

/// Project whether @p bytes fit under the hard limit without running an eviction sweep. Used by
/// speculative CPU-backed texture loads; defers the create when false (CPU backing retained).
auto would_allocation_fit(std::uint64_t bytes) -> bool;

/// Restore every currently evicted resource immediately. Calls @ref reclaim_for once up front
/// for the full evicted byte count so a large pending pool cannot push the device past the hard
/// limit. The actual restore loop runs under the registry mutex to keep the evicted set stable.
/// Useful for tests and for turning paging off. Must run on the graphics API thread.
auto restore_all() -> stats;

/// Publish the active memory budget for this frame so @ref reclaim_for and tooling see the same
/// numbers. Pure bookkeeping — it does not trigger eviction itself. @p used_bytes is the metric
/// the driver compares against the budget (e.g. gpuMemoryUsed + queued for auto budget, or
/// evictable resident bytes for a manual budget); it is recorded only for diagnostics.
void set_budget(const budget_state& budget, std::uint64_t used_bytes);

/// Snapshot the most recently published @ref budget_state. Returns an all-zero struct when no
/// budget has been set yet (start-up frame, or eviction is unsupported / unnecessary).
auto current_budget() -> budget_state;

/// Peek the GPU bytes queued since the last bgfx::frame/flush without consuming them. The driver
/// adds this to the backend's gpuMemoryUsed (which lags a frame or more) to react in the same
/// frame, and @ref reclaim_for uses it to project occupancy. Lock-free; cleared by
/// @ref clear_queued_allocations.
auto peek_queued_bytes() -> std::uint64_t;

/// GPU bytes from destroys/evictions queued in bgfx but not yet reflected in gpuMemoryUsed. Credited
/// against occupancy projections until @ref clear_queued_allocations runs after a pump.
auto peek_pending_release_bytes() -> std::uint64_t;

/// In-flight bytes from non-evictable creates (render targets, etc.) recorded via
/// @ref note_pending_allocation. Used by the manual-budget driver path.
auto peek_external_queued_bytes() -> std::uint64_t;

/// Record a newly queued GPU allocation. Used for allocations that are not registered as evictable
/// resources (e.g. render targets / compute write textures). Complements the @p bytes argument passed
/// to @ref reclaim_for for the same create: reclaim counts the imminent allocation once; this keeps
/// it visible in @ref peek_queued_bytes for later same-frame creates until @ref clear_queued_allocations.
void note_pending_allocation(std::uint64_t bytes);

/// Clear the queued-allocation counter after bgfx::frame or bgfx::flush has processed the command
/// stream. Called from the graphics layer; user code should not need this.
void clear_queued_allocations();

/// Snapshot the current statistics.
auto get_stats() -> stats;

/// Set the global frame counter (call once per presented frame).
void set_frame(std::uint64_t frame);

/// Advance the global frame counter by one.
void advance_frame();

// --- Test / diagnostics ---------------------------------------------------------------------

/// Reserve raw GPU memory to artificially raise device occupancy so the eviction and near-the-limit
/// allocation paths can be exercised on GPUs that have plenty of memory. The reserved memory is
/// plain, non-evictable GPU storage held outside the registry (uninitialized RGBA8 textures), so it
/// genuinely pins VRAM and forces the system to evict real resources. Calls are additive: @p bytes
/// is added on top of whatever is already reserved (rounded up to whole chunks). Stops early if an
/// allocation fails. Returns the total reserved bytes after the call. Must run on the graphics API
/// thread.
auto debug_consume_memory(std::uint64_t bytes) -> std::uint64_t;

/// Reserve memory so that the device behaves as if it only had @p target_free_bytes of GPU memory
/// available, i.e. it reserves (real budget - current usage - @p target_free_bytes) of VRAM in one
/// call. This is an absolute target: any existing reservation is released first so repeated calls
/// with different targets are stable. Genuinely allocates VRAM (so the engine hits real out-of-memory
/// behavior at the simulated limit). No-op if the backend reports no budget or the target already
/// fits. Returns the total reserved bytes after the call. Must run on the graphics API thread.
auto debug_simulate_budget(std::uint64_t target_free_bytes) -> std::uint64_t;

/// Release all memory reserved by @ref debug_consume_memory and reclaim its VRAM immediately. Must
/// run on the graphics API thread.
void debug_release_memory();

/// Total bytes currently reserved by @ref debug_consume_memory.
auto debug_consumed_bytes() -> std::uint64_t;

// --- Internal surface used by gfx::handle_impl ----------------------------------------------

/// Begin tracking a resource (assumed resident). Called from handle_impl::make_evictable.
void register_resource(ievictable* resource);

/// Begin tracking a resource with no live GPU handle (deferred create / allocation skipped).
/// Counts toward @ref stats::evicted_bytes; @ref restore_resource recreates it on access.
void register_evicted_resource(ievictable* resource);

/// Stop tracking a resource. Called from handle_impl's destructor.
void unregister_resource(ievictable* resource);

/// Restore a resource immediately if it is evicted. Called from handle_impl::native_handle on
/// access. Must run on the graphics API thread. No-op if the resource is already resident.
void restore_resource(ievictable* resource);

} // namespace gfx::eviction
