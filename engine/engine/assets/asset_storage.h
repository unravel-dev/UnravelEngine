#pragma once

#include "asset_handle.h"
#include "asset_flags.h"

#include <context/context.hpp>
#include <hpp/event.hpp>

#include <cassert>
#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>

#include <reflection/registration.h>

namespace unravel
{

struct asset_importer_meta : crtp_meta_type<asset_importer_meta>
{
    virtual ~asset_importer_meta() = default;
};

struct texture_importer_meta : crtp_meta_type<texture_importer_meta, asset_importer_meta>
{
    enum class texture_type
    {
        automatic,
        normal_map,
        equirect,
    };

    enum class compression_quality
    {
        project_default,
        none,
        low_quality,
        normal_quality,
        high_quality
    };

    enum class texture_size
    {
        project_default,
        size_32,
        size_64,
        size_128,
        size_256,
        size_512,
        size_1024,
        size_2048,
        size_4096,
        size_8192,
        size_16384
        
    };

    texture_type type{texture_type::automatic};
    bool generate_mipmaps{true};
    /// Bake OpenGL/DirectX normal Y difference into texels at compile (normal_map type only).
    bool invert_normal_y{false};

    struct quality_meta
    {
        texture_size max_size{texture_size::project_default};
        compression_quality compression{compression_quality::project_default};

    } quality;


};

struct mesh_importer_meta : crtp_meta_type<mesh_importer_meta, asset_importer_meta>
{
    struct model_meta
    {
        bool import_meshes{true};
        bool weld_vertices{true};
        bool optimize_meshes{true};
        bool split_large_meshes{true};
        bool find_degenerates{true};
        bool find_invalid_data{true};
        
        ///< Enable automatic LOD generation during compilation
        bool generate_lods{true};
        ///< Target error for LOD generation (lower = higher quality, higher = more aggressive)
        float lod_target_error{0.01f};
    } model;

    ///< Signed distance field bake, consumed by the surface cache GI tracer. The field is
    ///< rigid, so it is generated for every mesh; skinned meshes use it through per-segment
    ///< proxies rather than as a single instance.
    struct sdf_meta
    {
        ///< Generate a distance field for this mesh at compile time.
        bool generate_sdf{true};
        ///< Target voxel count along the longest bounds axis.
        uint32_t resolution{64};
        ///< Clamps on the derived voxel size, in local units.
        float min_voxel_size{0.01f};
        float max_voxel_size{1.0f};
        ///< Ceiling on total grid voxels in one field. The dominant control on both bake time
        ///< and atlas footprint, since both scale with voxel count. Lower this on models split
        ///< into very many submeshes, where the per-field cost is multiplied by the split.
        uint64_t max_total_voxels{262144};
        ///< LOD the field is baked from. 0 is the full-detail topology. Higher levels cost less
        ///< to bake -- a closest-point query scales with roughly the square root of the triangle
        ///< count -- and the field is coarse enough that the lost detail is usually below its
        ///< voxel size anyway. A level beyond the last one generated is clamped to the coarsest
        ///< available, so a mesh with no generated LODs bakes from the base.
        uint32_t lod_index{2};
        ///< Bake an unsigned shell instead of a signed field. Required for foliage and any
        ///< other mesh that is not a closed surface, where inside/outside is meaningless.
        bool two_sided{false};
        ///< Local-space half thickness given to the shell when @ref two_sided is set.
        float two_sided_thickness{0.05f};
    } sdf;

    struct rig_meta
    {

    } rig;

    struct animations_meta
    {
        bool import_animations{true};

    } animations;

    struct materials_meta
    {
        bool import_materials{true};
        bool remove_redundant_materials{true};

    } materials;
};

struct animation_importer_meta : crtp_meta_type<animation_importer_meta, asset_importer_meta>
{

    struct root_motion_meta
    {
        bool keep_position_y{true};
        bool keep_position_xz{};
        bool keep_rotation{};

