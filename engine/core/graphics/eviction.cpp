#include "eviction.h"

#include "graphics.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <type_traits>
#include <vector>

namespace gfx
{

namespace detail
{
std::uint64_t g_eviction_frame{0};
} // namespace detail

namespace
{
using clock = std::chrono::steady_clock;

auto to_ms(clock::duration d) -> double
{
    return std::chrono::duration<double, std::milli>(d).count();
}
} // namespace

/// Static registry of evictable GPU resources. Owns no resources; it only tracks raw pointers
/// whose lifetime is bounded by register/unregister calls made from handle_impl.
class eviction_registry
{
public:
    static auto instance() -> eviction_registry&
    {
        static eviction_registry s_instance;
        return s_instance;
    }

    auto get_init_status() const -> eviction::init_status
    {
        // Atomic so the hot-path is_supported() check is lock-free; the registry mutex still
        // guards the actual transitions in init/shutdown.
        return init_status_.load(std::memory_order_acquire);
    }

    auto init(const eviction::config& cfg) -> eviction::init_status
    {
        std::lock_guard<std::mutex> lk(mutex_);
        default_config_ = cfg;
        const auto* gpu_stats = gfx::get_stats();
        if(gpu_stats == nullptr)
        {
            set_init_status(eviction::init_status::failed);
            return eviction::init_status::failed;
        }
        auto status = gpu_stats->gpuMemoryMax > 0 ? eviction::init_status::ok : eviction::init_status::unsupported;

        // D3D11 and OpenGL drivers do their own resource paging behind the API, so a second
        // layer of eviction on top would just thrash. Vulkan/Metal/D3D12 surface explicit memory
        // budgets through bgfx and need the help.
        if(gfx::get_renderer_type() == gfx::renderer_type::Direct3D11 ||
           gfx::get_renderer_type() == gfx::renderer_type::OpenGL)
        {
            status = eviction::init_status::unnecessary;
        }
        set_init_status(status);
        if(status == eviction::init_status::ok)
        {
            seed_startup_budget_locked(gpu_stats);
        }
        return status;
    }
    void shutdown()
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for(auto* r : resident_)
        {
            r->evict_slot_ = UINT32_MAX;
        }
        for(auto* r : evicted_)
        {
            r->evict_slot_ = UINT32_MAX;
        }
        resident_.clear();
        evicted_.clear();
        resident_bytes_ = 0;
        evicted_bytes_ = 0;
        queued_allocation_bytes_.store(0, std::memory_order_relaxed);
        external_queued_bytes_.store(0, std::memory_order_relaxed);
        pending_release_bytes_.store(0, std::memory_order_relaxed);
        budget_ = {};
        budget_used_bytes_ = 0;
        set_init_status(eviction::init_status::unsupported);
    }

