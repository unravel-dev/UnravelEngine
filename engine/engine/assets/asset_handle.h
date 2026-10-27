#pragma once

#include <engine/assets/asset_epoch.h>
#include <engine/engine_export.h>
#include <engine/profiler/profiler.h>
#include <hpp/filesystem.hpp>
#include <logging/logging.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <uuid/uuid.h>

#include "../threading/threader.h"

template<typename T>
using task_future = tpp::job_shared_future<T>;

template<typename T>
struct asset_handle;

namespace asset_detail
{

/// Accesses closer together than this share one last-access stamp (nanoseconds). Idle eviction
/// measures idleness in seconds, so the stamp loses nothing it needs.
inline constexpr int64_t LAST_ACCESS_RESOLUTION_NS = 1'000'000;

} // namespace asset_detail

/**
 * @struct asset_link
 * @brief Thread-safe link to an asset: what every copy of one asset handle shares.
 *
 * The link's state (uid, id, load task) is an immutable `state_t` that writers replace whole. Readers
 * load it with one atomic pointer read inside an epoch guard (asset_detail::epoch_domain) and write
 * nothing the other readers read, so any number of threads read one link without contending. A
 * replaced state is retired to the epoch domain, which frees it once no reader can still reach it.
 *
 * The asset a get() resolved under a state lives in that state (`resolved`), so it is dropped with
 * the state and can never outlive a reload. The last-access stamp sits outside the state and is only
 * rewritten once it is older than its resolution.
 */
template<typename T>
struct asset_link
{
    using task_future_t = task_future<std::shared_ptr<T>>;

    /**
     * @brief One published state of the link. Immutable once published, except `resolved`, which the
     * first get() or try_get() that finds the task's value sets.
     */
    struct state_t
    {
        state_t() = default;
        state_t(const hpp::uuid& state_uid, std::string state_id, task_future_t state_task)
            : uid(state_uid)
            , id(std::move(state_id))
            , task(std::move(state_task))
        {
        }
        state_t(const state_t&) = delete;
        auto operator=(const state_t&) -> state_t& = delete;

        /// Unique identifier for the asset.
        hpp::uuid uid;
        /// String identifier for the asset.
        std::string id;
        /// Task future for the asset (valid for both scheduled and deferred jobs).
        task_future_t task;
        /// The task's value once a get() or try_get() under this state found it: a pointer into the
        /// task's shared state, which this state keeps alive, so it is valid as long as the state.
        mutable std::atomic<const std::shared_ptr<T>*> resolved{nullptr};
    };

    asset_link() = default;
    ~asset_link()
    {
        // No reader can be inside: every reader holds a handle, and so a reference to this link.
        delete state.load(std::memory_order_relaxed);
    }
    asset_link(const asset_link&) = delete;
    auto operator=(const asset_link&) -> asset_link& = delete;

    /// The current state. Readers load it inside an epoch guard; only asset_handle::publish replaces it.
    std::atomic<state_t*> state{new state_t()};

    /// Whether the current state holds a task, so is_valid() needs no guard.
    std::atomic<bool> has_task{false};

    /// Serializes writers: each copies the current state, changes it and publishes the copy.
    std::mutex write_mutex;

    /// When a handle last read the asset, in steady-clock nanoseconds (update_last_access). Idle eviction reads it.
    mutable std::atomic<int64_t> last_access_ns{0};
};

/**
 * @struct asset_handle
 * @brief Thread-safe handle to an asset.
 *
 * The handle is a cheap value (shared_ptr to the link). All accessors are
 * safe to call concurrently from any thread; mutators publish their changes
 * atomically so other threads always observe a consistent state.
 */
template<typename T>
struct asset_handle
{
    using asset_link_t = asset_link<T>;
    using state_t = typename asset_link_t::state_t;
    using task_future_t = typename asset_link_t::task_future_t;

    /**
     * @brief Equality operator for asset handles.
     */
    auto operator==(const asset_handle& rhs) const -> bool
    {
        if(is_valid() != rhs.is_valid())
        {
            return false;
        }
        asset_detail::epoch_domain::guard guard;
        const state_t* lhs_state = link_ ? load_state() : nullptr;
        const state_t* rhs_state = rhs.link_ ? rhs.load_state() : nullptr;
        static const hpp::uuid no_uid{};
        static const std::string no_id{};
        return (lhs_state ? lhs_state->uid : no_uid) == (rhs_state ? rhs_state->uid : no_uid) &&
               (lhs_state ? lhs_state->id : no_id) == (rhs_state ? rhs_state->id : no_id);
    }