        bool keep_in_place{};

    } root_motion;
};

struct audio_importer_meta : crtp_meta_type<audio_importer_meta, asset_importer_meta>
{
    bool force_to_mono{false};
};

//-----------------------------------------------------------------------------
// Asset header info -- lightweight per-type metadata readable without full load.
// Populated during compilation, stored in .meta files (editor only).
//-----------------------------------------------------------------------------

/// Base class for per-type asset header information.
struct asset_header_info : crtp_meta_type<asset_header_info>
{
    virtual ~asset_header_info() = default;
    size_t file_size{};
};

struct texture_header_info : crtp_meta_type<texture_header_info, asset_header_info>
{
    uint32_t width{};
    uint32_t height{};
    uint16_t depth{1};
    uint16_t num_layers{1};
    uint16_t num_mips{1};
    std::string format{};
};

struct mesh_header_info : crtp_meta_type<mesh_header_info, asset_header_info>
{
    uint32_t vertex_count{};
    uint32_t index_count{};
    uint32_t submesh_count{};
    uint32_t lod_count{};
};

struct animation_header_info : crtp_meta_type<animation_header_info, asset_header_info>
{
    float duration{};
    uint32_t channel_count{};
    float sample_rate{};
};

struct audio_header_info : crtp_meta_type<audio_header_info, asset_header_info>
{
    float duration{};
    uint32_t sample_rate{};
    uint16_t channels{};
};

/**
 * @struct asset_meta
 * @brief Metadata for an asset, including its UUID and type.
 */
struct asset_meta
{
    /// Unique identifier for the asset.
    hpp::uuid uid{};
    /// Type of the asset.
    std::string type{};
    /// Importer meta
    std::shared_ptr<asset_importer_meta> importer;
    /// Per-type header info (editor only, populated during compilation).
    std::shared_ptr<asset_header_info> header;
};

/**
 * @class asset_database
 * @brief Manages asset metadata and provides functionality for adding, removing, and querying assets.
 */
class asset_database
{
public:
    /**
     * @struct meta
     * @brief Metadata information for an asset including its location.
     */
    struct meta
    {
        /// Location of the asset.
        std::string location{};
        /// Metadata of the asset.
        asset_meta meta;
    };

    /// Type definition for the asset database.
    using database_t = std::map<hpp::uuid, meta>;

    /**
     * @brief Gets the entire asset database.
     * @return A constant reference to the asset database.
     */
    auto get_database() const -> const database_t&
    {
        return asset_meta_;
    }

    /**
     * @brief Sets the asset database.
     * @param rhs The asset database to set.
     */
    void set_database(const database_t& rhs)
    {
        std::lock_guard<std::mutex> lock(asset_mutex_);
        asset_meta_ = rhs;
    }

    /**
     * @brief Removes all assets from the database.
     */
    void remove_all()
    {
        std::lock_guard<std::mutex> lock(asset_mutex_);
        asset_meta_.clear();
    }

    /**
     * @brief Adds an asset to the database.
     *
     * If an entry already exists for `location`:
     *   - `override == false`: returns the existing UUID and does nothing else.
     *   - `override == true`:  updates the entry's metadata *in place*, keeping
     *     the original UUID so that any link referencing it stays valid.
     *
     * Otherwise inserts a new entry keyed by `meta.uid`.
     *
     * The entire operation runs under a single lock: looking up the existing
     * entry, deciding whether to insert vs. update, and mutating the map all
     * happen atomically. The previous implementation released the lock between
     * the lookup and the insert, which opened a TOC-TOU window where another
     * thread could remove the entry and we'd end up inserting under a fresh
     * random UUID instead of preserving the existing one (or, with override,
     * creating a duplicate entry that aliased the same location under two
     * different UUIDs).
     *
     * @param location The location of the asset.
     * @param meta The metadata of the asset.
     * @param override Whether to overwrite an existing entry's metadata.
     * @return The UUID of the added (or pre-existing) asset.
     */
    auto add_asset(const std::string& location, const asset_meta& meta, bool override) -> hpp::uuid
    {
        std::lock_guard<std::mutex> lock(asset_mutex_);

        auto existing = find_entry_by_location_unlocked(location);
        if(existing != asset_meta_.end())
        {
            if(!override)
            {
                return existing->first;
            }

            // Update in place, preserving the existing UUID so that other
            // assets that reference it (e.g. materials → textures) don't get
            // orphaned by an override pass.
            existing->second.location = location;
            existing->second.meta = meta;
            existing->second.meta.uid = existing->first;
            return existing->first;
        }

        auto& metainfo = asset_meta_[meta.uid];
        metainfo.location = location;
        metainfo.meta = meta;
        // APPLOG_TRACE("{} - {} -> {}", __func__, hpp::to_string(metainfo.meta.uid), location);
        return metainfo.meta.uid;
    }

    /**
     * @brief Gets the UUID of an asset based on its location.
     * @param location The location of the asset.
     * @return The UUID of the asset, or a nil UUID if not found.
     */
    auto get_uuid(const std::string& location) const -> const hpp::uuid&
    {
        std::lock_guard<std::mutex> lock(asset_mutex_);

        auto it = find_entry_by_location_unlocked(location);
        if(it != asset_meta_.end())
        {
            return it->first;
        }

        static const hpp::uuid uid;
        return uid;
    }

    auto get_metadata(const std::string& location) const -> const meta&
    {
        std::lock_guard<std::mutex> lock(asset_mutex_);

        auto it = find_entry_by_location_unlocked(location);
        if(it != asset_meta_.end())
        {
            return it->second;
        }

        static const meta empty;
        return empty;
    }

