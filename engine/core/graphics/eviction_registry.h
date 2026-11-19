#pragma once

#include "eviction.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace gfx
{

/// Registry of evictable GPU resources. Owns no resources; it tracks raw pointers whose lifetime is
/// bounded by register/unregister calls made from handle_impl.
///
/// One instance serves the free functions in gfx::eviction (@ref instance). A test makes its own and
/// activates it with a budget, since the test backend reports none.
///
/// Every change of a resource's state or level goes through one transition that applies the byte
/// difference to the totals, and every callback into a resource runs under the registry's mutex.
class eviction_registry
{
public:
    eviction_registry() = default;
    eviction_registry(const eviction_registry&) = delete;
    auto operator=(const eviction_registry&) -> eviction_registry& = delete;
    eviction_registry(eviction_registry&&) = delete;
    auto operator=(eviction_registry&&) -> eviction_registry& = delete;
    ~eviction_registry() = default;

    /// The registry behind gfx::eviction.
    static auto instance() -> eviction_registry&;

    /// Lock-free; the hot-path is_supported() reads it.
    auto get_init_status() const -> eviction::init_status;

    /// Records a status other than ok: nothing is tracked.
    void set_inactive(eviction::init_status status);

    /// Starts tracking with @p cfg as the default pass config and @p startup_budget published until the
    /// first frame's driver runs. The calling thread becomes the graphics API thread.
    void activate(const eviction::config& cfg, const eviction::budget_state& startup_budget, std::uint64_t used_bytes);

    /// Whether the caller is the thread that activated the registry, the only one that may change a
    /// resource's GPU copy: the render code reads handles without a lock.
    auto is_api_thread() const -> bool;

    /// Forgets every resource and counter; resources keep their handles, a reduced texture the copy it has.
    void shutdown();

    /// Tracks @p r, whose GPU copy is live at @p level (0: full), and hands over @p admitted_bytes from
    /// @ref try_admit, which its copy now counts as. Any thread; a resource registers once.
    void register_resource(ievictable* r, std::uint8_t level, std::uint64_t admitted_bytes = 0);
    /// Tracks @p r with no GPU copy yet: its first read creates one. Releases @p admitted_bytes from
    /// @ref try_admit for a copy that was not created. Any thread.
    void register_evicted_resource(ievictable* r, std::uint64_t admitted_bytes = 0);
    /// Stops tracking @p r; after it returns the registry never calls into @p r. Any thread.
    void unregister_resource(ievictable* r);
    /// Creates the GPU copy a read of @p read kind needs (see gfx::eviction::request_residency). Does nothing
    /// off the API thread: the read takes the copy there is.
    void request_residency(ievictable* r, texture_read read);

    /// One pass with @p cfg (gfx::eviction::evict). API thread.
    auto evict(const eviction::config& cfg) -> eviction::stats;
    /// One pass with the config given to @ref activate. API thread.
    auto evict_default() -> eviction::stats;
    /// Frees at least @p free_bytes from idle resources (gfx::eviction::evict_bytes). API thread.
    auto evict_bytes(std::uint64_t free_bytes,
                     eviction::strategy strat,
                     std::uint32_t min_age_frames,
                     std::uint32_t max_evictions) -> eviction::stats;
    /// Takes every resource down regardless of age (gfx::eviction::evict_all). API thread.
    auto evict_all() -> eviction::stats;
    /// Brings every resource back to its full size. API thread.
    auto restore_all() -> eviction::stats;
    /// The frame's drops and rises (gfx::eviction::update_residency). API thread.
    auto update_residency(const eviction::frame_request& request) -> eviction::stats;

    /// Sets how textures keep and regain their mips.
    void set_residency_options(const eviction::residency_options& options);
    /// The options last set.
    auto get_residency_options() const -> eviction::residency_options;
    /// The tail size of the options last set, lock-free.
    auto get_tail_size() const -> std::uint32_t;
    /// The tail size a texture loaded over the soft budget starts with; 0 when loads start whole.
    /// Lock-free.
    auto get_load_tail_size() const -> std::uint32_t;
    /// Whether paging may take memory from resources (eviction::residency_options::paging_enabled).
    /// Lock-free.
    auto is_paging_enabled() const -> bool;

    /// Admits @p bytes about to be created when @p used_bytes, the bytes admitted before and @p bytes
    /// stay within @p limit_bytes, and counts them at once. Any thread, lock-free: admissions on
    /// several threads never pass the limit together. The resource hands them back when it registers.
    auto try_admit(std::uint64_t bytes, std::uint64_t used_bytes, std::uint64_t limit_bytes) -> bool;
    /// Bytes admitted whose resources have not registered yet. Lock-free.
    auto peek_admitted_bytes() const -> std::uint64_t;
    /// GPU bytes the tracked resources hold now. Lock-free.
    auto peek_held_bytes() const -> std::uint64_t;
    /// Bytes created over the frames bgfx's memory stats may not show yet, this one included.
    /// Lock-free; never lower than the bytes created in those frames.
    auto peek_recent_created_bytes() const -> std::uint64_t;

    /// Publishes the frame's budget and the usage the driver measured.
    void set_budget(const eviction::budget_state& budget, std::uint64_t used_bytes);
    /// The budget last published, lock-free.
    auto current_budget() const -> eviction::budget_state;
    /// The config given to @ref activate.
    auto snapshot_default_config() const -> eviction::config;
    /// The counters and totals now.
    auto get_stats() const -> eviction::stats;

    /// Counts @p bytes of an untracked GPU allocation queued this frame. Any thread, lock-free.
    void note_pending_allocation(std::uint64_t bytes);
    /// Untracked bytes queued this frame.
    auto peek_external_queued_bytes() const -> std::uint64_t;
    /// Bytes released this frame.
    auto peek_pending_release_bytes() const -> std::uint64_t;
    /// Bytes created this frame, tracked or not.
    auto peek_queued_bytes() const -> std::uint64_t;
    /// Bytes created and released over the frames in flight (gfx::eviction::get_recent_traffic).
    auto get_recent_traffic() const -> eviction::recent_traffic;

    /// bgfx processed the command stream (frame or flush): this frame's created and released bytes
    /// move into the recent traffic window.
    void on_frame_submitted();

    /// Resets the per-frame peaks the profiler reads.
    void on_frame_advanced();

private:
    /// Frames bgfx may still show the old memory use for after a create or destroy: its frames in
    /// flight, whose destroyed resources it frees only once the GPU is done with them.
    static constexpr std::size_t k_traffic_frames = 3;
    /// Longest gap between reads, in minimum ages, a resource keeps after a read needed what a drop
    /// took. A resource read less often than this drops again once the minimum age passes.
    static constexpr std::uint64_t k_reuse_ages = 16;

    struct frame_traffic
    {
        std::uint64_t created = 0;
        std::uint64_t released = 0;
    };

    auto bucket_for(evict_state state) -> std::vector<ievictable*>&;
    void place_locked(ievictable* r, evict_state state, std::uint8_t level);
    void remove_locked(ievictable* r);
    void move_locked(ievictable* r, evict_state state, std::uint8_t level);

    void note_created_locked(std::uint64_t bytes);
    void note_released_locked(std::uint64_t bytes);
    /// Returns @p bytes admitted by @ref try_admit: their resource now counts them itself, or was not
    /// created.
    void release_admission(std::uint64_t bytes);
    /// Whether @p r may start being tracked: the registry is active and @p r is not tracked yet.
    auto can_register_locked(const ievictable* r) const -> bool;
    void publish_held_locked();

    auto get_tail_level_locked(const ievictable& r) const -> std::uint8_t;
    auto make_candidate_locked(const ievictable& r) const -> residency::candidate;
    /// Which resources a pass hands the planner.
    enum class plan_scope : std::uint8_t
    {
        /// Every tracked resource with a GPU copy: passes that may drop.
        all,
        /// Reduced resources read within the request's wanted frames: rises and the backlog only.
        wanted_reduced,
    };

    auto run_plan_locked(const residency::request& req, plan_scope scope) -> residency::plan_result;
    void apply_plan_locked(const residency::plan_result& plan);

    void drop_whole_locked(ievictable* r);
    void drop_to_level_locked(ievictable* r, std::uint8_t level);
    void raise_locked(ievictable* r, std::uint8_t level);
    /// Creates @p r's copy at @p level from nothing or a smaller copy and counts it; false when the
    /// create failed and @p r stays as it was.
    auto create_copy_locked(ievictable* r, std::uint8_t level) -> bool;
    /// The copy a read needs, created at once; the read may teach @p r its reuse gap.
    void create_on_demand_locked(ievictable* r, std::uint8_t level);
    /// A read needed memory a drop took from @p r: remember the gap between its reads, and count a
    /// thrash when the drop was within the minimum age.
    void note_read_after_drop_locked(ievictable* r);
    void note_dropped_locked(ievictable* r, std::uint64_t read_frame);

    void record_pass_locked(const residency::plan_result& plan, std::size_t scanned, double ms);
    auto held_bytes_locked() const -> std::uint64_t;
    void publish_budget_locked();
    auto snapshot_locked() const -> eviction::stats;

    mutable std::mutex mutex_;
    std::atomic<eviction::init_status> init_status_{eviction::init_status::unsupported};
    /// The thread that activated the registry; read lock-free by every native_handle.
    std::atomic<std::thread::id> api_thread_{};
    eviction::config default_config_{};
    eviction::residency_options options_{};
    /// options_.tail_size for readers that must not wait on the mutex.
    std::atomic<std::uint32_t> tail_size_{eviction::residency_options{}.tail_size};
    /// The tail size loads start with over the soft budget, for the load path.
    std::atomic<std::uint32_t> load_tail_size_{eviction::residency_options{}.tail_size};
    /// options_.paging_enabled for readers that must not wait on the mutex.
    std::atomic<bool> paging_enabled_{eviction::residency_options{}.paging_enabled};
    /// Bytes admitted for copies being created whose resources have not registered yet.
    std::atomic<std::uint64_t> admitted_bytes_{0};
    /// resident_bytes_ + reduced_bytes_ for the load path.
    std::atomic<std::uint64_t> held_bytes_published_{0};
    /// The created bytes of the frames in traffic_, for the load path. Counts a frame's creates here
    /// before they leave queued_allocation_bytes_, so a reader never misses them.
    std::atomic<std::uint64_t> recent_created_bytes_{0};
    std::uint32_t min_age_frames_{60};

    std::vector<ievictable*> resident_;
    std::vector<ievictable*> reduced_;
    std::vector<ievictable*> evicted_;
    /// The resources of the pass being planned, by candidate index.
    std::vector<ievictable*> pass_resources_;
    std::vector<residency::candidate> pass_candidates_;

    std::uint64_t resident_bytes_ = 0;
    std::uint64_t reduced_bytes_ = 0;
    std::uint64_t reduced_full_bytes_ = 0;
    std::uint64_t evicted_bytes_ = 0;

    /// GPU bytes queued since the last bgfx::frame/flush. Atomic because resources may be
    /// registered (or noted) from worker threads while the driver and reclaim_for peek it from
    /// the API thread. Cleared in @ref on_frame_submitted after a pump.
    std::atomic<std::uint64_t> queued_allocation_bytes_{0};
    std::atomic<std::uint64_t> external_queued_bytes_{0};
    std::atomic<std::uint64_t> pending_release_bytes_{0};
    std::array<frame_traffic, k_traffic_frames> traffic_{};
    std::size_t traffic_next_ = 0;

    std::uint64_t total_evictions_ = 0;
    std::uint64_t total_reductions_ = 0;
    std::uint64_t total_restores_ = 0;
    std::uint64_t total_raises_ = 0;
    std::uint64_t total_bytes_evicted_ = 0;
    std::uint64_t total_bytes_reduced_ = 0;
    std::uint64_t total_bytes_restored_ = 0;
    std::uint64_t total_bytes_raised_ = 0;
    std::uint64_t failed_restores_ = 0;
    std::uint64_t thrash_events_ = 0;

    /// The budget_ readers on any thread see, published as a seqlock over atomic fields: a reader
    /// retries while a publish runs and never reads a field while it is written.
    struct published_budget
    {
        std::atomic<std::uint32_t> seq{0};
        std::atomic<std::uint64_t> hard_limit_bytes{0};
        std::atomic<std::uint64_t> soft_budget_bytes{0};
        std::atomic<std::uint64_t> target_bytes{0};
        std::atomic<std::uint64_t> safety_margin_bytes{0};
        std::atomic<bool> counts_engine_bytes{false};

        /// Writes @p budget. One writer at a time: the registry's mutex.
        void store(const eviction::budget_state& budget);
        /// The last budget written, whole.
        auto load() const -> eviction::budget_state;
    };

    eviction::budget_state budget_{};
    published_budget published_budget_;
    std::uint64_t budget_used_bytes_ = 0;

    std::uint64_t last_pass_scanned_ = 0;
    std::uint64_t last_pass_evicted_ = 0;
    std::uint64_t last_pass_freed_bytes_ = 0;
    std::uint64_t last_pass_raised_ = 0;
    std::uint64_t last_pass_raised_bytes_ = 0;
    std::uint64_t raise_backlog_ = 0;
    double last_pass_ms_ = 0.0;
    double last_restore_ms_ = 0.0;
};

} // namespace gfx
