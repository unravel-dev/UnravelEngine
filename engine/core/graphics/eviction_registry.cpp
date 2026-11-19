#include "eviction_registry.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <limits>

namespace gfx
{
namespace
{
using registry_clock = std::chrono::steady_clock;

auto elapsed_ms(registry_clock::time_point start) -> double
{
    return std::chrono::duration<double, std::milli>(registry_clock::now() - start).count();
}

auto subtract_floor(std::uint64_t value, std::uint64_t amount) -> std::uint64_t
{
    return value > amount ? value - amount : 0;
}

auto state_for_level(std::uint8_t level) -> evict_state
{
    return level == 0 ? evict_state::resident : evict_state::reduced;
}

auto eviction_frame_now() -> std::uint64_t
{
    return detail::eviction_frame();
}
} // namespace

auto eviction_registry::instance() -> eviction_registry&
{
    static eviction_registry s_instance;
    return s_instance;
}

auto eviction_registry::get_init_status() const -> eviction::init_status
{
    return init_status_.load(std::memory_order_acquire);
}

auto eviction_registry::is_api_thread() const -> bool
{
    return get_init_status() == eviction::init_status::ok &&
           std::this_thread::get_id() == api_thread_.load(std::memory_order_acquire);
}

void eviction_registry::set_inactive(eviction::init_status status)
{
    std::lock_guard<std::mutex> lk(mutex_);
    init_status_.store(status == eviction::init_status::ok ? eviction::init_status::failed : status,
                       std::memory_order_release);
}

void eviction_registry::activate(const eviction::config& cfg,
                                 const eviction::budget_state& startup_budget,
                                 std::uint64_t used_bytes)
{
    std::lock_guard<std::mutex> lk(mutex_);
    api_thread_.store(std::this_thread::get_id(), std::memory_order_release);
    default_config_ = cfg;
    min_age_frames_ = cfg.min_age_frames;
    budget_ = startup_budget;
    budget_used_bytes_ = used_bytes;
    publish_budget_locked();
    init_status_.store(eviction::init_status::ok, std::memory_order_release);
}

void eviction_registry::shutdown()
{
    std::lock_guard<std::mutex> lk(mutex_);
    // A reduced texture keeps the copy it has and stops asking for more; nothing serves its reads now.
    for(auto* r : reduced_)
    {
        r->evict_state_.store(evict_state::resident, std::memory_order_relaxed);
        r->level_ = 0;
    }
    for(auto* bucket : {&resident_, &reduced_, &evicted_})
    {
        for(auto* r : *bucket)
        {
            r->evict_slot_ = UINT32_MAX;
        }
        bucket->clear();
    }
    pass_resources_.clear();
    pass_candidates_.clear();
    resident_bytes_ = 0;
    reduced_bytes_ = 0;
    reduced_full_bytes_ = 0;
    evicted_bytes_ = 0;
    queued_allocation_bytes_.store(0, std::memory_order_relaxed);
    external_queued_bytes_.store(0, std::memory_order_relaxed);
    pending_release_bytes_.store(0, std::memory_order_relaxed);
    traffic_ = {};
    traffic_next_ = 0;
    recent_created_bytes_.store(0, std::memory_order_relaxed);
    admitted_bytes_.store(0, std::memory_order_relaxed);
    publish_held_locked();
    budget_ = {};
    budget_used_bytes_ = 0;
    publish_budget_locked();
    init_status_.store(eviction::init_status::unsupported, std::memory_order_release);
}

auto eviction_registry::can_register_locked(const ievictable* r) const -> bool
{
    return r != nullptr && get_init_status() == eviction::init_status::ok && r->evict_slot_ == UINT32_MAX;
}

void eviction_registry::register_resource(ievictable* r, std::uint8_t level, std::uint64_t admitted_bytes)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(can_register_locked(r))
        {
            // A new resource counts as just used, read in full too, so the first sweep neither takes it as
            // idle forever nor takes a level before its first full read.
            r->last_use_frame_.store(eviction_frame_now(), std::memory_order_relaxed);
            r->last_full_use_frame_.store(eviction_frame_now(), std::memory_order_relaxed);
            r->drop_frame_ = 0;
            r->reuse_frames_ = 0;
            r->awaiting_read_ = false;
            place_locked(r, state_for_level(level), level);
            note_created_locked(r->accounted_bytes_);
        }
    }
    // After the copy counts as held and created, so a load judging usage meanwhile counts it twice, never not.
    release_admission(admitted_bytes);
}