    void register_resource(ievictable* r)
    {
        if(r == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lk(mutex_);
        if(get_init_status() != eviction::init_status::ok)
        {
            return;
        }
        // Stamp the lifetime markers so the very first sweep treats the resource as freshly used
        // (rather than appearing infinitely idle because last_use_frame_ == 0). evict_frame_ gets
        // the same treatment so the thrash-detection window is honest from frame zero.
        const std::uint64_t frame = detail::g_eviction_frame;
        r->last_use_frame_ = frame;
        r->evict_frame_ = frame;
        add_to(resident_, r);
        const std::uint64_t sz = r->gpu_size();
        resident_bytes_ += sz;
        queued_allocation_bytes_.fetch_add(sz, std::memory_order_relaxed);
    }

    void register_evicted_resource(ievictable* r)
    {
        if(r == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lk(mutex_);
        if(get_init_status() != eviction::init_status::ok)
        {
            return;
        }
        const std::uint64_t frame = detail::g_eviction_frame;
        r->last_use_frame_ = frame;
        r->evict_frame_ = frame;
        add_to(evicted_, r);
        evicted_bytes_ += r->gpu_size();
    }

    void unregister_resource(ievictable* r)
    {
        if(r == nullptr || r->evict_slot_ == UINT32_MAX)
        {
            return;
        }
        std::lock_guard<std::mutex> lk(mutex_);
        if(r->get_evict_state() == evict_state::resident)
        {
            const std::uint64_t sz = r->gpu_size();
            resident_bytes_ -= sz;
            note_pending_release_locked(sz);
            remove_from(resident_, r);
        }
        else
        {
            evicted_bytes_ -= r->gpu_size();
            remove_from(evicted_, r);
        }
        r->evict_slot_ = UINT32_MAX;
    }

    void restore_resource(ievictable* r)
    {
        if(r == nullptr)
        {
            return;
        }
        std::unique_lock<std::mutex> lk(mutex_);
        if(get_init_status() != eviction::init_status::ok || r->evict_slot_ == UINT32_MAX ||
           r->get_evict_state() == evict_state::resident)
        {
            return;
        }
        restore_locked(lk, r);
    }

    auto restore_all() -> eviction::stats
    {
        std::unique_lock<std::mutex> lk(mutex_);
        if(get_init_status() != eviction::init_status::ok)
        {
            return snapshot();
        }
        // Copy the evicted set so a concurrent unregister (rare; the registry mutex serializes
        // it) cannot invalidate our iterator. restore_locked rebalances evicted_/resident_ in
        // place so iterating the original would be UB.
        const std::vector<ievictable*> pending = evicted_;
        for(auto* r : pending)
        {
            if(r->evict_slot_ != UINT32_MAX && r->get_evict_state() == evict_state::evicted)
            {
                restore_locked(lk, r);
            }
        }
        return snapshot();
    }

    auto evict(const eviction::config& cfg) -> eviction::stats
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(get_init_status() != eviction::init_status::ok)
        {
            return snapshot();
        }

        sweep_request req;
        req.strat = cfg.strat;
        req.min_age_frames = cfg.min_age_frames;
        req.max_idle_frames = cfg.max_idle_frames;
        req.max_evictions = cfg.max_evictions;

        if(cfg.strat == eviction::strategy::age_ttl)
        {
            req.target_resident = 0;
            req.free_limit = UINT64_MAX;
        }
        else
        {
            const std::uint64_t target = (cfg.target_bytes != 0) ? cfg.target_bytes : cfg.budget_bytes;
            if(target == 0)
            {
                return snapshot();
            }
            req.target_resident = target;
            req.free_limit = UINT64_MAX;
        }
        return do_sweep(req);
    }

    auto evict_bytes(std::uint64_t free_bytes,
                     eviction::strategy strat,
                     std::uint32_t min_age_frames,
                     std::uint32_t max_evictions) -> eviction::stats
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(get_init_status() != eviction::init_status::ok)
        {
            return snapshot();
        }
        sweep_request req;
        req.strat = strat;
        req.min_age_frames = min_age_frames;
        req.max_idle_frames = 0;
        req.max_evictions = max_evictions;
        req.target_resident = 0;
        req.free_limit = free_bytes;
        return do_sweep(req);
    }