    /**
     * @brief Gets the metadata of an asset based on its UUID.
     * @param id The UUID of the asset.
     * @return The metadata of the asset.
     */
    auto get_metadata(const hpp::uuid& id) const -> const meta&
    {
        std::lock_guard<std::mutex> lock(asset_mutex_);

        auto it = asset_meta_.find(id);
        if(it == asset_meta_.end())
        {
            static const meta empty;
            return empty;
        }

        return it->second;
    }

    /**
     * @brief Renames an asset.
     * @param key The current key of the asset.
     * @param new_key The new key for the asset.
     */
    void rename_asset(const std::string& key, const std::string& new_key)
    {
        std::lock_guard<std::mutex> lock(asset_mutex_);
        for(auto& kvp : asset_meta_)
        {
            auto& uid = kvp.first;
            auto& metainfo = kvp.second;
            if(metainfo.location == key)
            {
                APPLOG_TRACE("{}::{} - {} -> {}", __func__, hpp::to_string(uid), key, new_key);

                metainfo.location = new_key;
            }
        }
    }

    /**
     * @brief Removes an asset from the database.
     * @param key The key of the asset to remove.
     */
    void remove_asset(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(asset_mutex_);
        for(auto& kvp : asset_meta_)
        {
            auto& uid = kvp.first;
            auto& metainfo = kvp.second;
            if(metainfo.location == key)
            {
                APPLOG_TRACE("{}::{} - {}", __func__, hpp::to_string(uid), key);

                asset_meta_.erase(uid);
                return;
            }
        }
    }

private:
    /**
     * @brief Linear-scan lookup by location. Caller must hold `asset_mutex_`.
     *
     * Returned iterator is into `asset_meta_` and remains valid for the
     * duration of the lock (std::map iterators stay valid across insertions
     * and across erases of *other* elements).
     */
    auto find_entry_by_location_unlocked(const std::string& location) -> database_t::iterator
    {
        for(auto it = asset_meta_.begin(); it != asset_meta_.end(); ++it)
        {
            if(it->second.location == location)
            {
                return it;
            }
        }
        return asset_meta_.end();
    }

    auto find_entry_by_location_unlocked(const std::string& location) const -> database_t::const_iterator
    {
        for(auto it = asset_meta_.cbegin(); it != asset_meta_.cend(); ++it)
        {
            if(it->second.location == location)
            {
                return it;
            }
        }
        return asset_meta_.cend();
    }

    /// Mutex for asset database operations.
    mutable std::mutex asset_mutex_{};
    /// The asset database.+
    database_t asset_meta_{};
};

/**
 * @struct basic_storage
 * @brief Abstract base class for asset storage.
 */
struct basic_storage
{
    virtual ~basic_storage() = default;

    /**
     * @brief Unloads all assets.
     * @param pool The thread pool for unloading tasks.
     */
    virtual void unload_all(tpp::thread_pool& pool) = 0;

    /**
     * @brief Unloads a single asset by its key.
     * @param pool The thread pool for unloading tasks.
     * @param key The key of the asset to unload.
     */
    virtual void unload_single(tpp::thread_pool& pool, const std::string& key) = 0;

    /**
     * @brief Unloads all assets in a specified group.
     * @param pool The thread pool for unloading tasks.
     * @param group The group to unload.
     */
    virtual void unload_group(tpp::thread_pool& pool, const std::string& group) = 0;

    /**
     * @brief Evicts loaded assets not accessed within the given duration.
     * Demotes them back to deferred state so memory is freed.
     * @param pool The thread pool.
     * @param max_idle Maximum idle duration before eviction.
     */
    virtual void evict_unused(tpp::thread_pool& pool, const std::string& group, std::chrono::steady_clock::duration max_idle) = 0;

    /**
     * @brief Triggers loading on all deferred handles in this storage.
     * Non-blocking: starts background loads but does not wait.
     */
    virtual void preload_all() = 0;
};

/**
 * @struct asset_storage
 * @brief Manages storage and loading of assets of a specific type.
 * @tparam T The type of the assets.
 */
template<typename T>
struct asset_storage : public basic_storage
{
    /// Container for asset requests.
    using request_container_t = std::unordered_map<std::string, asset_handle<T>>;
    /// Type alias for callable functions.
    template<typename F>
    using callable = std::function<F>;

    /// Function type for loading from file.
    using load_from_file_t = callable<bool(tpp::thread_pool& pool, asset_handle<T>&, const std::string&, load_mode)>;

