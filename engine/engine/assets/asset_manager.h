#pragma once
#include <engine/engine_export.h>

#include "asset_flags.h"
#include "asset_storage.h"
#include "impl/asset_extensions.h"
#include <cassert>
#include <map>
#include <mutex>
#include <unordered_map>

namespace unravel
{

/**
 * @class asset_manager
 * @brief Manages assets, including loading, unloading, and storage.
 */
class asset_manager
{
public:
    /**
     * @brief Constructs an asset manager with the given context.
     * @param ctx The context for the asset manager.
     */
    asset_manager(rtti::context& ctx);

    /**
     * @brief Destructs the asset manager.
     */
    ~asset_manager();

    /**
     * @brief Sets the parent asset manager.
     * @param parent The parent asset manager.
     */
    void set_parent(asset_manager* parent);

    /**
     * @brief Initializes the asset manager with the given context.
     * @param ctx The context to initialize with.
     * @return True if initialization was successful, false otherwise.
     */
    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Deinitializes the asset manager with the given context.
     * @param ctx The context to deinitialize.
     * @return True if deinitialization was successful, false otherwise.
     */
    auto deinit(rtti::context& ctx) -> bool;

    /**
     * @brief Unloads all assets.
     */
    void unload_all();

    /**
     * @brief Unloads all assets in a specified group.
     * @param group The group to unload.
     */
    void unload_group(const std::string& group);

    /**
     * @brief Loads an asset database from a protocol.
     * @param protocol The protocol to load from.
     * @return True if the database was loaded successfully, false otherwise.
     */
    auto load_database(const std::string& protocol) -> bool;

    /**
     * @brief Saves the asset database to a specified path.
     * @param protocol The protocol to use for saving.
     * @param path The path to save the database to.
     */
    void save_database(const std::string& protocol, const fs::path& path);

    /**
     * @brief Removes asset information for a specified path.
     * @param path The path of the asset.
     */
    void remove_asset_info_for_path(const fs::path& path);

    /**
     * @brief Removes asset information for a specified key.
     * @param key The key of the asset.
     */
    void remove_asset_info_for_key(const std::string& key);


    void register_asset_info_for_path(const fs::path& path, bool override);
    /**
     * @brief Adds asset information for a specified path.
     * @param path The path of the asset.
     * @param meta The metadata of the asset.
     * @return The UUID of the added asset.
     */
    auto add_asset_info_for_path(const fs::path& path, const asset_meta& meta, bool override) -> hpp::uuid;

    /**
     * @brief Adds asset information for a specified key.
     * @param key The key of the asset.
     * @param meta The metadata of the asset.
     * @return The UUID of the added asset.
     */
    auto add_asset_info_for_key(const std::string& key, const asset_meta& meta, bool override) -> hpp::uuid;

    /**
     * @brief Gets metadata for a resource uid.
     * @param uid The the uuid of the resource.
     * @return Meta object containing information.
     */
    auto get_metadata(const hpp::uuid& uid) const -> asset_database::meta;
    auto get_metadata_for_path(const fs::path& path) const -> asset_database::meta;
    auto get_metadata_for_key(const std::string& key) const -> asset_database::meta;
    auto generate_metadata(const fs::path& p) const -> asset_meta;

    /**
     * @brief Gets typed header info for an asset without loading it.
     * @tparam HeaderT The header info type (e.g. texture_header_info).
     * @param uid The UUID of the asset.
     * @return Shared pointer to the header, or nullptr if not available.
     */
    template<typename HeaderT>
    auto get_header(const hpp::uuid& uid) const -> std::shared_ptr<HeaderT>
    {
        auto meta = get_metadata(uid);
        if(meta.meta.header && meta.meta.header->is<HeaderT>())
        {
            return std::static_pointer_cast<HeaderT>(meta.meta.header);
        }
        return nullptr;
    }

    /**
     * @brief Adds a storage for a specific type.
     * @tparam S The type of storage.
     * @tparam Args The arguments for constructing the storage.
     * @param args The arguments for constructing the storage.
     * @return A reference to the added storage.
     */
    template<typename S, typename... Args>
    auto add_storage(Args&&... args) -> asset_storage<S>&
    {
        auto operation = storages_.emplace(hpp::type_id<asset_storage<S>>().hash_code(),
                                           std::make_unique<asset_storage<S>>(std::forward<Args>(args)...));

        return static_cast<asset_storage<S>&>(*operation.first->second);
    }

