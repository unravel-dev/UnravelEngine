#pragma once

#include <engine/engine_export.h>
#include <hpp/filesystem.hpp>
#include <logging/logging.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <uuid/uuid.h>

#include "../threading/threader.h"

template<typename T>
using task_future = tpp::job_shared_future<T>;

template<typename T>
struct asset_handle;

/**
 * @struct asset_link
 * @brief Thread-safe link to an asset.
 *
 * The link's "logical state" (uid, id, task) lives in an immutable `state_t`
 * snapshot held by `std::atomic<std::shared_ptr<state_t>>`. Readers do a
 * single `state.load()` to obtain a coherent snapshot — no torn reads are
 * possible even when another thread is concurrently invalidating or
 * reloading the link.
 *
 * The weak-asset cache and the last-access timestamp are kept *outside* the
 * snapshot so they can be updated without allocating a new snapshot on every
 * cache hit / get() call. `weak_asset` is guarded by a small mutex (weak_ptr
 * cannot be portably made atomic), `last_access_ns` is a relaxed int64
 * atomic.
 */
template<typename T>
struct asset_link
{
    using task_future_t = task_future<std::shared_ptr<T>>;
    using weak_asset_t = std::weak_ptr<T>;

    /**
     * @brief Immutable snapshot of the link's logical state.
     *
     * New snapshots are published via `state.store(...)`. Once published,
     * `state_t` instances are read-only and safe to share between threads
     * without further synchronization.
     */
    struct state_t
    {
        /// Unique identifier for the asset.
        hpp::uuid uid;
        /// String identifier for the asset.
        std::string id;
        /// Task future for the asset (valid for both scheduled and deferred jobs).
        task_future_t task;
    };

    /// Current snapshot. Initialized to an empty state so readers never see null.
    std::atomic<std::shared_ptr<state_t>> state{std::make_shared<state_t>()};

    /// Best-effort cache of the resolved asset. Updated on cache miss in get().
    /// Guarded by `weak_asset_mtx` (weak_ptr can't be portably atomic). The
    /// critical section is just a load or assignment, so contention is
    /// negligible.
    mutable std::mutex weak_asset_mtx;
    mutable weak_asset_t weak_asset;

    /// Last-access timestamp in nanoseconds-since-steady-epoch. Stored as
    /// `std::atomic<int64_t>` so updates from the rendering hot path never
    /// race with concurrent readers (and never block).
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
    using state_ptr = std::shared_ptr<state_t>;
    using task_future_t = typename asset_link_t::task_future_t;

    /**
     * @brief Equality operator for asset handles.
     */
    auto operator==(const asset_handle& rhs) const -> bool
    {
        return uid() == rhs.uid() && id() == rhs.id() && is_valid() == rhs.is_valid();
    }

