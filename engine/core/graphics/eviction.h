#pragma once

#include "evictable.h"
#include "graphics.h"
#include "residency_planner.h"

#include <memory>
#include <vector>

/// GPU resource eviction / paging service.
///
/// The registry (eviction_registry.h) tracks every resource that opted in through
/// @ref gfx::handle_impl by retaining a CPU-side backing. Under memory pressure it releases idle
/// resources; a texture with mips above the tail size keeps its smallest mips on the GPU instead, so
/// a material sampling it draws a blurry copy and never waits for an allocation. At frame begin,
/// while there is room, textures read lately rise back toward their full size, a capped number of
/// bytes per frame.
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

inline auto make_backing_ref(const backing_buffer& backing) -> const bgfx::Memory*
{
    auto* bgfx_ref = new backing_buffer(backing);
    return bgfx::makeRef((*bgfx_ref)->data(),
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
using strategy = residency::strategy;

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
///   - @ref hard_limit_bytes      - never cross this; an allocation projected past it must flush.
///   - @ref soft_budget_bytes     - begin evicting once usage exceeds this (defines the "begin work" line).
///   - @ref target_bytes          - evict down to this watermark (hysteresis; should be < soft_budget).
///   - @ref safety_margin_bytes   - added on top of pending allocations when deciding whether
///                                  to evict; keeps reclaim_for from edging exactly to the limit.
///
/// All-zero state means "no budget reported" - every entry point treats this as "do nothing".
struct budget_state
{
    std::uint64_t hard_limit_bytes = 0;
    std::uint64_t soft_budget_bytes = 0;
    std::uint64_t target_bytes = 0;
    std::uint64_t safety_margin_bytes = 0;
    /// Whether the limits hold the GPU bytes the engine's tracked resources hold (a manual budget)
    /// rather than the device's whole usage.
    bool counts_engine_bytes = false;
};

/// Configuration for a single eviction pass.
struct config
{
    /// Victim selection policy.
    strategy strat = strategy::lru;
    /// Run a budget-driven pass only while held bytes exceed this value. 0 disables the budget,
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

/// How textures keep and regain their mips. Set every frame by the driver from the project settings.
struct residency_options
{
    /// Longest side of the mips an evicted texture keeps on the GPU; 0 evicts textures whole.
    std::uint32_t tail_size = 64;
    /// Most bytes rises upload in one frame; 0 for no limit.
    std::uint64_t max_upload_bytes = std::uint64_t(32) * 1024 * 1024;
    /// Whether textures in use lose top mips when the textures on screen exceed the budget.
    bool reduce_in_use = true;
    /// Whether paging may take GPU memory from resources: the frame pass drops, a texture loaded over the
    /// soft budget starts at its tail (sharpening once draws read it and there is room), and making
    /// room for a render target evicts idle resources. Off, nothing is taken and reduced textures
    /// sharpen again.
    bool paging_enabled = true;
};

/// The limit @ref admit_allocation holds a new GPU copy to.
enum class admit_limit : std::uint8_t
{
    /// The soft budget, with usage judged as rises judge it: every create bgfx's memory stats may not
    /// show yet added, no release credited. For a load that can start smaller instead.
    soft_budget,
    /// The hard limit with the safety margin, projected as @ref would_allocation_fit does. For a load
    /// with no smaller form.
    hard_limit,
};

/// GPU bytes admitted for a copy about to be created.
struct admission
{
    /// Whether the copy may be created.
    bool admitted = false;
    /// Bytes counted against the budget until the resource registers: pass them to register_resource,
    /// or to register_evicted_resource when the create failed. 0 when no budget applies.
    std::uint64_t reserved_bytes = 0;
};

/// GPU bytes created and released over the frames bgfx's memory stats may not show yet: the frame
/// being built and the few before it.
struct recent_traffic
{
    std::uint64_t created = 0;
    std::uint64_t released = 0;
};

/// One frame's residency pass, filled by the per-frame driver.
struct frame_request
{
    /// Usage with recent releases credited: what drops are judged by, so they never take more than
    /// the pressure asks while released memory has not shown in the stats yet.
    std::uint64_t used_for_drops = 0;
    /// Usage with recent creates added and releases not credited: what rises are judged by, so they
    /// never overshoot the target while new memory has not shown in the stats yet.
    std::uint64_t used_for_rises = 0;
    /// Drops start above this.
    std::uint64_t soft_budget_bytes = 0;
    /// Drops go down to this; rises stay under it.
    std::uint64_t target_bytes = 0;
    /// Victim order for idle resources.
    strategy strat = strategy::lru;
    /// Resources read within this many frames are not idle; nothing rises within this many frames of
    /// a drop.
    std::uint32_t min_age_frames = 60;
    /// Most drops this frame; 0 for no limit (over the hard limit).
    std::uint32_t max_drops = 0;
    /// Most drops of textures in use this frame, each a texture created; 0 for no limit.
    std::uint32_t max_in_use_drops = 0;
    /// Most rises this frame, each a texture created; 0 for no limit.
    std::uint32_t max_rises = 0;
};

/// Cumulative and last-pass statistics reported by the eviction system.
struct stats
{
    std::uint64_t resident_count = 0;        ///< Resources whose full copy is on the GPU.
    std::uint64_t resident_bytes = 0;        ///< GPU bytes of those full copies.
    std::uint64_t reduced_count = 0;         ///< Textures on the GPU without their top mips.
    std::uint64_t reduced_bytes = 0;         ///< GPU bytes those reduced textures hold.
    std::uint64_t saved_bytes = 0;           ///< GPU bytes the reduced textures save against their full size.
    std::uint64_t evicted_count = 0;         ///< Resources with no GPU copy.
    std::uint64_t evicted_bytes = 0;         ///< Full GPU size of the evicted resources.
    std::uint64_t registered_count = 0;      ///< Total tracked resources (resident + reduced + evicted).
    std::uint64_t total_evictions = 0;       ///< Lifetime number of whole evictions.
    std::uint64_t total_reductions = 0;      ///< Lifetime number of drops to a lower level.
    std::uint64_t total_restores = 0;        ///< Lifetime GPU copies created for an evicted resource.
    std::uint64_t total_raises = 0;          ///< Lifetime rises of a reduced texture.
    std::uint64_t total_bytes_evicted = 0;   ///< Lifetime GPU bytes freed by whole evictions.
    std::uint64_t total_bytes_reduced = 0;   ///< Lifetime GPU bytes freed by drops to a lower level.
    std::uint64_t total_bytes_restored = 0;  ///< Lifetime GPU bytes created for evicted resources.
    std::uint64_t total_bytes_raised = 0;    ///< Lifetime GPU bytes created by rises.
    std::uint64_t failed_restores = 0;       ///< GPU copies that could not be created.
    std::uint64_t thrash_events = 0;         ///< Copies created on demand within min_age_frames of a drop.
    std::uint64_t budget_bytes = 0;          ///< Active budget reported by the driver (0 = none).
    std::uint64_t target_bytes = 0;          ///< Watermark the driver evicts down to.
    std::uint64_t budget_used_bytes = 0;     ///< Metric compared against the budget (e.g. GPU mem used).
    std::uint64_t last_pass_scanned = 0;     ///< Candidates considered by the most recent sweep.
    std::uint64_t last_pass_evicted = 0;     ///< Drops (whole or to a level) of the most recent sweep.
    std::uint64_t last_pass_freed_bytes = 0; ///< GPU bytes those drops freed.
    std::uint64_t last_pass_raised = 0;      ///< Rises of the latest frame pass.
    std::uint64_t last_pass_raised_bytes = 0; ///< GPU bytes those rises uploaded.
    std::uint64_t raise_backlog = 0;         ///< Reduced textures read lately that did not rise in the latest frame pass.
    std::uint64_t short_reads = 0;           ///< Draws that got fewer mips than they read because the hard limit had no room, since start.
    std::uint64_t pending_release_bytes = 0; ///< Destroys queued in bgfx but not yet reflected in gpuMemoryUsed.
    double last_pass_ms = 0.0;               ///< Duration of the most recent sweep in milliseconds.
    double last_restore_ms = 0.0;            ///< Duration of the slowest on-demand restore this frame.
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

/// Free at least @p free_bytes from idle resources (in @p strat order) until the candidate pool is
/// exhausted or @p max_evictions is reached. A texture with a tail goes to its tail; anything else
/// is evicted whole. @p min_age_frames protects recently used resources (0 disables the
/// protection). Must run on the graphics API thread.
auto evict_bytes(std::uint64_t free_bytes,
                 strategy strat = strategy::lru,
                 std::uint32_t min_age_frames = 0,
                 std::uint32_t max_evictions = 0) -> stats;

/// Take every resource down, regardless of budget or age: textures with a tail to it, the rest
/// evicted. Intended for testing/diagnostics. Must run on the graphics API thread.
auto evict_all() -> stats;

/// The frame's residency pass: above the soft budget, idle resources give up memory and, with
/// @ref residency_options::reduce_in_use, textures in use lose a level; under the target, textures
/// read lately rise within the upload limit. Must run on the graphics API thread.
auto update_residency(const frame_request& request) -> stats;

/// Set how textures keep and regain their mips. Cheap; called every frame.
void set_residency_options(const residency_options& options);

/// The options last set.
auto get_residency_options() -> residency_options;

/// The tail size of the options last set, lock-free for the load path on worker threads.
auto get_tail_size() -> std::uint32_t;

/// Longest side of the mips a texture loaded over the soft budget starts with; 0 when loads start
/// whole (paging or mip tails off). Lock-free.
auto get_load_tail_size() -> std::uint32_t;

/// Whether the graphics driver moves resources to system memory when GPU memory runs out, so an
/// allocation past it slows down instead of failing. Windows drivers do; the Linux and Android ones
/// do not, and there a Vulkan allocation past the free memory fails.
auto driver_pages_gpu_memory() -> bool;

/// Bytes kept free under a hard limit of @p hard_limit_bytes when allocations are projected against
/// it. 2% with a floor of 64 MiB; for a device budget on a driver that does not page GPU memory
/// (@p without_driver_paging), 256 MiB plus 4%: the budget sums every device-local heap, the small
/// host-visible one a GPU without a resizable memory window lists beside its memory included, and
/// render targets, buffers and the freed memory bgfx keeps for reuse grow after a check.
auto get_safety_margin(std::uint64_t hard_limit_bytes, bool without_driver_paging) -> std::uint64_t;

/// Admit @p bytes about to be created under @p limit, counting them at once so loads on other threads
/// see them before the resource registers. A manual budget judges the engine's own tracked bytes.
/// Any thread, lock-free. Admits everything and reserves nothing when eviction is unsupported or no
/// budget is set.
auto admit_allocation(std::uint64_t bytes, admit_limit limit) -> admission;

/// GPU bytes created and released by tracked resources and noted allocations over the frames
/// bgfx's memory stats may not show yet.
auto get_recent_traffic() -> recent_traffic;

/// Synchronously guarantee headroom for an imminent GPU allocation of @p bytes (@ref reclaim_kind::immediate).
/// Reads the active @ref budget_state and live backend stats:
///   - If projected occupancy stays under the soft budget, returns @ref reclaim_result::headroom.
///   - Otherwise evicts resident victims (using @ref config::strat and @ref config::min_age_frames from
///     @ref init) down toward @ref budget_state::target_bytes, then flushes so VRAM is free before create.
///   - Called off the graphics API thread it only projects, as @ref reclaim_kind::evictable does.
/// @ref reclaim_kind::evictable is projection-only; prefer @ref would_allocation_fit at load sites.
/// Restore-on-bind must not call this; frame_begin eviction already shaped the pool.
/// Must run on the graphics API thread. A no-op when eviction is unsupported or no budget is set.
auto reclaim_for(std::uint64_t bytes, reclaim_kind kind = reclaim_kind::immediate) -> reclaim_result;

/// Project whether @p bytes fit under the hard limit without running an eviction sweep, counting the
/// bytes admitted for loads still being created. A manual budget judges the engine's own tracked
/// bytes. Used by CPU-backed loads with no smaller form; defers the create when false (CPU backing
/// retained).
auto would_allocation_fit(std::uint64_t bytes) -> bool;

/// Bring every tracked resource back to its full size immediately, warning when that does not fit
/// the budget. Useful for tests and for turning paging off. Must run on the graphics API thread.
auto restore_all() -> stats;

/// Publish the active memory budget for this frame so @ref reclaim_for and tooling see the same
/// numbers. Pure bookkeeping - it does not trigger eviction itself. @p used_bytes is the metric
/// the driver compares against the budget (e.g. gpuMemoryUsed + queued for auto budget, or
/// evictable held bytes for a manual budget); it is recorded only for diagnostics.
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

/// Clear the queued-allocation counters after bgfx::frame or bgfx::flush has processed the command
/// stream; their bytes move into @ref get_recent_traffic for the next few frames. Called from the
/// graphics layer; user code should not need this.
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

/// Begin tracking a resource whose GPU copy is live at @p level (0: full). Called from
/// handle_impl::make_evictable and from a texture loaded without its top mips. @p reserved_bytes
/// from @ref admit_allocation now count as the resource's copy.
void register_resource(ievictable* resource, std::uint8_t level = 0, std::uint64_t reserved_bytes = 0);

/// Begin tracking a resource with no live GPU handle (deferred create / allocation skipped).
/// Counts toward @ref stats::evicted_bytes; @ref request_residency recreates it on access.
/// Releases @p reserved_bytes from @ref admit_allocation for a copy that was not created.
void register_evicted_resource(ievictable* resource, std::uint64_t reserved_bytes = 0);

/// Stop tracking a resource. Called from handle_impl's destructor, or earlier from a derived one.
void unregister_resource(ievictable* resource);

/// Give @p resource the GPU copy a read of @p read kind needs: any read of an evicted resource, and a
/// full read of a reduced texture. An any_level read of an evicted texture with a tail creates only
/// its tail. A texture with a tail whose copy does not fit under the hard limit gets the largest level
/// that does, its tail at least; anything else is created whole, as a draw cannot use less of it.
/// Called from handle_impl::native_handle; off the graphics API thread it does nothing.
void request_residency(ievictable* resource, texture_read read);

} // namespace gfx::eviction