    auto version() const -> uintptr_t
    {
        update_last_access();
        if(!link_)
        {
            return 0;
        }
        asset_detail::epoch_domain::guard guard;
        const auto* resolved = load_state()->resolved.load(std::memory_order_acquire);
        return resolved != nullptr ? uintptr_t(resolved->get()) : 0;
    }

    /**
     * @brief Conversion operator to bool.
     * @return True if the handle references an asset (loaded or deferred).
     */
    operator bool() const
    {
        return is_valid();
    }

    /**
     * @brief Gets the string identifier of the asset.
     *
     * Returns by value - the caller receives a snapshot of the current id.
     * Binding to `const auto&` is still safe (the temporary's lifetime is
     * extended to the binding's scope).
     */
    auto id() const -> std::string
    {
        if(!link_)
        {
            return {};
        }
        asset_detail::epoch_domain::guard guard;
        return load_state()->id;
    }

    /**
     * @brief Gets the unique identifier of the asset.
     *
     * Returns by value - the caller receives a snapshot of the current uid.
     * Binding to `const auto&` is still safe (the temporary's lifetime is
     * extended to the binding's scope).
     */
    auto uid() const -> hpp::uuid
    {
        if(!link_)
        {
            return {};
        }
        asset_detail::epoch_domain::guard guard;
        return load_state()->uid;
    }

    /**
     * @brief Gets the name of the asset derived from its path.
     */
    auto name() const -> std::string
    {
        return fs::path(id()).stem().string();
    }

    auto extension() const -> std::string
    {
        return fs::path(id()).extension().string();
    }

    /**
     * @brief Gets the shared pointer to the asset.
     * @param wait If true, waits for the task to complete if not ready.
     *
     * If the handle was registered with deferred loading, the first call to
     * get() submits the load to the thread pool; every call then waits for it
     * or polls it, as @p wait says.
     */
    auto get(bool wait = true) const -> std::shared_ptr<T>
    {
        return resolve(wait, true);
    }

    /**
     * @brief The asset if it is loaded, without waiting: null while it is still on its way (a deferred load is
     * submitted, as get(false) submits it) or when the handle references no asset; the empty asset when its load
     * failed, as get() returns it.
     *
     * The readiness test for code that must not wait: it touches the asset, so an idle one is not demoted to deferred,
     * and submits the load that is_ready() alone never starts. get(false) returns the empty asset both while loading
     * and after a failed load, and is_ready() read after it can see a load that finished in between; this decides
     * from one snapshot.
     */
    auto try_get(bool wait = false) const -> std::shared_ptr<T>
    {
        return resolve(wait, false);
    }

    /**
     * @brief Calls `visitor(T*)` with the asset try_get(wait) would return - null without a task (or, without
     * waiting, while it loads), the empty asset when its load failed - without taking a reference to it, and returns
     * what `visitor` returns (as a value). Like try_get(), it touches the access stamp and submits a deferred load;
     * with `wait` it waits for a load still running, outside any guard, and then visits what it loaded.
     *
     * For per-frame paths that read one asset from many threads: a get() counts a reference on the asset's one
     * control block, which every thread reading it contends on; a visit writes nothing other threads read.
     *
     * The pointer is valid only inside the call. A reload or unload on another thread replaces the asset for later
     * calls, but the object `visitor` received stays alive until it returns. It is not guarded against edits of the
     * asset object itself, as an asset from get() is not. Inside `visitor`, never let the pointer out (keep get()'s
     * shared_ptr instead) and never wait: no get(), and no try_get() or visit() with `wait` - a wait inside the visit
     * holds back every free that is due. The other handle calls are fine.
     */
    template<typename F>
    auto visit(F&& visitor, bool wait = false) const
    {
        update_last_access();
        task_future_t waited;
        {
            asset_detail::epoch_domain::guard guard;
            const state_t* state = link_ ? load_state() : nullptr;
            const std::shared_ptr<T>* found = state != nullptr ? find_loaded(*state) : nullptr;
            if(found != nullptr || !wait || state == nullptr || !state->task.valid())
            {
                return std::forward<F>(visitor)(found != nullptr ? found->get() : nullptr);
            }
            waited = state->task;
        }
        // Still loading: waited for outside the guard; the task copy keeps what it loaded alive.
        return std::forward<F>(visitor)(wait_for_load(waited).get());
    }

