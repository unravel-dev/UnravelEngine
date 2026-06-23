#include "eviction.h"

#include "graphics.h"

#include <algorithm>
#include <chrono>
#include <mutex>
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

    auto get_init_status() -> eviction::init_status
    {
        return init_status_;
    }

    auto init(const eviction::config& cfg) -> eviction::init_status
    {
        std::lock_guard<std::mutex> lk(mutex_);
        default_config_ = cfg;
        const auto* gpu_stats = gfx::get_stats();
        if(gpu_stats == nullptr)
        {
            init_status_ = eviction::init_status::failed;
            return eviction::init_status::failed;
        }
        init_status_ = gpu_stats->gpuMemoryMax > 0 ? eviction::init_status::ok : eviction::init_status::unsupported;

        // Direct3D11 and OpenGL do need eviction
        if(gfx::get_renderer_type() == gfx::renderer_type::Direct3D11 || gfx::get_renderer_type() == gfx::renderer_type::OpenGL)
        {
            init_status_ = eviction::init_status::unnecessary;
        }
        return init_status_;
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
        init_status_ = eviction::init_status::unsupported;
    }

    void register_resource(ievictable* r)
    {
        if(r == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lk(mutex_);
        if(init_status_ != eviction::init_status::ok)
        {
            return;
        }
        add_to(resident_, r);
        resident_bytes_ += r->gpu_size();
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
            resident_bytes_ -= r->gpu_size();
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
        std::lock_guard<std::mutex> lk(mutex_);
        if(init_status_ != eviction::init_status::ok || r->evict_slot_ == UINT32_MAX || r->get_evict_state() == evict_state::resident)
        {
            return;
        }
        restore_locked(r);
    }

    auto restore_all() -> eviction::stats
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(init_status_ != eviction::init_status::ok)
        {
            return snapshot();
        }
        const std::vector<ievictable*> pending = evicted_;
        for(auto* r : pending)
        {
            restore_locked(r);
        }
        return snapshot();
    }

    auto evict(const eviction::config& cfg) -> eviction::stats
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(init_status_ != eviction::init_status::ok)
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
        if(init_status_ != eviction::init_status::ok)
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
        if(init_status_ != eviction::init_status::ok)
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

    void report_budget(std::uint64_t used_bytes, std::uint64_t budget_bytes, std::uint64_t target_bytes)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(init_status_ != eviction::init_status::ok)
        {
            return;
        }
        budget_used_bytes_ = used_bytes;
        budget_bytes_ = budget_bytes;
        target_bytes_ = target_bytes;
    }

    auto get_stats() -> eviction::stats
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return snapshot();
    }

private:
    eviction_registry() = default;

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

    void restore_locked(ievictable* r)
    {
        const auto t0 = clock::now();
        const std::uint64_t sz = r->gpu_size();
        const bool ok = r->on_restore();
        last_restore_ms_ = to_ms(clock::now() - t0);
        if(!ok)
        {
            ++failed_restores_;
            return;
        }
        remove_from(evicted_, r);
        add_to(resident_, r);
        evicted_bytes_ -= sz;
        resident_bytes_ += sz;
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

        last_pass_scanned_ = candidates_.size();
        last_pass_evicted_ = pass_evicted;
        last_pass_ms_ = to_ms(clock::now() - t0);
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
        s.budget_bytes = budget_bytes_;
        s.target_bytes = target_bytes_;
        s.budget_used_bytes = budget_used_bytes_;
        s.last_pass_scanned = last_pass_scanned_;
        s.last_pass_evicted = last_pass_evicted_;
        s.last_pass_ms = last_pass_ms_;
        s.last_restore_ms = last_restore_ms_;
        return s;
    }

    std::mutex mutex_;
    eviction::init_status init_status_ = eviction::init_status::unsupported;
    eviction::config default_config_{};

    std::vector<ievictable*> resident_;
    std::vector<ievictable*> evicted_;
    std::vector<ievictable*> candidates_;

    std::uint64_t resident_bytes_ = 0;
    std::uint64_t evicted_bytes_ = 0;
    std::uint64_t total_evictions_ = 0;
    std::uint64_t total_restores_ = 0;
    std::uint64_t total_bytes_evicted_ = 0;
    std::uint64_t total_bytes_restored_ = 0;
    std::uint64_t failed_restores_ = 0;
    std::uint64_t thrash_events_ = 0;
    std::uint64_t budget_bytes_ = 0;
    std::uint64_t target_bytes_ = 0;
    std::uint64_t budget_used_bytes_ = 0;
    std::uint64_t last_pass_scanned_ = 0;
    std::uint64_t last_pass_evicted_ = 0;
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

auto restore_all() -> stats
{
    return eviction_registry::instance().restore_all();
}

void report_budget(std::uint64_t used_bytes, std::uint64_t budget_bytes, std::uint64_t target_bytes)
{
    eviction_registry::instance().report_budget(used_bytes, budget_bytes, target_bytes);
}

auto get_stats() -> stats
{
    return eviction_registry::instance().get_stats();
}

void set_frame(std::uint64_t frame)
{
    detail::g_eviction_frame = frame;
}

void advance_frame()
{
    ++detail::g_eviction_frame;
}

void register_resource(ievictable* resource)
{
    eviction_registry::instance().register_resource(resource);
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