    auto version() const -> uintptr_t
    {
        update_last_access();
        return uintptr_t(peek().get());
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
     * Returns by value — the caller receives a snapshot of the current id.
     * Binding to `const auto&` is still safe (the temporary's lifetime is
     * extended to the binding's scope).
     */
    auto id() const -> std::string
    {
        if(auto s = load_state())
        {
            return s->id;
        }
        return {};
    }

    /**
     * @brief Gets the unique identifier of the asset.
     *
     * Returns by value — the caller receives a snapshot of the current uid.
     * Binding to `const auto&` is still safe (the temporary's lifetime is
     * extended to the binding's scope).
     */
    auto uid() const -> hpp::uuid
    {
        if(auto s = load_state())
        {
            return s->uid;
        }
        return {};
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
     * get() triggers the actual load on the thread pool. Subsequent calls
     * behave as before (wait or poll).
     */
    auto get(bool wait = true) const -> std::shared_ptr<T>
    {
        update_last_access();

        if(auto cached_asset = peek())
        {
            return cached_asset;
        }

        // Take a stable snapshot. Even if another thread invalidates the link
        // mid-call, our local `s` keeps the old state alive for the duration.
        auto s = load_state();
        if(!s || !s->task.valid())
        {
            return empty_asset();
        }

        if(!s->task.is_submitted())
        {
            s->task.submit();
        }

        const bool ready = s->task.is_ready();
        const bool should_get = ready || wait;
        if(!should_get)
        {
            return empty_asset();
        }

        // Copy the task locally; we may need to change priority. task.get()
        // may block — we must hold no locks here.
        auto task = s->task;
        if(!ready)
        {
            task.change_priority(tpp::priority::high());
        }

        auto value = task.get();
        if(value)
        {
            std::lock_guard<std::mutex> lock(link_->weak_asset_mtx);
            link_->weak_asset = value;
            return value;
        }
        return empty_asset();
    }

    /**
     * @brief Checks if the handle references a task.
     */
    auto is_valid() const -> bool
    {
        auto s = load_state();
        return s && s->task.valid();
    }

    /**
     * @brief Checks if the task is ready.
     */
    auto is_ready() const -> bool
    {
        auto s = load_state();
        return s && s->task.valid() && s->task.is_ready();
    }

    /**
     * @brief Checks if the handle has a deferred job not yet submitted to workers.
     */
    auto is_deferred() const -> bool
    {
        auto s = load_state();
        return s && s->task.valid() && !s->task.is_submitted();
    }

    /**
     * @brief Checks if the asset is currently loading.
     */
    auto is_loading() const -> bool
    {
        auto s = load_state();
        return s && s->task.valid() && s->task.is_submitted() && !s->task.is_ready();
    }

    /**
     * @brief Submits a deferred task to the thread pool for execution.
     * Does nothing if the task is already submitted or invalid.
     */
    void submit()
    {
        auto s = load_state();
        if(s && s->task.valid() && !s->task.is_submitted())
        {
            s->task.submit();
        }
    }

    /**
     * @brief Demotes a fully loaded asset back to deferred state.
     *
     * Publishes a new snapshot that preserves uid / id but drops the task;
     * also clears the weak-asset cache. Caller should follow with a
     * load_from_file(deferred) to set up a new deferred task.
     */
    void demote_to_deferred()
    {
        if(!link_)
        {
            return;
        }

        auto old = load_state();
        auto fresh = std::make_shared<state_t>();
        if(old)
        {
            fresh->uid = old->uid;
            fresh->id = old->id;
        }
        link_->state.store(std::move(fresh), std::memory_order_release);

        std::lock_guard<std::mutex> lock(link_->weak_asset_mtx);
        link_->weak_asset.reset();
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
        if(auto s = load_state(); s && s->task.valid())
        {
            return s->task.id;
        }
        return tpp::job_id{};
    }

    /**
     * @brief Sets the internal job future.
     *
     * Publishes a new snapshot with the given task; preserves uid + id. Also
     * clears the weak-asset cache so the next get() resolves the new task.
     */
    void set_internal_job(const task_future_t& future)
    {
        ensure();
        publish_state(
            [&](state_t& fresh) -> void
            {
                fresh.task = future;
            });

        std::lock_guard<std::mutex> lock(link_->weak_asset_mtx);
        link_->weak_asset.reset();
    }

    /**
     * @brief Sets the internal IDs (uid + string identifier).
     *
     * Publishes a new snapshot; preserves the current task.
     */
    void set_internal_ids(const hpp::uuid& internal_uid, const std::string& internal_id = get_empty_id())
    {
        ensure();
        publish_state(
            [&](state_t& fresh) -> void
            {
                fresh.uid = internal_uid;
                fresh.id = internal_id;
            });
    }

    /**
     * @brief Sets the internal string identifier.
     *
     * Publishes a new snapshot; preserves uid and task.
     */
    void set_internal_id(const std::string& internal_id = get_empty_id())
    {
        ensure();
        publish_state(
            [&](state_t& fresh) -> void
            {
                fresh.id = internal_id;
            });
    }

    /**
     * @brief Invalidates the handle, resetting its state.
     *
     * Publishes an empty snapshot so any concurrent reader observing the
     * link after this call sees a fully-cleared state.
     */
    void invalidate()
    {
        if(auto s = load_state(); s && s->task.valid())
        {
            const auto task_count = s->task.use_count();
            if(task_count > 1)
            {
                APPLOG_TRACE("{} - task leak use_count {}", s->id, task_count);
            }
        }

        if(!link_)
        {
            return;
        }
        link_->state.store(std::make_shared<state_t>(), std::memory_order_release);

        std::lock_guard<std::mutex> lock(link_->weak_asset_mtx);
        link_->weak_asset.reset();
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
     * iteration use cases — e.g. walking the asset manager's loaded set to
     * build a dependency graph — where we only care about what is already
     * resident and must not have side effects.
     */
    auto peek() const -> std::shared_ptr<T>
    {
        if(!link_)
        {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(link_->weak_asset_mtx);
        return link_->weak_asset.lock();
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
     * @brief Loads the current immutable snapshot of the link's state.
     */
    auto load_state() const -> state_ptr
    {
        if(!link_)
        {
            return nullptr;
        }
        return link_->state.load(std::memory_order_acquire);
    }

    /**
     * @brief Publishes a new snapshot built from the current one and a
     * caller-supplied mutator. The mutator runs on a fresh copy; the old
     * snapshot is left untouched (other readers that loaded it remain
     * unaffected).
     */
    template<typename F>
    void publish_state(F&& mutator)
    {
        auto old = load_state();
        auto fresh = std::make_shared<state_t>();
        if(old)
        {
            *fresh = *old;
        }
        std::forward<F>(mutator)(*fresh);
        link_->state.store(std::move(fresh), std::memory_order_release);
    }

    /**
     * @brief Returns the canonical "empty" asset shared by all handles
     * whose load failed / has no task. Lifetime: process.
     */
    static auto empty_asset() -> std::shared_ptr<T>
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
        link_->last_access_ns.store(ns, std::memory_order_relaxed);
    }

    /// Shared pointer to the asset link. Cheap to copy.
    std::shared_ptr<asset_link_t> link_;
};