    /**
     * @brief True for the shared empty asset that get() and try_get() return when a load failed or
     * the handle references no asset - the one way to tell a failed load from a finished one.
     */
    static auto is_empty(const std::shared_ptr<T>& asset) -> bool
    {
        return asset && asset == empty_asset();
    }

    /**
     * @brief True for the shared empty asset as visit() passes it.
     */
    static auto is_empty(const T* asset) -> bool
    {
        return asset != nullptr && asset == empty_asset().get();
    }

    /**
     * @brief Checks if the handle references a task.
     */
    auto is_valid() const -> bool
    {
        // The flag published with the state, see asset_link::has_task.
        return link_ && link_->has_task.load(std::memory_order_acquire);
    }

    /**
     * @brief Checks if the task is ready.
     */
    auto is_ready() const -> bool
    {
        if(!link_)
        {
            return false;
        }
        asset_detail::epoch_domain::guard guard;
        const auto& task = load_state()->task;
        return task.valid() && task.is_ready();
    }

    /**
     * @brief Checks if the handle has a deferred job not yet submitted to workers.
     */
    auto is_deferred() const -> bool
    {
        if(!link_)
        {
            return false;
        }
        asset_detail::epoch_domain::guard guard;
        const auto& task = load_state()->task;
        return task.valid() && !task.is_submitted();
    }

    /**
     * @brief Checks if the asset is currently loading.
     */
    auto is_loading() const -> bool
    {
        if(!link_)
        {
            return false;
        }
        asset_detail::epoch_domain::guard guard;
        const auto& task = load_state()->task;
        return task.valid() && task.is_submitted() && !task.is_ready();
    }

    /**
     * @brief Submits a deferred task to the thread pool for execution.
     * Does nothing if the task is already submitted or invalid.
     */
    void submit()
    {
        if(!link_)
        {
            return;
        }
        asset_detail::epoch_domain::guard guard;
        const auto& task = load_state()->task;
        if(task.valid() && !task.is_submitted())
        {
            task.submit();
        }
    }

    /**
     * @brief Demotes a fully loaded asset back to deferred state.
     *
     * Publishes a new state that preserves uid / id but drops the task and the
     * resolved asset. Caller should follow with a load_from_file(deferred) to
     * set up a new deferred task.
     */
    void demote_to_deferred()
    {
        if(!link_)
        {
            return;
        }
        publish(
            [](const state_t& current)
            {
                return new state_t(current.uid, current.id, {});
            },
            false);
    }

    /**
     * @brief Gets the last access timestamp.
     */
    auto last_access() const -> std::chrono::steady_clock::time_point
    {
        if(!link_)
        {
            return {};
        }
        const auto ns = link_->last_access_ns.load(std::memory_order_relaxed);
        return std::chrono::steady_clock::time_point(std::chrono::nanoseconds(ns));
    }

    /**
     * @brief Gets the task ID (may be empty if there's no task).
     */
    auto task_id() const
    {
        if(link_)
        {
            asset_detail::epoch_domain::guard guard;
            if(const auto& task = load_state()->task; task.valid())
            {
                return task.id;
            }
        }
        return tpp::job_id{};
    }

    /**
     * @brief Sets the internal job future.
     *
     * Publishes a new state with the given task; preserves uid + id. The asset
     * resolved under the old task goes with it, so the next get() resolves the
     * new task.
     */
    void set_internal_job(const task_future_t& future)
    {
        ensure();
        publish(
            [&](const state_t& current)
            {
                return new state_t(current.uid, current.id, future);
            },
            false);
    }