    auto evict_all() -> eviction::stats
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(get_init_status() != eviction::init_status::ok)
        {
            return snapshot();
        }
        sweep_request req;
        req.strat = eviction::strategy::largest_first;
        req.min_age_frames = 0;
        req.max_idle_frames = 0;
        req.max_evictions = 0;
        req.target_resident = 0;
        req.free_limit = UINT64_MAX;
        return do_sweep(req);
    }

    auto evict_default() -> eviction::stats
    {
        eviction::config cfg;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            cfg = default_config_;
        }
        return evict(cfg);
    }

    void set_budget(const eviction::budget_state& budget, std::uint64_t used_bytes)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(get_init_status() != eviction::init_status::ok)
        {
            return;
        }
        budget_ = budget;
        budget_used_bytes_ = used_bytes;
        publish_budget_locked();
    }

    auto current_budget() const -> eviction::budget_state
    {
        // Seqlock publish: lock-free readers without a 32-byte std::atomic (needs libatomic on some Linux builds).
        for(;;)
        {
            const auto seq = published_budget_seq_.load(std::memory_order_acquire);
            if(seq & 1u)
            {
                continue;
            }
            const auto snapshot = published_budget_;
            if(published_budget_seq_.load(std::memory_order_acquire) == seq)
            {
                return snapshot;
            }
        }
    }

    auto snapshot_default_config() const -> eviction::config
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return default_config_;
    }

    auto get_stats() -> eviction::stats
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return snapshot();
    }

    void note_pending_allocation(std::uint64_t bytes)
    {
        if(bytes == 0 || get_init_status() != eviction::init_status::ok)
        {
            return;
        }
        // The hot path: a worker thread is registering a render target / compute-write texture.
        // Atomic and lock-free so we never block resource creation on the registry mutex.
        queued_allocation_bytes_.fetch_add(bytes, std::memory_order_relaxed);
        external_queued_bytes_.fetch_add(bytes, std::memory_order_relaxed);
    }

    auto peek_external_queued_bytes() const -> std::uint64_t
    {
        return external_queued_bytes_.load(std::memory_order_relaxed);
    }

    auto peek_pending_release_bytes() const -> std::uint64_t
    {
        return pending_release_bytes_.load(std::memory_order_relaxed);
    }

    auto peek_queued_bytes() const -> std::uint64_t
    {
        return queued_allocation_bytes_.load(std::memory_order_relaxed);
    }

    void clear_queued_allocations()
    {
        // Called whenever bgfx has processed the command queue (bgfx::frame OR a mid-frame
        // flush). Both clear the in-flight counter; only frame() rolls over the peak diagnostics
        // (see on_frame_advanced) so a coalescing flush mid-frame does not erase the worst-case
        // reading the profiler is about to surface.
        queued_allocation_bytes_.store(0, std::memory_order_relaxed);
        external_queued_bytes_.store(0, std::memory_order_relaxed);
        pending_release_bytes_.store(0, std::memory_order_relaxed);
    }

    void on_frame_advanced()
    {
        // Per-frame peak timers used by the profiler. Called from set_frame / advance_frame so
        // diagnostics reflect the entire frame up to the profiler read.
        last_pass_ms_ = 0.0;
        last_restore_ms_ = 0.0;
        last_pass_scanned_ = 0;
        last_pass_evicted_ = 0;
        last_pass_freed_bytes_ = 0;
    }