    /**
     * @brief Gets an asset by its key.
     * @tparam T The type of the asset.
     * @param key The key of the asset.
     * @param flags The load flags for the asset.
     * @param mode Whether to load immediately or defer until first get().
     * @return The handle to the asset.
     */
    template<typename T>
    auto get_asset(const std::string& key,
                   load_flags flags = load_flags::standard,
                   load_mode mode = load_mode::immediate) -> asset_handle<T>
    {
        auto& storage = get_storage<T>();
        if(ex::should_keep_key_as_is(key))
        {
            return load_asset_from_file_impl<T>(key,
                                                flags,
                                                mode,
                                                storage.container_mutex,
                                                storage.container,
                                                storage.load_from_file);
        }
        const std::string resolved_key = ex::resolve_key_missing_extension<T>(key);
        return load_asset_from_file_impl<T>(resolved_key,
                                            flags,
                                            mode,
                                            storage.container_mutex,
                                            storage.container,
                                            storage.load_from_file);
    }

    /**
     * @brief Gets an asset by its UUID.
     * @tparam T The type of the asset.
     * @param uid The UUID of the asset.
     * @param flags The load flags for the asset.
     * @param mode Whether to load immediately or defer until first get().
     * @return The handle to the asset.
     */
    template<typename T>
    auto get_asset(const hpp::uuid& uid,
                   load_flags flags = load_flags::standard,
                   load_mode mode = load_mode::immediate) -> asset_handle<T>
    {
        auto meta = get_metadata(uid);
        if(!meta.location.empty())
        {
            const auto& key = meta.location;
            return get_asset<T>(key, flags, mode);
        }

        if(parent_)
        {
            return parent_->get_asset<T>(uid, flags, mode);
        }
        return {};
    }

    /**
     * @brief Registers an asset by key without loading it.
     * The actual load is triggered on the first handle.get() call.
     * @tparam T The type of the asset.
     * @param key The key of the asset.
     * @return A deferred handle to the asset.
     */
    template<typename T>
    auto register_asset(const std::string& key) -> asset_handle<T>
    {
        auto& storage = get_storage<T>();
        if(ex::should_keep_key_as_is(key))
        {
            return register_asset_impl<T>(key,
                                          storage.container_mutex,
                                          storage.container,
                                          storage.load_from_file);
        }
        const std::string resolved_key = ex::resolve_key_missing_extension<T>(key);
        return register_asset_impl<T>(resolved_key,
                                      storage.container_mutex,
                                      storage.container,
                                      storage.load_from_file);
    }

    /**
     * @brief Registers an asset by UUID without loading it.
     * The actual load is triggered on the first handle.get() call.
     * @tparam T The type of the asset.
     * @param uid The UUID of the asset.
     * @return A deferred handle to the asset.
     */
    template<typename T>
    auto register_asset(const hpp::uuid& uid) -> asset_handle<T>
    {
        auto meta = get_metadata(uid);
        if(!meta.location.empty())
        {
            return register_asset<T>(meta.location);
        }
        if(parent_)
        {
            return parent_->register_asset<T>(uid);
        }
        return {};
    }

    /**
     * @brief Triggers loading on all deferred handles of a given type.
     * Non-blocking: starts background loads but does not wait.
     * @tparam T The type of the assets to preload.
     */
    template<typename T>
    void preload_assets()
    {
        auto& storage = get_storage<T>();
        storage.preload_all();
    }

    /**
     * @brief Triggers loading on all deferred handles across every registered storage.
     * Non-blocking: starts background loads but does not wait.
     * Useful for game runtime preloading after scene deserialization.
     */
    void preload_all_assets();

    /**
     * @brief Finds an asset by its key.
     * @tparam T The type of the asset.
     * @param key The key of the asset.
     * @return A constant reference to the handle of the asset.
     */
    template<typename T>
    auto find_asset(const std::string& key) const -> const asset_handle<T>&
    {
        auto& storage = get_storage<T>();
        if(ex::should_keep_key_as_is(key))
        {
            return find_asset_impl<T>(key, storage.container_mutex, storage.container);
        }
        const std::string resolved_key = ex::resolve_key_missing_extension<T>(key);
        return find_asset_impl<T>(resolved_key, storage.container_mutex, storage.container);
    }

    /**
     * @brief Gets an asset handle from an instance.
     * @tparam T The type of the asset.
     * @param key The key of the asset.
     * @param entry The shared pointer to the asset instance.
     * @return The handle to the asset.
     */
    template<typename T>
    auto get_asset_from_instance(const std::string& key, std::shared_ptr<T> entry) -> asset_handle<T>
    {
        auto& storage = get_storage<T>();
        return get_asset_from_instance_impl(key,
                                            entry,
                                            storage.container_mutex,
                                            storage.container,
                                            storage.load_from_instance);
    }

