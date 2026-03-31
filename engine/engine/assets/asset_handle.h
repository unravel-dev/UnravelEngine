#pragma once

#include <engine/engine_export.h>
#include <hpp/filesystem.hpp>
#include <logging/logging.h>
#include <chrono>
#include <memory>
#include <string>
#include <uuid/uuid.h>

#include "../threading/threader.h"

template<typename T>
using task_future = tpp::job_shared_future<T>;

template<typename T>
struct asset_handle;

/**
 * @struct asset_link
 * @brief Represents a link to an asset, including its task and weak pointer.
 * @tparam T The type of the asset.
 */
template<typename T>
struct asset_link
{
    using task_future_t = task_future<std::shared_ptr<T>>;
    using weak_asset_t = std::weak_ptr<T>;

    /// Unique identifier for the asset.
    hpp::uuid uid{};
    /// String identifier for the asset.
    std::string id{};
    /// Task future for the asset (valid for both scheduled and deferred jobs).
    task_future_t task{};
    /// Weak pointer to the asset.
    weak_asset_t weak_asset{};
    /// Last time this asset was accessed via get(), used for eviction.
    mutable std::chrono::steady_clock::time_point last_access{};
};

/**
 * @struct asset_handle
 * @brief Represents a handle to an asset, providing access and management functions.
 * @tparam T The type of the asset.
 */
template<typename T>
struct asset_handle
{
    using asset_link_t = asset_link<T>;

    /**
     * @brief Equality operator for asset handles.
     * @param rhs The right-hand side asset handle.
     * @return True if the handles are equal, false otherwise.
     */
    auto operator==(const asset_handle& rhs) const -> bool
    {
        return uid() == rhs.uid() && id() == rhs.id() && is_valid() == rhs.is_valid();
    }

    auto version() const -> uintptr_t
    {
        update_last_access();
        return uintptr_t(get_cached_asset().get());
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
     * @return The string identifier of the asset.
     */
    auto id() const -> const std::string&
    {
        if(link_)
        {
            return link_->id;
        }

        static const std::string empty;
        return empty;
    }

    /**
     * @brief Gets the unique identifier of the asset.
     * @return The unique identifier of the asset.
     */
    auto uid() const -> const hpp::uuid&
    {
        if(link_)
        {
            return link_->uid;
        }

        static const hpp::uuid empty;
        return empty;
    }

    /**
     * @brief Gets the name of the asset derived from its path.
     * @return The name of the asset.
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
     * @return The shared pointer to the asset.
     *
     * If the handle was registered with deferred loading, the first call
     * to get() triggers the actual load on the thread pool. Subsequent
     * calls behave as before (wait or poll).
     */
    auto get(bool wait = true) const -> std::shared_ptr<T>
    {
        update_last_access();

        if(auto cached_asset = get_cached_asset())
        {
            return cached_asset;
        }

        if(is_deferred())
        {
            link_->task.submit();
        }

        bool valid = is_valid();
        bool ready = is_ready();
        bool should_get = ready || (!ready && wait);

        if(valid && should_get)
        {
            auto task = link_->task;
            if(!ready)
            {
                task.change_priority(tpp::priority::high());
            }

            auto value = task.get();

            if(value)
            {
                link_->weak_asset = value;
                return value;
            }
        }

        static const std::shared_ptr<T> empty = std::make_shared<T>();
        return empty;
    }

    /**
     * @brief Checks if the handle is valid.
     * @return True if the handle is valid, false otherwise.
     */
    auto is_valid() const -> bool
    {
        return link_ && link_->task.valid();
    }

    /**
     * @brief Checks if the task is ready.
     * @return True if the task is ready, false otherwise.
     */
    auto is_ready() const -> bool
    {
        return is_valid() && link_->task.is_ready();
    }

    /**
     * @brief Checks if the handle has a deferred job not yet submitted to workers.
     * @return True if the task exists but hasn't been submitted.
     */
    auto is_deferred() const -> bool
    {
        return is_valid() && !link_->task.is_submitted();
    }

    /**
     * @brief Checks if the asset is currently loading (task exists but not ready).
     * @return True if the asset load is in progress.
     */
    auto is_loading() const -> bool
    {
        return is_valid() && link_->task.is_submitted() && !link_->task.is_ready();
    }

    /**
     * @brief Submits a deferred task to the thread pool for execution.
     * Does nothing if the task is already submitted or invalid.
     */
    void submit()
    {
        if(link_ && link_->task.valid() && !link_->task.is_submitted())
        {
            link_->task.submit();
        }
    }

    /**
     * @brief Demotes a fully loaded asset back to deferred state.
     * Clears the task and cached asset but preserves ids.
     * Caller should follow with load_from_file(deferred) to set up a new deferred task.
     */
    void demote_to_deferred()
    {
        if(link_)
        {
            link_->task = {};
            link_->weak_asset = {};
        }
    }

    /**
     * @brief Gets the last access timestamp.
     * @return The time point of the last get() call.
     */
    auto last_access() const -> std::chrono::steady_clock::time_point
    {
        if(link_)
        {
            return link_->last_access;
        }
        return {};
    }

    /**
     * @brief Gets the task ID.
     * @return The task ID.
     */
    auto task_id() const
    {
        if(link_)
        {
            return link_->task.id;
        }

        return tpp::job_id{};
    }

    /**
     * @brief Sets the internal job future.
     * @param future The task future to set.
     */
    void set_internal_job(const typename asset_link_t::task_future_t& future)
    {
        ensure();
        link_->task = future;
        link_->weak_asset = {};
    }

    /**
     * @brief Sets the internal IDs.
     * @param internal_uid The unique identifier to set.
     * @param internal_id The string identifier to set.
     */
    void set_internal_ids(const hpp::uuid& internal_uid, const std::string& internal_id = get_empty_id())
    {
        ensure();
        link_->uid = internal_uid;
        link_->id = internal_id;
    }

    /**
     * @brief Sets the internal string identifier.
     * @param internal_id The string identifier to set.
     */
    void set_internal_id(const std::string& internal_id = get_empty_id())
    {
        ensure();
        link_->id = internal_id;
    }

    /**
     * @brief Invalidates the handle, resetting its state.
     */
    void invalidate()
    {
        if(is_valid())
        {
            auto task_count = link_->task.use_count();
            if(task_count > 1)
            {
                APPLOG_TRACE("{} - task leak use_count {}", id(), task_count);
            }
        }
        set_internal_ids({});
        set_internal_job({});
    }


    /**
     * @brief Gets an empty asset handle.
     * @return The empty asset handle.
     */
    static auto get_empty() -> const asset_handle&
    {
        static const asset_handle none_asset = []()
        {
            asset_handle asset;
            asset.set_internal_ids({});
            return asset;
        }();
        return none_asset;
    }

    /**
     * @brief Gets an empty string identifier.
     * @return The empty string identifier.
     */
    static auto get_empty_id() -> const std::string&
    {
        static const std::string empty{"None"};
        return empty;
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

    void update_last_access() const
    {
        if(link_)
        {
            link_->last_access = std::chrono::steady_clock::now();
        }
    }
    auto get_cached_asset() const -> std::shared_ptr<T>
    {
        if(link_)
        {
            return link_->weak_asset.lock();
        }

        return nullptr;
    }
    /// Shared pointer to the asset link.
    std::shared_ptr<asset_link_t> link_;
};