private:
    eviction_registry() = default;

    void set_init_status(eviction::init_status s)
    {
        init_status_.store(s, std::memory_order_release);
    }

    static auto startup_safety_margin(std::uint64_t hard_limit_bytes) -> std::uint64_t
    {
        constexpr std::uint64_t k_floor = std::uint64_t(64) * 1024 * 1024;
        return std::max(k_floor, hard_limit_bytes / 50);
    }

    void seed_startup_budget_locked(const gfx::stats* gpu_stats)
    {
        if(gpu_stats == nullptr || gpu_stats->gpuMemoryMax <= 0)
        {
            return;
        }
        const std::uint64_t gpu_max = static_cast<std::uint64_t>(gpu_stats->gpuMemoryMax);
        eviction::budget_state b;
        b.hard_limit_bytes = gpu_max;
        // Match default @ref unravel::eviction_settings fractions so reclaim_for works before the
        // first frame_begin publish.
        b.soft_budget_bytes = static_cast<std::uint64_t>(static_cast<double>(gpu_max) * 0.85);
        b.target_bytes = static_cast<std::uint64_t>(static_cast<double>(gpu_max) * 0.75);
        b.safety_margin_bytes = startup_safety_margin(gpu_max);
        budget_ = b;
        budget_used_bytes_ = static_cast<std::uint64_t>(std::max<std::int64_t>(0, gpu_stats->gpuMemoryUsed));
        publish_budget_locked();
    }

    void publish_budget_locked()
    {
        published_budget_seq_.fetch_add(1, std::memory_order_release);
        published_budget_ = budget_;
        published_budget_seq_.fetch_add(1, std::memory_order_release);
    }

    void note_pending_release_locked(std::uint64_t bytes)
    {
        if(bytes != 0)
        {
            pending_release_bytes_.fetch_add(bytes, std::memory_order_relaxed);
        }
    }

    static void add_to(std::vector<ievictable*>& bucket, ievictable* r)
    {
        r->evict_slot_ = static_cast<std::uint32_t>(bucket.size());
        bucket.push_back(r);
    }

    static void remove_from(std::vector<ievictable*>& bucket, ievictable* r)
    {
        const std::uint32_t idx = r->evict_slot_;
        ievictable* last = bucket.back();
        bucket[idx] = last;
        last->evict_slot_ = idx;
        bucket.pop_back();
    }

    /// Parameters shared by every eviction entry point. The sweep evicts ordered candidates until
    /// any stop condition is met.
    struct sweep_request
    {
        eviction::strategy strat = eviction::strategy::lru;
        std::uint32_t min_age_frames = 0; ///< Skip resources used within this many frames.
        std::uint32_t max_idle_frames = 0; ///< For age_ttl: only evict resources idle longer than this.
        std::uint32_t max_evictions = 0;   ///< Cap victims this pass (0 = unlimited).
        std::uint64_t target_resident = 0; ///< Stop once resident bytes drop to/below this.
        std::uint64_t free_limit = UINT64_MAX; ///< Stop once this many bytes have been reclaimed.
    };

    void restore_locked(std::unique_lock<std::mutex>& lk, ievictable* r)
    {
        const auto t0 = clock::now();
        const std::uint64_t sz = r->gpu_size();
        // on_restore may load GPU resources and call @ref reclaim_for — never hold the registry
        // mutex across that callback (reclaim re-locks for sweeps).
        lk.unlock();
        const bool ok = r->on_restore();
        lk.lock();
        last_restore_ms_ = std::max(last_restore_ms_, to_ms(clock::now() - t0));
        if(!ok)
        {
            ++failed_restores_;
            return;
        }
        if(r->evict_slot_ == UINT32_MAX || r->get_evict_state() != evict_state::resident)
        {
            return;
        }
        const std::uint32_t slot = r->evict_slot_;
        if(slot >= evicted_.size() || evicted_[slot] != r)
        {
            return;
        }
        remove_from(evicted_, r);
        add_to(resident_, r);
        evicted_bytes_ -= sz;
        resident_bytes_ += sz;
        queued_allocation_bytes_.fetch_add(sz, std::memory_order_relaxed);
        ++total_restores_;
        total_bytes_restored_ += sz;
        const std::uint64_t frame = detail::g_eviction_frame;
        if(frame - r->evict_frame_ < default_config_.min_age_frames)
        {
            ++thrash_events_;
        }
    }

    auto do_sweep(const sweep_request& req) -> eviction::stats
    {
        const auto t0 = clock::now();
        const std::uint64_t frame = detail::g_eviction_frame;

        candidates_.clear();
        collect_candidates(req, frame);
        order_candidates(req.strat);

        std::uint64_t pass_evicted = 0;
        std::uint64_t freed = 0;
        for(auto* r : candidates_)
        {
            if(req.max_evictions != 0 && pass_evicted >= req.max_evictions)
            {
                break;
            }
            if(resident_bytes_ <= req.target_resident || freed >= req.free_limit)
            {
                break;
            }
            const std::uint64_t sz = r->gpu_size();
            r->on_evict();
            note_pending_release_locked(sz);
            remove_from(resident_, r);
            add_to(evicted_, r);
            resident_bytes_ -= sz;
            evicted_bytes_ += sz;
            r->evict_frame_ = frame;
            ++pass_evicted;
            ++total_evictions_;
            total_bytes_evicted_ += sz;
            freed += sz;
        }

        // Per-frame peaks: max-of, not last-of, so a small fast sweep right before the profiler
        // reads the stats cannot mask a slow expensive sweep that happened earlier in the same
        // frame. Reset on bgfx::frame via on_frame_advanced.
        last_pass_scanned_ = std::max<std::uint64_t>(last_pass_scanned_, candidates_.size());
        last_pass_evicted_ = std::max<std::uint64_t>(last_pass_evicted_, pass_evicted);
        last_pass_freed_bytes_ = std::max<std::uint64_t>(last_pass_freed_bytes_, freed);
        last_pass_ms_ = std::max(last_pass_ms_, to_ms(clock::now() - t0));
        return snapshot();
    }

    void collect_candidates(const sweep_request& req, std::uint64_t frame)
    {
        for(auto* r : resident_)
        {
            if(r->get_evict_class() != evict_class::evictable)
            {
                continue;
            }
            const std::uint64_t idle = frame - r->get_last_use_frame();
            if(req.min_age_frames != 0 && idle < req.min_age_frames)
            {
                continue;
            }
            if(req.strat == eviction::strategy::age_ttl &&
               (req.max_idle_frames == 0 || idle <= req.max_idle_frames))
            {
                continue;
            }
            candidates_.push_back(r);
        }
    }

    void order_candidates(eviction::strategy strat)
    {
        switch(strat)
        {
            case eviction::strategy::lru:
            case eviction::strategy::age_ttl:
                std::sort(candidates_.begin(),
                          candidates_.end(),
                          [](const ievictable* a, const ievictable* b) -> bool
                          {
                              return a->get_last_use_frame() < b->get_last_use_frame();
                          });
                break;
            case eviction::strategy::lfu:
                std::sort(candidates_.begin(),
                          candidates_.end(),
                          [](const ievictable* a, const ievictable* b) -> bool
                          {
                              return a->get_use_count() < b->get_use_count();
                          });
                break;
            case eviction::strategy::largest_first:
                std::sort(candidates_.begin(),
                          candidates_.end(),
                          [](const ievictable* a, const ievictable* b) -> bool
                          {
                              return a->gpu_size() > b->gpu_size();
                          });
                break;
        }
    }

    auto snapshot() const -> eviction::stats
    {
        eviction::stats s;
        s.resident_count = resident_.size();
        s.resident_bytes = resident_bytes_;
        s.evicted_count = evicted_.size();
        s.evicted_bytes = evicted_bytes_;
        s.registered_count = resident_.size() + evicted_.size();
        s.total_evictions = total_evictions_;
        s.total_restores = total_restores_;
        s.total_bytes_evicted = total_bytes_evicted_;
        s.total_bytes_restored = total_bytes_restored_;
        s.failed_restores = failed_restores_;
        s.thrash_events = thrash_events_;
        // Expose the soft budget as the "budget" for tooling — it is the line above which the
        // driver starts working. Hard limit and safety margin are advisory and shown only via
        // current_budget() if needed by callers.
        s.budget_bytes = budget_.soft_budget_bytes;
        s.target_bytes = budget_.target_bytes;
        s.budget_used_bytes = budget_used_bytes_;
        s.last_pass_scanned = last_pass_scanned_;
        s.last_pass_evicted = last_pass_evicted_;
        s.last_pass_freed_bytes = last_pass_freed_bytes_;
        s.pending_release_bytes = pending_release_bytes_.load(std::memory_order_relaxed);
        s.last_pass_ms = last_pass_ms_;
        s.last_restore_ms = last_restore_ms_;
        return s;
    }

    mutable std::mutex mutex_;
    std::atomic<eviction::init_status> init_status_{eviction::init_status::unsupported};
    eviction::config default_config_{};

    std::vector<ievictable*> resident_;
    std::vector<ievictable*> evicted_;
    std::vector<ievictable*> candidates_;

    std::uint64_t resident_bytes_ = 0;
    std::uint64_t evicted_bytes_ = 0;
    /// GPU bytes queued since the last bgfx::frame/flush. Atomic because resources may be
    /// registered (or noted) from worker threads while the driver and reclaim_for peek it from
    /// the API thread. Cleared in @ref clear_queued_allocations after a pump.
    std::atomic<std::uint64_t> queued_allocation_bytes_{0};
    std::atomic<std::uint64_t> external_queued_bytes_{0};
    std::atomic<std::uint64_t> pending_release_bytes_{0};
    std::uint64_t total_evictions_ = 0;
    std::uint64_t total_restores_ = 0;
    std::uint64_t total_bytes_evicted_ = 0;
    std::uint64_t total_bytes_restored_ = 0;
    std::uint64_t failed_restores_ = 0;
    std::uint64_t thrash_events_ = 0;
    eviction::budget_state budget_{};
    std::atomic<std::uint32_t> published_budget_seq_{0};
    eviction::budget_state published_budget_{};
    std::uint64_t budget_used_bytes_ = 0;
    std::uint64_t last_pass_scanned_ = 0;
    std::uint64_t last_pass_evicted_ = 0;
    std::uint64_t last_pass_freed_bytes_ = 0;
    double last_pass_ms_ = 0.0;
    double last_restore_ms_ = 0.0;
};