    /**
     * @brief Sets the internal IDs (uid + string identifier).
     *
     * Publishes a new state; preserves the current task and the asset it resolved.
     */
    void set_internal_ids(const hpp::uuid& internal_uid, const std::string& internal_id = get_empty_id())
    {
        ensure();
        publish(
            [&](const state_t& current)
            {
                return new state_t(internal_uid, internal_id, current.task);
            },
            true);
    }

    /**
     * @brief Sets the internal string identifier.
     *
     * Publishes a new state; preserves uid, the task and the asset it resolved.
     */
    void set_internal_id(const std::string& internal_id = get_empty_id())
    {
        ensure();
        publish(
            [&](const state_t& current)
            {
                return new state_t(current.uid, internal_id, current.task);
            },
            true);
    }

    /**
     * @brief Invalidates the handle, resetting its state.
     *
     * Publishes an empty state so any concurrent reader observing the
     * link after this call sees a fully-cleared state.
     */
    void invalidate()
    {
        if(!link_)
        {
            return;
        }
        // Copied out of the guard: the log can wait on its sink, and a guard must not wait.
        std::string leaked_id;
        long task_count = 0;
        {
            asset_detail::epoch_domain::guard guard;
            const auto* state = load_state();
            task_count = state->task.valid() ? state->task.use_count() : 0;
            if(task_count > 1)
            {
                leaked_id = state->id;
            }
        }
        if(task_count > 1)
        {
            APPLOG_TRACE("{} - task leak use_count {}", leaked_id, task_count);
        }
        publish(
            [](const state_t&)
            {
                return new state_t();
            },
            false);
    }

    /**
     * @brief Gets an empty asset handle.
     */
    static auto get_empty() -> const asset_handle&
    {
        static const asset_handle none_asset = []() -> asset_handle
        {
            asset_handle asset;
            asset.set_internal_ids({});
            return asset;
        }();
        return none_asset;
    }

    /**
     * @brief Gets the conventional empty string identifier ("None").
     */
    static auto get_empty_id() -> const std::string&
    {
        static const std::string empty{"None"};
        return empty;
    }

    /**
     * @brief Returns the loaded asset if it is currently in memory, or
     * nullptr otherwise.
     *
     * Unlike `get()`, this does NOT force-load, does NOT submit deferred
     * tasks, does NOT wait, and does NOT update last-access. Intended for
     * iteration use cases - e.g. walking the asset manager's loaded set to
     * build a dependency graph - where we only care about what is already
     * resident and must not have side effects.
     */
    auto peek() const -> std::shared_ptr<T>
    {
        if(!link_)
        {
            return nullptr;
        }
        asset_detail::epoch_domain::guard guard;
        const auto* resolved = load_state()->resolved.load(std::memory_order_acquire);
        return resolved != nullptr ? *resolved : nullptr;
    }

    /**
     * @brief Ensures the asset link is initialized.
     */
    void ensure()
    {
        static_assert(sizeof(asset_link_t) >= 1, "Type must be fully defined");
        if(!link_)
        {
            link_ = std::make_shared<asset_link_t>();
        }
    }

private:
    /**
     * @brief The body of get() (`is_get`) and try_get(): the asset, waited for when @p wait (a deferred load is
     * submitted first); without a task or, without waiting, while the asset still loads, the empty asset for get()
     * and null for try_get(); the empty asset when its load failed.
     */
    auto resolve(bool wait, bool is_get) const -> std::shared_ptr<T>
    {
        if(!link_)
        {
            return unresolved(is_get);
        }
        update_last_access();
        task_future_t waited;
        {
            asset_detail::epoch_domain::guard guard;
            const auto* state = load_state();
            if(const auto* found = find_loaded(*state))
            {
                return *found;
            }
            if(!wait || !state->task.valid())
            {
                return unresolved(is_get);
            }
            waited = state->task;
        }
        return wait_for_load(waited);
    }

    /**
     * @brief Waits for `task` - the load of a state, copied inside a guard - and records its value as that state's
     * resolved asset if the state is still the current one. Holds no guard while it waits: a long load must not hold
     * back the frees that are due. Returns the value, or the empty asset when the load failed; it lives as long as
     * `task`.
     */
    auto wait_for_load(task_future_t& task) const -> const std::shared_ptr<T>&
    {
        APP_SCOPE_PERF("Asset/Wait");
        task.change_priority(tpp::priority::high());
        const auto& value = task.get();
        if(!value)
        {
            return empty_asset();
        }
        asset_detail::epoch_domain::guard guard;
        const auto* state = load_state();
        if(state->task.valid() && state->task.is_ready() && &state->task.get() == &value)
        {
            // Release: a reader that acquires the pointer sees the asset it points at.
            state->resolved.store(&value, std::memory_order_release);
        }
        return value;
    }

