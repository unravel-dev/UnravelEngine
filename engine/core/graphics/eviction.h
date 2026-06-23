#pragma once

#include "evictable.h"

/// GPU resource eviction / paging service.
///
/// The registry itself is a static singleton defined entirely in eviction.cpp; only the free
/// functions below are exposed. Resources opt into management through @ref gfx::handle_impl by
/// retaining a CPU-side backing, after which they are tracked here and may be evicted (GPU memory
/// released) and restored on demand.
namespace gfx::eviction
{

/// Result of @ref init. Determines whether the system tracks resources at all.
enum class init_status : std::uint8_t
{
    ok,          ///< Initialized and active.
    unnecessary, ///< Eviction is not needed for this backend.
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

/// Restore every currently evicted resource immediately. Useful for testing and for turning paging
/// off. Must run on the graphics API thread.
auto restore_all() -> stats;

/// Report the budget the external driver is currently enforcing, so tooling can display it through
/// @ref get_stats. Pure bookkeeping; it does not trigger eviction. @p used_bytes is the metric the
/// driver compares against the budget (e.g. GPU memory used, or resident bytes for a manual budget).
void report_budget(std::uint64_t used_bytes, std::uint64_t budget_bytes, std::uint64_t target_bytes);

/// Snapshot the current statistics.
auto get_stats() -> stats;

/// Set the global frame counter (call once per presented frame).
void set_frame(std::uint64_t frame);

/// Advance the global frame counter by one.
void advance_frame();

// --- Internal surface used by gfx::handle_impl ----------------------------------------------

/// Begin tracking a resource (assumed resident). Called from handle_impl::make_evictable.
void register_resource(ievictable* resource);

/// Stop tracking a resource. Called from handle_impl's destructor.
void unregister_resource(ievictable* resource);

/// Restore a resource immediately if it is evicted. Called from handle_impl::native_handle on
/// access. Must run on the graphics API thread. No-op if the resource is already resident.
void restore_resource(ievictable* resource);

} // namespace gfx::eviction