namespace eviction
{

auto init(const config& cfg) -> init_status
{
    return eviction_registry::instance().init(cfg);
}

auto is_supported() -> bool
{
    return eviction_registry::instance().get_init_status() == eviction::init_status::ok;
}

void shutdown()
{
    if(debug_consumed_bytes() > 0)
    {
        debug_release_memory();
    }
    eviction_registry::instance().shutdown();
}

auto evict(const config& cfg) -> stats
{
    return eviction_registry::instance().evict(cfg);
}

auto evict() -> stats
{
    return eviction_registry::instance().evict_default();
}

auto evict_bytes(std::uint64_t free_bytes, strategy strat, std::uint32_t min_age_frames, std::uint32_t max_evictions)
    -> stats
{
    return eviction_registry::instance().evict_bytes(free_bytes, strat, min_age_frames, max_evictions);
}

auto evict_all() -> stats
{
    return eviction_registry::instance().evict_all();
}

namespace
{
auto live_gpu_used() -> std::uint64_t
{
    const auto* gpu_stats = gfx::get_stats();
    if(gpu_stats == nullptr)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, gpu_stats->gpuMemoryUsed));
}

auto credit_pending_release(std::uint64_t gross) -> std::uint64_t
{
    const std::uint64_t pending = eviction_registry::instance().peek_pending_release_bytes();
    return gross > pending ? gross - pending : 0;
}