    /**
     * @brief The asset `state` has without waiting, inside the caller's epoch guard: the resolved one, else the
     * value of its ready task (then recorded as resolved), the empty asset when that load failed; null without a
     * task or while it loads. Submits a deferred load. The result lives as long as the guard.
     */
    static auto find_loaded(const state_t& state) -> const std::shared_ptr<T>*
    {
        if(const auto* resolved = state.resolved.load(std::memory_order_acquire))
        {
            return resolved;
        }
        if(!state.task.valid())
        {
            return nullptr;
        }
        if(!state.task.is_submitted())
        {
            state.task.submit();
        }
        if(!state.task.is_ready())
        {
            return nullptr;
        }
        const auto& value = state.task.get();
        if(!value)
        {
            return &empty_asset();
        }
        // Release: a reader that acquires the pointer sees the asset it points at.
        state.resolved.store(&value, std::memory_order_release);
        return &value;
    }

    static auto unresolved(bool is_get) -> std::shared_ptr<T>
    {
        return is_get ? empty_asset() : nullptr;
    }

    /**
     * @brief The current state; only inside an epoch guard, which keeps it alive.
     */
    auto load_state() const -> const state_t*
    {
        return link_->state.load(std::memory_order_seq_cst);
    }

    /**
     * @brief Publishes the state `make(current)` returns in place of the current one, together with
     * its `has_task` flag, and retires the current one. Writers are serialized, so none loses
     * another's change. `keeps_resolved`: the new state holds the same task, and so the same asset.
     */
    template<typename F>
    void publish(F&& make, bool keeps_resolved)
    {
        state_t* replaced = nullptr;
        {
            std::lock_guard<std::mutex> lock(link_->write_mutex);
            // Relaxed: only writers store the state, and they hold the mutex.
            const state_t* current = link_->state.load(std::memory_order_relaxed);
            state_t* fresh = std::forward<F>(make)(*current);
            if(keeps_resolved)
            {
                // Relaxed: the exchange below publishes the fresh state with everything written to it.
                fresh->resolved.store(current->resolved.load(std::memory_order_acquire), std::memory_order_relaxed);
            }
            const bool has_task = fresh->task.valid();
            // Sequentially consistent with the readers' epoch announcements and the writer's slot scan (retire): a
            // reader that may still load the replaced state announced an epoch that keeps it alive.
            replaced = link_->state.exchange(fresh, std::memory_order_seq_cst);
            link_->has_task.store(has_task, std::memory_order_release);
        }
        // Retired after the lock: freeing it can destroy its task and, with that, the asset it loaded.
        asset_detail::epoch_domain::instance().retire(replaced,
                                                      [](void* object)
                                                      {
                                                          delete static_cast<state_t*>(object);
                                                      });
    }

    /**
     * @brief The canonical "empty" asset shared by all handles whose load failed / has no task.
     * Lifetime: process. Returned by reference: a copy counts a reference every handle of T shares.
     */
    static auto empty_asset() -> const std::shared_ptr<T>&
    {
        static const std::shared_ptr<T> empty = std::make_shared<T>();
        return empty;
    }

    void update_last_access() const
    {
        if(!link_)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        // Read before writing: threads reading one asset together then keep the stamp's cache line
        // shared instead of taking it from each other on every access. The exchange only raises the
        // stamp, so a thread holding an older time never moves it back.
        auto stored = link_->last_access_ns.load(std::memory_order_relaxed);
        while(ns - stored >= asset_detail::LAST_ACCESS_RESOLUTION_NS)
        {
            if(link_->last_access_ns.compare_exchange_weak(stored, ns, std::memory_order_relaxed))
            {
                return;
            }
        }
    }

    /// Shared pointer to the asset link. Cheap to copy.
    std::shared_ptr<asset_link_t> link_;
};