    /**
     * @brief Renames an asset.
     * @tparam T The type of the asset.
     * @param key The current key of the asset.
     * @param new_key The new key for the asset.
     */
    template<typename T>
    void rename_asset(const std::string& key, const std::string& new_key)
    {
        for(auto& kvp : databases_)
        {
            auto& db = kvp.second;
            db.rename_asset(key, new_key);
        }

        auto& storage = get_storage<T>();

        std::lock_guard<std::recursive_mutex> lock(storage.container_mutex);
        auto it = storage.container.find(key);
        if(it != storage.container.end())
        {
            auto& handle = it->second;
            handle.set_internal_id(new_key);
            storage.container[new_key] = handle;
            storage.container.erase(it);
        }

        if(parent_)
        {
            parent_->rename_asset<T>(key, new_key);
        }
    }

    /**
     * @brief Unloads an asset by its key.
     * @tparam T The type of the asset.
     * @param key The key of the asset.
     */
    template<typename T>
    void unload_asset(const std::string& key)
    {
        auto& storage = get_storage<T>();
        storage.unload_single(pool_, key);

        if(parent_)
        {
            parent_->unload_asset<T>(key);
        }
    }

    /**
     * @brief Gets all assets in a specified group.
     * @tparam T The type of the assets.
     * @param group The group to get assets from.
     * @return A vector of handles to the assets.
     */
    template<typename T>
    auto get_assets(const std::string& group = {}) const -> std::vector<asset_handle<T>>
    {
        auto& storage = get_storage<T>();
        auto assets = storage.get_group(group);

        if(parent_)
        {
            auto passets = parent_->get_assets<T>(group);
            std::move(std::begin(passets), std::end(passets), std::back_inserter(assets));
        }

        return assets;
    }

    /**
     * @brief Gets all assets that satisfy a predicate.
     * @tparam T The type of the assets.
     * @tparam F The predicate function.
     * @param predicate The predicate function.
     * @return A vector of handles to the assets that satisfy the predicate.
     */
    template<typename T, typename F>
    auto get_assets_with_predicate(F&& predicate) const -> std::vector<asset_handle<T>>
    {
        auto& storage = get_storage<T>();
        return storage.get_with_condition(std::forward<F>(predicate));
    }

    /**
     * @brief Applies a callback function to each asset.
     * @tparam T The type of the assets.
     * @tparam F The callback function.
     * @param callback The callback function.
     */
    template<typename T, typename F>
    void for_each_asset(F&& callback)
    {
        auto& storage = get_storage<T>();
        std::lock_guard<std::recursive_mutex> lock(storage.container_mutex);

        for(const auto& el : storage.container)
        {
            callback(el);
        }

        if(parent_)
        {
            parent_->for_each_asset<T, F>(std::forward<F>(callback));
        }
    }

    /**
     * @brief Gets all assets.
     * @return A vector of handles to the assets.
     */
    auto get_all_assets(const std::string& group) const -> std::vector<std::string>;

    /**
     * @brief Evicts full-loaded assets not accessed within the given duration.
     * Demotes them back to deferred state so they can be re-loaded on next access.
     * @param group The group to evict assets from.
     * @param max_idle Maximum idle duration before eviction.
     */
    void evict_unused_assets(const std::string& group, std::chrono::steady_clock::duration max_idle);

        /**
     * @brief Adds an asset with a specified key.
     * @param key The key of the asset.
     * @return The UUID of the added asset.
     */
     auto add_asset(const std::string& key, bool override = false) -> hpp::uuid;

     /**
      * @brief Adds an asset with a specified path.
      * @param path The path of the asset.
      * @param override Whether to override the asset if it already exists.
      * @return The UUID of the added asset.
      */
     auto add_asset_for_path(const fs::path& path, bool override = false) -> hpp::uuid;
 

private:
    /**
     * @brief Gets the asset database for a specified group.
     * @param group The group to get the database for.
     * @return A reference to the asset database.
     */
    auto get_database(const std::string& group) -> asset_database&;

    /**
     * @brief Removes an asset database for a specified group.
     * @param group The group to remove the database for.
     */
    void remove_database(const std::string& group);


    /**
     * @brief Loads an asset from a file.
     * @tparam T The type of the asset.
     * @tparam F The function to load the asset.
     * @param key The key of the asset.
     * @param flags The load flags for the asset.
     * @param mode Whether to load immediately or defer until first get().
     * @param container_mutex The mutex for the asset container.
     * @param container The container for the assets.
     * @param load_func The function to load the asset.
     * @return The handle to the asset.
     */
    template<typename T, typename F>
    auto load_asset_from_file_impl(const std::string& key,
                                   load_flags flags,
                                   load_mode mode,
                                   std::recursive_mutex& container_mutex,
                                   typename asset_storage<T>::request_container_t& container,
                                   F&& load_func) -> asset_handle<T>
    {
        if(flags != load_flags::reload)
        {
            auto inst = find_asset_impl<T>(key, container_mutex, container);
            if(inst)
            {
                return inst;
            }
        }

        std::unique_lock<std::recursive_mutex> lock(container_mutex);

        auto& handle = container[key];
        if(load_func)
        {
            auto uid = add_asset(key);

            if(handle.task_id())
            {
                pool_.stop(handle.task_id());
                handle.invalidate();
            }

            handle.set_internal_ids(uid, key);
            load_func(pool_, handle, key, mode);
        }

        return handle;
    }