auto project_occupancy(std::uint64_t used, std::uint64_t queued, std::uint64_t request, std::uint64_t margin)
    -> std::uint64_t
{
    return credit_pending_release(used + queued + request + margin);
}

auto projected_allocation_bytes(std::uint64_t bytes) -> std::uint64_t
{
    const budget_state budget = eviction_registry::instance().current_budget();
    if(budget.hard_limit_bytes == 0)
    {
        return 0;
    }
    return project_occupancy(live_gpu_used(),
                             eviction_registry::instance().peek_queued_bytes(),
                             bytes,
                             budget.safety_margin_bytes);
}
} // namespace

auto would_allocation_fit(std::uint64_t bytes) -> bool
{
    if(bytes == 0 || !is_supported())
    {
        return true;
    }
    const budget_state budget = eviction_registry::instance().current_budget();
    if(budget.hard_limit_bytes == 0)
    {
        return true;
    }
    return projected_allocation_bytes(bytes) <= budget.hard_limit_bytes;
}

auto reclaim_for(std::uint64_t bytes, reclaim_kind kind) -> reclaim_result
{
    if(bytes == 0 || !is_supported())
    {
        return reclaim_result::headroom;
    }
    const budget_state budget = eviction_registry::instance().current_budget();
    if(budget.hard_limit_bytes == 0)
    {
        return reclaim_result::headroom;
    }

    if(kind == reclaim_kind::evictable)
    {
        return would_allocation_fit(bytes) ? reclaim_result::headroom : reclaim_result::insufficient;
    }

    const std::uint64_t projected = projected_allocation_bytes(bytes);
    if(projected <= budget.soft_budget_bytes)
    {
        return reclaim_result::headroom;
    }

    const eviction::config cfg = eviction_registry::instance().snapshot_default_config();
    const std::uint64_t deficit = projected > budget.target_bytes ? projected - budget.target_bytes : 0;
    const stats sweep =
        evict_bytes(deficit, cfg.strat, cfg.min_age_frames, cfg.max_evictions);
    const std::uint64_t freed = sweep.last_pass_freed_bytes;

    auto after_evict = [&]() -> std::uint64_t
    {
        return projected_allocation_bytes(bytes);
    };

    std::uint64_t occupancy = after_evict();

    // immediate: evicted destroys must land on the GPU before the imminent allocation.
    if(bytes > 0 && freed > 0)
    {
        gfx::frames(1, BGFX_FRAME_FLUSH);
        occupancy = after_evict();
        return occupancy > budget.hard_limit_bytes ? reclaim_result::insufficient : reclaim_result::reclaimed;
    }

    if(occupancy <= budget.hard_limit_bytes)
    {
        return reclaim_result::reclaimed;
    }

    gfx::frames(1, BGFX_FRAME_FLUSH);
    occupancy = after_evict();
    return occupancy > budget.hard_limit_bytes ? reclaim_result::insufficient : reclaim_result::reclaimed;
}