void eviction_registry::register_evicted_resource(ievictable* r, std::uint64_t admitted_bytes)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(can_register_locked(r))
        {
            r->last_use_frame_.store(eviction_frame_now(), std::memory_order_relaxed);
            r->last_full_use_frame_.store(eviction_frame_now(), std::memory_order_relaxed);
            r->drop_frame_ = 0;
            r->reuse_frames_ = 0;
            r->awaiting_read_ = false;
            place_locked(r, evict_state::evicted, 0);
        }
    }
    release_admission(admitted_bytes);
}

auto eviction_registry::try_admit(std::uint64_t bytes, std::uint64_t used_bytes, std::uint64_t limit_bytes) -> bool
{
    std::uint64_t admitted = admitted_bytes_.load(std::memory_order_relaxed);
    for(;;)
    {
        if(used_bytes + admitted + bytes > limit_bytes)
        {
            return false;
        }
        if(admitted_bytes_.compare_exchange_weak(admitted, admitted + bytes, std::memory_order_acq_rel, std::memory_order_relaxed))
        {
            return true;
        }
    }
}

void eviction_registry::release_admission(std::uint64_t bytes)
{
    if(bytes == 0)
    {
        return;
    }
    // Saturating: a shutdown in between already dropped every admission.
    std::uint64_t admitted = admitted_bytes_.load(std::memory_order_relaxed);
    while(!admitted_bytes_.compare_exchange_weak(admitted,
                                                 subtract_floor(admitted, bytes),
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_relaxed))
    {
    }
}

auto eviction_registry::peek_admitted_bytes() const -> std::uint64_t
{
    return admitted_bytes_.load(std::memory_order_acquire);
}

auto eviction_registry::peek_held_bytes() const -> std::uint64_t
{
    return held_bytes_published_.load(std::memory_order_acquire);
}

auto eviction_registry::peek_recent_created_bytes() const -> std::uint64_t
{
    // The ring first: a frame's creates join it before they leave the queued count.
    const std::uint64_t ring = recent_created_bytes_.load(std::memory_order_acquire);
    return ring + queued_allocation_bytes_.load(std::memory_order_acquire);
}