    /**
     * @brief Registers an asset handle with a deferred pool job.
     * The job is stored but not queued -- it runs when handle.get() is first called.
     * @tparam T The type of the asset.
     * @param key The key of the asset.
     * @param container_mutex The mutex for the asset container.
     * @param container The container for the assets.
     * @param load_func The function to load the asset (called with deferred mode).
     * @return A deferred handle to the asset.
     */
    template<typename T>
    auto register_asset_impl(const std::string& key,
                             std::recursive_mutex& container_mutex,
                             typename asset_storage<T>::request_container_t& container,
                             typename asset_storage<T>::load_from_file_t& load_func) -> asset_handle<T>
    {
        auto inst = find_asset_impl<T>(key, container_mutex, container);
        if(inst)
        {
            return inst;
        }

        std::lock_guard<std::recursive_mutex> lock(container_mutex);

        auto& handle = container[key];
        auto uid = add_asset(key);
        handle.set_internal_ids(uid, key);
        load_func(pool_, handle, key, load_mode::deferred);

        return handle;
    }

    /**
     * @brief Gets an asset handle from an instance.
     * @tparam T The type of the asset.
     * @tparam F The function to load the asset.
     * @param key The key of the asset.
     * @param entry The shared pointer to the asset instance.
     * @param container_mutex The mutex for the asset container.
     * @param container The container for the assets.
     * @param load_func The function to load the asset.
     * @return The handle to the asset.
     */
    template<typename T, typename F>
    auto get_asset_from_instance_impl(const std::string& key,
                                      std::shared_ptr<T> entry,
                                      std::recursive_mutex& container_mutex,
                                      typename asset_storage<T>::request_container_t& container,
                                      F&& load_func) -> asset_handle<T>
    {
        auto inst = find_asset_impl<T>(key, container_mutex, container);
        if(inst)
        {
            return inst;
        }

        std::lock_guard<std::recursive_mutex> lock(container_mutex);

        auto& handle = container[key];
        // Dispatch the loading
        if(load_func)
        {
            auto uid = add_asset(key);

            // loading on a locked mutex is ok
            // since we dont expect this to actually
            // do much except add tasks to the
            // executor
            handle.set_internal_ids(uid, key);
            load_func(pool_, handle, entry);
        }

        return handle;
    }

    /**
     * @brief Finds an asset handle by its key.
     * @tparam T The type of the asset.
     * @param key The key of the asset.
     * @param container_mutex The mutex for the asset container.
     * @param container The container for the assets.
     * @return A constant reference to the handle of the asset.
     */
    template<typename T>
    auto find_asset_impl(const std::string& key,
                         std::recursive_mutex& container_mutex,
                         typename asset_storage<T>::request_container_t& container) const -> const asset_handle<T>&
    {
        std::lock_guard<std::recursive_mutex> lock(container_mutex);
        auto it = container.find(key);
        if(it != container.end())
        {
            return it->second;
        }

        if(parent_)
        {
            return parent_->find_asset_impl<T>(key, container_mutex, container);
        }

        return asset_handle<T>::get_empty();
    }

    /**
     * @brief Gets the storage for a specific type.
     * @tparam S The type of the storage.
     * @return A reference to the storage.
     */
    template<typename S>
    auto get_storage() -> asset_storage<S>&
    {
        auto it = storages_.find(hpp::type_id<asset_storage<S>>().hash_code());
        assert(it != storages_.end());
        return (static_cast<asset_storage<S>&>(*it->second.get()));
    }

    /**
     * @brief Gets the storage for a specific type (const version).
     * @tparam S The type of the storage.
     * @return A constant reference to the storage.
     */
    template<typename S>
    auto get_storage() const -> asset_storage<S>&
    {
        auto& storage = storages_.at(hpp::type_id<asset_storage<S>>().hash_code());
        return (static_cast<asset_storage<S>&>(*storage.get()));
    }

    /// Thread pool for asset loading tasks.
    tpp::thread_pool& pool_;
    /// Different storages for assets.
    std::unordered_map<std::size_t, std::unique_ptr<basic_storage>> storages_{};
    /// Mutex for database operations.
    mutable std::mutex db_mutex_;
    /// Map of asset databases.
    std::map<std::string, asset_database, std::less<>> databases_{};
    /// Parent asset manager.
    asset_manager* parent_{};
};

} // namespace unravel