auto peek_queued_bytes() -> std::uint64_t
{
    return eviction_registry::instance().peek_queued_bytes();
}

auto peek_pending_release_bytes() -> std::uint64_t
{
    return eviction_registry::instance().peek_pending_release_bytes();
}

auto peek_external_queued_bytes() -> std::uint64_t
{
    return eviction_registry::instance().peek_external_queued_bytes();
}

void note_pending_allocation(std::uint64_t bytes)
{
    eviction_registry::instance().note_pending_allocation(bytes);
}

void clear_queued_allocations()
{
    eviction_registry::instance().clear_queued_allocations();
}

namespace
{
/// Test-only reserved memory: raw, non-evictable GPU textures used to inflate device occupancy. Held
/// outside the registry so the eviction system treats them as immovable "real" usage. API-thread only.
struct debug_reserve
{
    std::vector<gfx::texture_handle> chunks;
    std::uint64_t bytes = 0;
};

auto reserved() -> debug_reserve&
{
    static debug_reserve s_reserved;
    return s_reserved;
}

constexpr std::uint16_t k_reserve_dim = 4096;                                          // 4096x4096 RGBA8
constexpr std::uint64_t k_reserve_chunk = std::uint64_t(k_reserve_dim) * k_reserve_dim * 4; // == 64 MiB

/// Allocate one uninitialized square RGBA8 texture of @p dim and track it. Returns its nominal byte
/// size, or 0 if the allocation failed (e.g. genuinely out of memory).
auto alloc_reserve_chunk(std::uint16_t dim) -> std::uint64_t
{
    const gfx::texture_handle handle =
        gfx::create_texture_2d(dim, dim, false, 1, gfx::texture_format::RGBA8, BGFX_TEXTURE_NONE, nullptr);
    if(!bgfx::isValid(handle))
    {
        return 0;
    }
    reserved().chunks.push_back(handle);
    const std::uint64_t sz = std::uint64_t(dim) * dim * 4;
    reserved().bytes += sz;
    return sz;
}
} // namespace

auto debug_consume_memory(std::uint64_t bytes) -> std::uint64_t
{
    std::uint64_t remaining = bytes;
    while(remaining >= k_reserve_chunk)
    {
        const std::uint64_t got = alloc_reserve_chunk(k_reserve_dim);
        if(got == 0)
        {
            return reserved().bytes; // allocation failed - stop reserving
        }
        remaining -= got;
    }
    if(remaining > 0)
    {
        // Smallest square RGBA8 texture that covers the remainder.
        const double texels = static_cast<double>(remaining) / 4.0;
        auto side = static_cast<std::uint32_t>(std::ceil(std::sqrt(texels)));
        side = std::clamp<std::uint32_t>(side, 1, k_reserve_dim);
        alloc_reserve_chunk(static_cast<std::uint16_t>(side));
    }
    return reserved().bytes;
}