void eviction_registry::unregister_resource(ievictable* r)
{
    if(r == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    if(r->evict_slot_ == UINT32_MAX)
    {
        return;
    }
    // The owner destroys its handle right after this; bgfx frees the memory a few frames later.
    if(r->get_evict_state() != evict_state::evicted)
    {
        note_released_locked(r->accounted_bytes_);
    }
    remove_locked(r);
    publish_held_locked();
}

void eviction_registry::request_residency(ievictable* r, texture_read read)
{
    if(r == nullptr || !is_api_thread())
    {
        return;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    if(get_init_status() != eviction::init_status::ok || r->evict_slot_ == UINT32_MAX ||
       !r->needs_residency_for(read))
    {
        return;
    }
    // A sampled read of an evicted texture needs only its tail; a full read needs every mip.
    const bool tail_will_do = read == texture_read::any_level && r->get_evict_state() == evict_state::evicted;
    create_on_demand_locked(r, tail_will_do ? get_tail_level_locked(*r) : std::uint8_t(0));
}

auto eviction_registry::evict(const eviction::config& cfg) -> eviction::stats
{
    std::lock_guard<std::mutex> lk(mutex_);
    if(get_init_status() != eviction::init_status::ok)
    {
        return snapshot_locked();
    }
    residency::request req;
    req.frame = eviction_frame_now();
    req.order = cfg.strat;
    req.min_age_frames = cfg.min_age_frames;
    req.max_idle_frames = cfg.max_idle_frames;
    req.max_drops = cfg.max_evictions;
    if(cfg.strat == eviction::strategy::age_ttl)
    {
        req.drop_bytes = std::numeric_limits<std::uint64_t>::max();
    }
    else
    {
        const std::uint64_t target = (cfg.target_bytes != 0) ? cfg.target_bytes : cfg.budget_bytes;
        req.drop_bytes = target == 0 ? 0 : subtract_floor(held_bytes_locked(), target);
    }
    if(req.drop_bytes == 0)
    {
        return snapshot_locked();
    }
    const auto start = registry_clock::now();
    const auto plan = run_plan_locked(req, plan_scope::all);
    apply_plan_locked(plan);
    record_pass_locked(plan, pass_candidates_.size(), elapsed_ms(start));
    return snapshot_locked();
}

auto eviction_registry::evict_default() -> eviction::stats
{
    return evict(snapshot_default_config());
}

auto eviction_registry::evict_bytes(std::uint64_t free_bytes,
                                    eviction::strategy strat,
                                    std::uint32_t min_age_frames,
                                    std::uint32_t max_evictions) -> eviction::stats
{
    std::lock_guard<std::mutex> lk(mutex_);
    if(get_init_status() != eviction::init_status::ok || free_bytes == 0)
    {
        return snapshot_locked();
    }
    residency::request req;
    req.frame = eviction_frame_now();
    req.order = strat;
    req.min_age_frames = min_age_frames;
    req.max_idle_frames = min_age_frames;
    req.max_drops = max_evictions;
    req.drop_bytes = free_bytes;
    const auto start = registry_clock::now();
    const auto plan = run_plan_locked(req, plan_scope::all);
    apply_plan_locked(plan);
    record_pass_locked(plan, pass_candidates_.size(), elapsed_ms(start));
    return snapshot_locked();
}

auto eviction_registry::evict_all() -> eviction::stats
{
    std::lock_guard<std::mutex> lk(mutex_);
    if(get_init_status() != eviction::init_status::ok)
    {
        return snapshot_locked();
    }
    residency::request req;
    req.frame = eviction_frame_now();
    req.order = eviction::strategy::largest_first;
    req.min_age_frames = 0;
    req.ignore_age = true;
    req.drop_bytes = std::numeric_limits<std::uint64_t>::max();
    const auto start = registry_clock::now();
    const auto plan = run_plan_locked(req, plan_scope::all);
    apply_plan_locked(plan);
    record_pass_locked(plan, pass_candidates_.size(), elapsed_ms(start));
    return snapshot_locked();
}

auto eviction_registry::restore_all() -> eviction::stats
{
    std::lock_guard<std::mutex> lk(mutex_);
    if(get_init_status() != eviction::init_status::ok)
    {
        return snapshot_locked();
    }
    // Copies: each change moves the resource to another bucket.
    const std::vector<ievictable*> evicted = evicted_;
    for(auto* r : evicted)
    {
        if(r->evict_slot_ != UINT32_MAX && r->get_evict_state() == evict_state::evicted && create_copy_locked(r, 0))
        {
            // Brought back without a read: the gap of the next read says nothing.
            r->awaiting_read_ = false;
        }
    }
    const std::vector<ievictable*> reduced = reduced_;
    for(auto* r : reduced)
    {
        if(r->evict_slot_ != UINT32_MAX && r->get_evict_state() == evict_state::reduced)
        {
            raise_locked(r, 0);
        }
    }
    return snapshot_locked();
}

auto eviction_registry::update_residency(const eviction::frame_request& request) -> eviction::stats
{
    std::lock_guard<std::mutex> lk(mutex_);
    if(get_init_status() != eviction::init_status::ok)
    {
        return snapshot_locked();
    }
    const auto start = registry_clock::now();
    min_age_frames_ = request.min_age_frames;

    residency::request req;
    req.frame = eviction_frame_now();
    req.order = request.strat;
    req.min_age_frames = request.min_age_frames;
    // Age TTL over budget drops everything idle for longer than the minimum age.
    req.max_idle_frames = request.min_age_frames;
    req.max_drops = request.max_drops;
    req.max_in_use_drops = request.max_in_use_drops;
    const bool is_over_budget = request.used_for_drops > request.soft_budget_bytes;
    req.drop_bytes = is_over_budget ? std::max<std::uint64_t>(1, subtract_floor(request.used_for_drops, request.target_bytes))
                                    : 0;
    req.drop_in_use = options_.reduce_in_use;
    req.rise_bytes = is_over_budget ? 0 : subtract_floor(request.target_bytes, request.used_for_rises);
    req.max_upload_bytes = options_.max_upload_bytes;
    req.max_rises = request.max_rises;

    // Only drops look at every resource; rises and the backlog need the reduced ones read lately.
    const auto plan = run_plan_locked(req, is_over_budget ? plan_scope::all : plan_scope::wanted_reduced);
    apply_plan_locked(plan);
    // Rises run once a frame, here: their numbers stand until the next frame pass.
    raise_backlog_ = plan.backlog;
    last_pass_raised_ = plan.rises;
    last_pass_raised_bytes_ = plan.raised_bytes;
    record_pass_locked(plan, pass_candidates_.size(), elapsed_ms(start));
    return snapshot_locked();
}

void eviction_registry::set_residency_options(const eviction::residency_options& options)
{
    std::lock_guard<std::mutex> lk(mutex_);
    options_ = options;
    tail_size_.store(options.tail_size, std::memory_order_relaxed);
    load_tail_size_.store(options.paging_enabled ? options.tail_size : 0, std::memory_order_relaxed);
    paging_enabled_.store(options.paging_enabled, std::memory_order_relaxed);
}

auto eviction_registry::get_load_tail_size() const -> std::uint32_t
{
    return load_tail_size_.load(std::memory_order_relaxed);
}

auto eviction_registry::is_paging_enabled() const -> bool
{
    return paging_enabled_.load(std::memory_order_relaxed);
}

auto eviction_registry::get_tail_size() const -> std::uint32_t
{
    return tail_size_.load(std::memory_order_relaxed);
}

auto eviction_registry::get_residency_options() const -> eviction::residency_options
{
    std::lock_guard<std::mutex> lk(mutex_);
    return options_;
}

void eviction_registry::set_budget(const eviction::budget_state& budget, std::uint64_t used_bytes)
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

auto eviction_registry::current_budget() const -> eviction::budget_state
{
    return published_budget_.load();
}

void eviction_registry::published_budget::store(const eviction::budget_state& budget)
{
    // A seqlock over atomic fields instead of one atomic struct, which would need libatomic on some
    // Linux builds: odd while writing, the fields, even again.
    const std::uint32_t start = seq.load(std::memory_order_relaxed);
    seq.store(start + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    hard_limit_bytes.store(budget.hard_limit_bytes, std::memory_order_relaxed);
    soft_budget_bytes.store(budget.soft_budget_bytes, std::memory_order_relaxed);
    target_bytes.store(budget.target_bytes, std::memory_order_relaxed);
    safety_margin_bytes.store(budget.safety_margin_bytes, std::memory_order_relaxed);
    counts_engine_bytes.store(budget.counts_engine_bytes, std::memory_order_relaxed);
    seq.store(start + 2, std::memory_order_release);
}

auto eviction_registry::published_budget::load() const -> eviction::budget_state
{
    for(;;)
    {
        const std::uint32_t start = seq.load(std::memory_order_acquire);
        if((start & 1u) != 0)
        {
            continue;
        }
        eviction::budget_state budget;
        budget.hard_limit_bytes = hard_limit_bytes.load(std::memory_order_relaxed);
        budget.soft_budget_bytes = soft_budget_bytes.load(std::memory_order_relaxed);
        budget.target_bytes = target_bytes.load(std::memory_order_relaxed);
        budget.safety_margin_bytes = safety_margin_bytes.load(std::memory_order_relaxed);
        budget.counts_engine_bytes = counts_engine_bytes.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if(seq.load(std::memory_order_relaxed) == start)
        {
            return budget;
        }
    }
}

auto eviction_registry::snapshot_default_config() const -> eviction::config
{
    std::lock_guard<std::mutex> lk(mutex_);
    return default_config_;
}

auto eviction_registry::get_stats() const -> eviction::stats
{
    std::lock_guard<std::mutex> lk(mutex_);
    return snapshot_locked();
}

void eviction_registry::note_pending_allocation(std::uint64_t bytes)
{
    if(bytes == 0 || get_init_status() != eviction::init_status::ok)
    {
        return;
    }
    // A worker thread creating a render target or compute-write texture: lock-free, so resource
    // creation never waits on the registry mutex.
    queued_allocation_bytes_.fetch_add(bytes, std::memory_order_relaxed);
    external_queued_bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

auto eviction_registry::peek_external_queued_bytes() const -> std::uint64_t
{
    return external_queued_bytes_.load(std::memory_order_relaxed);
}

auto eviction_registry::peek_pending_release_bytes() const -> std::uint64_t
{
    return pending_release_bytes_.load(std::memory_order_relaxed);
}

auto eviction_registry::peek_queued_bytes() const -> std::uint64_t
{
    return queued_allocation_bytes_.load(std::memory_order_relaxed);
}

auto eviction_registry::get_recent_traffic() const -> eviction::recent_traffic
{
    std::lock_guard<std::mutex> lk(mutex_);
    eviction::recent_traffic traffic;
    traffic.created = queued_allocation_bytes_.load(std::memory_order_relaxed);
    traffic.released = pending_release_bytes_.load(std::memory_order_relaxed);
    for(const auto& frame : traffic_)
    {
        traffic.created += frame.created;
        traffic.released += frame.released;
    }
    return traffic;
}

void eviction_registry::on_frame_submitted()
{
    // Both bgfx::frame and a mid-frame flush land here. The peaks the profiler reads roll over in
    // on_frame_advanced instead, so a flush does not erase them.
    // The creates join the recent window before they leave the queued count: a load judging usage in
    // between counts them twice, never not.
    const std::uint64_t created = queued_allocation_bytes_.load(std::memory_order_acquire);
    recent_created_bytes_.fetch_add(created, std::memory_order_acq_rel);
    queued_allocation_bytes_.fetch_sub(created, std::memory_order_acq_rel);
    const std::uint64_t released = pending_release_bytes_.exchange(0, std::memory_order_relaxed);
    external_queued_bytes_.store(0, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(mutex_);
    const std::uint64_t oldest_created = traffic_[traffic_next_].created;
    traffic_[traffic_next_] = {created, released};
    traffic_next_ = (traffic_next_ + 1) % traffic_.size();
    recent_created_bytes_.fetch_sub(oldest_created, std::memory_order_acq_rel);
}

void eviction_registry::on_frame_advanced()
{
    std::lock_guard<std::mutex> lk(mutex_);
    last_pass_ms_ = 0.0;
    last_restore_ms_ = 0.0;
    last_pass_scanned_ = 0;
    last_pass_evicted_ = 0;
    last_pass_freed_bytes_ = 0;
}

auto eviction_registry::bucket_for(evict_state state) -> std::vector<ievictable*>&
{
    switch(state)
    {
        case evict_state::reduced:
            return reduced_;
        case evict_state::evicted:
            return evicted_;
        case evict_state::resident:
            break;
    }
    return resident_;
}

void eviction_registry::place_locked(ievictable* r, evict_state state, std::uint8_t level)
{
    auto& bucket = bucket_for(state);
    r->evict_slot_ = static_cast<std::uint32_t>(bucket.size());
    bucket.push_back(r);
    r->evict_state_.store(state, std::memory_order_relaxed);
    r->level_ = state == evict_state::reduced ? level : std::uint8_t(0);
    r->accounted_full_bytes_ = r->gpu_size();
    r->accounted_bytes_ = state == evict_state::evicted ? 0 : r->gpu_size_at(r->level_);
    switch(state)
    {
        case evict_state::resident:
            resident_bytes_ += r->accounted_bytes_;
            break;
        case evict_state::reduced:
            reduced_bytes_ += r->accounted_bytes_;
            reduced_full_bytes_ += r->accounted_full_bytes_;
            break;
        case evict_state::evicted:
            evicted_bytes_ += r->accounted_full_bytes_;
            break;
    }
    publish_held_locked();
}

void eviction_registry::publish_held_locked()
{
    held_bytes_published_.store(resident_bytes_ + reduced_bytes_, std::memory_order_release);
}

void eviction_registry::remove_locked(ievictable* r)
{
    auto& bucket = bucket_for(r->get_evict_state());
    const std::uint32_t slot = r->evict_slot_;
    assert(slot < bucket.size() && bucket[slot] == r && "Evictable resource is not where the registry put it");
    ievictable* last = bucket.back();
    bucket[slot] = last;
    last->evict_slot_ = slot;
    bucket.pop_back();
    r->evict_slot_ = UINT32_MAX;
    switch(r->get_evict_state())
    {
        case evict_state::resident:
            resident_bytes_ -= r->accounted_bytes_;
            break;
        case evict_state::reduced:
            reduced_bytes_ -= r->accounted_bytes_;
            reduced_full_bytes_ -= r->accounted_full_bytes_;
            break;
        case evict_state::evicted:
            evicted_bytes_ -= r->accounted_full_bytes_;
            break;
    }
}

void eviction_registry::move_locked(ievictable* r, evict_state state, std::uint8_t level)
{
    remove_locked(r);
    place_locked(r, state, level);
}

void eviction_registry::note_created_locked(std::uint64_t bytes)
{
    if(bytes != 0)
    {
        queued_allocation_bytes_.fetch_add(bytes, std::memory_order_relaxed);
    }
}

void eviction_registry::note_released_locked(std::uint64_t bytes)
{
    if(bytes != 0)
    {
        pending_release_bytes_.fetch_add(bytes, std::memory_order_relaxed);
    }
}

auto eviction_registry::get_tail_level_locked(const ievictable& r) const -> std::uint8_t
{
    return options_.tail_size == 0 ? std::uint8_t(0) : r.get_tail_level(options_.tail_size);
}

auto eviction_registry::make_candidate_locked(const ievictable& r) const -> residency::candidate
{
    residency::candidate c;
    c.level = r.level_;
    c.tail_level = get_tail_level_locked(r);
    c.has_handle = r.get_evict_state() != evict_state::evicted;
    c.last_use = r.get_last_use_frame();
    c.last_full_use = r.get_last_full_use_frame();
    c.last_drop = r.drop_frame_;
    c.reuse_frames = r.reuse_frames_;
    c.use_count = r.get_use_count();
    const std::size_t top = std::min<std::size_t>(std::max(c.level, c.tail_level), residency::k_max_levels - 1);
    for(std::size_t level = 0; level <= top; ++level)
    {
        c.bytes[level] = r.gpu_size_at(static_cast<std::uint8_t>(level));
    }
    return c;
}

auto eviction_registry::run_plan_locked(const residency::request& req, plan_scope scope) -> residency::plan_result
{
    pass_resources_.clear();
    pass_candidates_.clear();
    const auto is_wanted = [&](const ievictable& r)
    {
        return req.frame <= r.get_last_use_frame() + req.wanted_frames;
    };
    const auto add_bucket = [&](const std::vector<ievictable*>& bucket)
    {
        for(auto* r : bucket)
        {
            if(r->get_evict_class() == evict_class::evictable && (scope == plan_scope::all || is_wanted(*r)))
            {
                pass_resources_.push_back(r);
                pass_candidates_.push_back(make_candidate_locked(*r));
            }
        }
    };
    if(scope == plan_scope::all)
    {
        add_bucket(resident_);
    }
    add_bucket(reduced_);
    return residency::plan(pass_candidates_, req);
}

void eviction_registry::apply_plan_locked(const residency::plan_result& plan)
{
    for(const auto& action : plan.actions)
    {
        ievictable* r = pass_resources_[action.index];
        if(r->evict_slot_ == UINT32_MAX || r->get_evict_state() == evict_state::evicted)
        {
            continue;
        }
        if(action.evict)
        {
            drop_whole_locked(r);
        }
        else if(action.level > r->level_)
        {
            drop_to_level_locked(r, action.level);
        }
        else if(action.level < r->level_)
        {
            raise_locked(r, action.level);
        }
    }
}

void eviction_registry::drop_whole_locked(ievictable* r)
{
    const std::uint64_t held = r->accounted_bytes_;
    r->on_evict();
    move_locked(r, evict_state::evicted, 0);
    note_released_locked(held);
    note_dropped_locked(r, r->get_last_use_frame());
    ++total_evictions_;
    total_bytes_evicted_ += held;
}

void eviction_registry::drop_to_level_locked(ievictable* r, std::uint8_t level)
{
    const std::uint64_t held = r->accounted_bytes_;
    if(!r->on_set_level(level))
    {
        ++failed_restores_;
        return;
    }
    move_locked(r, evict_state::reduced, level);
    note_created_locked(r->accounted_bytes_);
    note_released_locked(held);
    // Only a full read takes a reduced texture back, so its gap counts from the last full read.
    note_dropped_locked(r, r->get_last_full_use_frame());
    ++total_reductions_;
    total_bytes_reduced_ += subtract_floor(held, r->accounted_bytes_);
}

void eviction_registry::raise_locked(ievictable* r, std::uint8_t level)
{
    const std::uint64_t held = r->accounted_bytes_;
    if(!r->on_set_level(level))
    {
        ++failed_restores_;
        return;
    }
    move_locked(r, state_for_level(level), level);
    note_created_locked(r->accounted_bytes_);
    note_released_locked(held);
    // A planned rise answers the room, not a read: the gap of the next read says nothing.
    r->awaiting_read_ = false;
    ++total_raises_;
    total_bytes_raised_ += r->accounted_bytes_;
}

auto eviction_registry::create_copy_locked(ievictable* r, std::uint8_t level) -> bool
{
    const bool was_evicted = r->get_evict_state() == evict_state::evicted;
    const std::uint64_t held = r->accounted_bytes_;
    if(!r->on_set_level(level))
    {
        ++failed_restores_;
        return false;
    }
    move_locked(r, state_for_level(level), level);
    note_created_locked(r->accounted_bytes_);
    note_released_locked(held);
    if(was_evicted)
    {
        ++total_restores_;
        total_bytes_restored_ += r->accounted_bytes_;
    }
    else
    {
        ++total_raises_;
        total_bytes_raised_ += r->accounted_bytes_;
    }
    return true;
}

void eviction_registry::create_on_demand_locked(ievictable* r, std::uint8_t level)
{
    const auto start = registry_clock::now();
    const bool created = create_copy_locked(r, level);
    last_restore_ms_ = std::max(last_restore_ms_, elapsed_ms(start));
    if(created)
    {
        note_read_after_drop_locked(r);
    }
}

void eviction_registry::note_dropped_locked(ievictable* r, std::uint64_t read_frame)
{
    r->drop_frame_ = eviction_frame_now();
    r->drop_read_frame_ = read_frame;
    r->awaiting_read_ = true;
}

void eviction_registry::note_read_after_drop_locked(ievictable* r)
{
    if(!r->awaiting_read_)
    {
        return;
    }
    r->awaiting_read_ = false;
    const std::uint64_t now = eviction_frame_now();
    const std::uint64_t gap = subtract_floor(now, r->drop_read_frame_);
    r->reuse_frames_ = gap <= k_reuse_ages * min_age_frames_ ? gap : 0;
    if(subtract_floor(now, r->drop_frame_) < min_age_frames_)
    {
        ++thrash_events_;
    }
}

void eviction_registry::record_pass_locked(const residency::plan_result& plan, std::size_t scanned, double ms)
{
    // Per-frame peaks: max-of, not last-of, so a small fast sweep right before the profiler reads the
    // stats cannot mask a slow one earlier in the same frame. Reset in on_frame_advanced.
    last_pass_scanned_ = std::max<std::uint64_t>(last_pass_scanned_, scanned);
    // The drops and the bytes they freed come from the same pass: the one that freed the most.
    if(plan.dropped_bytes >= last_pass_freed_bytes_)
    {
        last_pass_evicted_ = plan.drops;
        last_pass_freed_bytes_ = plan.dropped_bytes;
    }
    last_pass_ms_ = std::max(last_pass_ms_, ms);
}

auto eviction_registry::held_bytes_locked() const -> std::uint64_t
{
    return resident_bytes_ + reduced_bytes_;
}

void eviction_registry::publish_budget_locked()
{
    published_budget_.store(budget_);
}

auto eviction_registry::snapshot_locked() const -> eviction::stats
{
    eviction::stats s;
    s.resident_count = resident_.size();
    s.resident_bytes = resident_bytes_;
    s.reduced_count = reduced_.size();
    s.reduced_bytes = reduced_bytes_;
    s.saved_bytes = subtract_floor(reduced_full_bytes_, reduced_bytes_);
    s.evicted_count = evicted_.size();
    s.evicted_bytes = evicted_bytes_;
    s.registered_count = resident_.size() + reduced_.size() + evicted_.size();
    s.total_evictions = total_evictions_;
    s.total_reductions = total_reductions_;
    s.total_restores = total_restores_;
    s.total_raises = total_raises_;
    s.total_bytes_evicted = total_bytes_evicted_;
    s.total_bytes_reduced = total_bytes_reduced_;
    s.total_bytes_restored = total_bytes_restored_;
    s.total_bytes_raised = total_bytes_raised_;
    s.failed_restores = failed_restores_;
    s.thrash_events = thrash_events_;
    // The soft budget is the "budget" tooling shows: the line above which the driver starts working.
    s.budget_bytes = budget_.soft_budget_bytes;
    s.target_bytes = budget_.target_bytes;
    s.budget_used_bytes = budget_used_bytes_;
    s.last_pass_scanned = last_pass_scanned_;
    s.last_pass_evicted = last_pass_evicted_;
    s.last_pass_freed_bytes = last_pass_freed_bytes_;
    s.last_pass_raised = last_pass_raised_;
    s.last_pass_raised_bytes = last_pass_raised_bytes_;
    s.raise_backlog = raise_backlog_;
    s.pending_release_bytes = pending_release_bytes_.load(std::memory_order_relaxed);
    s.last_pass_ms = last_pass_ms_;
    s.last_restore_ms = last_restore_ms_;
    return s;
}

} // namespace gfx