    /// Function type for loading from instance. Predicate function type.
    using predicate_t = callable<bool(const asset_handle<T>&)>;
    using load_from_instance_t = callable<bool(tpp::thread_pool& pool, asset_handle<T>&, std::shared_ptr<T>)>;

    ~asset_storage() override = default;

    /**
     * @brief Unloads a handle.
     * @param pool The thread pool for unloading tasks.
     * @param handle The handle to unload.
     */
    void unload_handle(tpp::thread_pool& pool, asset_handle<T>& handle)
    {
        pool.stop(handle.task_id());
        handle.invalidate();
    }

    /**
     * @brief Unloads assets that satisfy a condition.
     * @param pool The thread pool for unloading tasks.
     * @param predicate The predicate function to determine which assets to unload.
     */
    void unload_with_condition(tpp::thread_pool& pool, const predicate_t& predicate)
    {
        std::lock_guard<std::recursive_mutex> lock(container_mutex);
        for(auto it = container.begin(); it != container.end();)
        {
            if(predicate(it->second))
            {
                auto& handle = it->second;
                unload_handle(pool, handle);
                it = container.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    /**
     * @brief Unloads all assets.
     * @param pool The thread pool for unloading tasks.
     */
    void unload_all(tpp::thread_pool& pool) final
    {
        unload_with_condition(pool,
                              [](const auto& it)
                              {
                                  return true;
                              });
    }

    /**
     * @brief Unloads all assets in a specified group.
     * @param pool The thread pool for unloading tasks.
     * @param group The group to unload.
     */
    void unload_group(tpp::thread_pool& pool, const std::string& group) final
    {
        unload_with_condition(pool,
                              [&](const auto& it)
                              {
                                  const auto& id = it.id();
                                  hpp::string_view id_view(id);
                                  return id_view.starts_with(group);
                              });
    }

    /**
     * @brief Unloads a single asset by its key.
     * @param pool The thread pool for unloading tasks.
     * @param key The key of the asset to unload.
     */
    void unload_single(tpp::thread_pool& pool, const std::string& key) final
    {
        unload_with_condition(pool,
                              [&](const auto& it)
                              {
                                  const auto& id = it.id();
                                  return id == key;
                              });
    }

    /**
     * @brief Gets assets that satisfy a condition.
     * @param predicate The predicate function to determine which assets to get.
     * @return A vector of asset handles that satisfy the condition.
     */
    auto get_with_condition(const predicate_t& predicate) const -> std::vector<asset_handle<T>>
    {
        std::lock_guard<std::recursive_mutex> lock(container_mutex);
        std::vector<asset_handle<T>> result;
        result.reserve(container.size() + 1);
        result.emplace_back(asset_handle<T>::get_empty());

        for(const auto& kvp : container)
        {
            if(predicate(kvp.second))
            {
                result.emplace_back(kvp.second);
            }
        }

        return result;
    }

    /**
     * @brief Gets all assets in a specified group.
     * @param group The group to get assets from.
     * @return A vector of asset handles in the group.
     */
    auto get_group(const std::string& group) const -> std::vector<asset_handle<T>>
    {
        return get_with_condition(
            [&](const auto& it)
            {
                const auto& id = it.id();
                hpp::string_view id_view(id);
                return id_view.starts_with(group);
            });
    }

    /**
     * @brief Evicts loaded assets not accessed within the given duration.
     * Assets whose weak_ptr has expired (no external references) and that
     * have been idle longer than max_idle are demoted back to deferred state.
     * @param pool The thread pool.
     * @param max_idle Maximum idle duration before eviction.
     */
    void evict_unused(tpp::thread_pool& pool, const std::string& group, std::chrono::steady_clock::duration max_idle) final
    {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::recursive_mutex> lock(container_mutex);
        for(auto& [key, handle] : container)
        {
            const auto& id = handle.id();
            hpp::string_view id_view(id);
            if(!id_view.starts_with(group))
            {
                continue;
            }
            if(!handle.is_ready())
            {
                continue;
            }
            bool is_idle = (now - handle.last_access()) > max_idle;
            if(!is_idle)
            {
                continue;
            }
            pool.stop(handle.task_id());
            handle.demote_to_deferred();
            load_from_file(pool, handle, key, load_mode::deferred);
        }
    }

    void preload_all() final
    {
        std::lock_guard<std::recursive_mutex> lock(container_mutex);
        for(auto& [key, handle] : container)
        {
            if(handle.is_deferred())
            {
                handle.submit();
            }
        }
    }

    /// Function for loading assets from file.
    load_from_file_t load_from_file;
    /// Function for loading assets from instance.
    load_from_instance_t load_from_instance;
    /// Container for asset requests.
    request_container_t container;
    /// Mutex for container operations.
    mutable std::recursive_mutex container_mutex;
};

} // namespace unravel