auto debug_simulate_budget(std::uint64_t target_free_bytes) -> std::uint64_t
{
    const auto* gpu_stats = gfx::get_stats();
    if(gpu_stats == nullptr || gpu_stats->gpuMemoryMax <= 0)
    {
        return debug_consumed_bytes();
    }
    // Absolute target: start from a clean slate, then let the released VRAM settle so the usage we
    // read back reflects only the real (non-reserved) consumers.
    if(debug_consumed_bytes() > 0)
    {
        debug_release_memory();
    }
    gpu_stats = gfx::get_stats();
    const auto budget = static_cast<std::uint64_t>(gpu_stats->gpuMemoryMax);
    const auto used = static_cast<std::uint64_t>(std::max<std::int64_t>(0, gpu_stats->gpuMemoryUsed));
    if(target_free_bytes >= budget)
    {
        return debug_consumed_bytes();
    }
    const std::uint64_t desired_used = budget - target_free_bytes;
    if(desired_used <= used)
    {
        return debug_consumed_bytes(); // less is already free than the requested target
    }
    return debug_consume_memory(desired_used - used);
}

void debug_release_memory()
{
    for(const auto handle : reserved().chunks)
    {
        if(bgfx::isValid(handle))
        {
            gfx::destroy(handle);
        }
    }
    reserved().chunks.clear();
    reserved().bytes = 0;
    // Pump the command buffer so the destroys are serviced and their VRAM is reclaimed before return.
    gfx::frames(1, BGFX_FRAME_FLUSH);
}

auto debug_consumed_bytes() -> std::uint64_t
{
    return reserved().bytes;
}

auto restore_all() -> stats
{
    auto& reg = eviction_registry::instance();
    if(!is_supported())
    {
        return reg.get_stats();
    }
    // Pre-flight reclaim: ensure there is room for the full evicted pool before we start
    // recreating handles. Doing it once up-front (rather than once per restore) avoids holding
    // the registry mutex across reclaim_for, which would deadlock since reclaim_for re-locks via
    // evict_bytes. We accept the slight over-estimate (queued allocations are double-counted by
    // gpu_used as bgfx catches up); reclaim_for treats that as headroom unless we are truly tight.
    const auto pre = reg.get_stats();
    if(pre.evicted_bytes != 0)
    {
        (void)reclaim_for(pre.evicted_bytes, reclaim_kind::immediate);
    }
    return reg.restore_all();
}

void set_budget(const budget_state& budget, std::uint64_t used_bytes)
{
    eviction_registry::instance().set_budget(budget, used_bytes);
}

auto current_budget() -> budget_state
{
    return eviction_registry::instance().current_budget();
}

auto get_stats() -> stats
{
    return eviction_registry::instance().get_stats();
}

void set_frame(std::uint64_t frame)
{
    detail::g_eviction_frame = frame;
    // New frame: reset the per-frame peak diagnostics so the profiler sees this frame's worst
    // case, not a value carried over from previous frames.
    eviction_registry::instance().on_frame_advanced();
}

void advance_frame()
{
    ++detail::g_eviction_frame;
    eviction_registry::instance().on_frame_advanced();
}

void register_resource(ievictable* resource)
{
    eviction_registry::instance().register_resource(resource);
}

void register_evicted_resource(ievictable* resource)
{
    eviction_registry::instance().register_evicted_resource(resource);
}

void unregister_resource(ievictable* resource)
{
    eviction_registry::instance().unregister_resource(resource);
}

void restore_resource(ievictable* resource)
{
    eviction_registry::instance().restore_resource(resource);
}

} // namespace eviction
} // namespace gfx
