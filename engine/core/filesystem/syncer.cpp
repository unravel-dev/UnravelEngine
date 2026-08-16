#include "syncer.h"
#include "watcher.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <concurrency/parallel.h>

#include <logging/logging.h>
namespace fs
{

static auto extract_entry_extension(const fs::path& path) -> std::string
{
    auto entry_path = path;
    std::string entry_extension;
    while(entry_path.has_extension())
    {
        entry_extension = entry_path.extension().string() + entry_extension;
        entry_path.replace_extension();
    }
    return entry_extension;
}

static void ensure_directory_exists(const fs::path& path)
{
    fs::error_code err;
    if(path.has_extension())
    {
        fs::create_directories(fs::path(path).parent_path(), err);
    }
    else
    {
        fs::create_directories(path, err);
    }
}

/// Paths that share a key are processed on one worker in order (create -> rename, etc.).
static auto entry_serialization_key(const fs::path& watch_root, const fs::watcher::entry& entry) -> std::string
{
    const auto normalize_key = [](const fs::path& path) -> std::string
    {
        return path.lexically_normal().generic_string();
    };

    if(entry.status == fs::watcher::entry_status::renamed)
    {
        const fs::path parent_old = entry.last_path.parent_path();
        const fs::path parent_new = entry.path.parent_path();
        if(parent_old == parent_new)
        {
            const fs::path key = parent_old.empty() ? watch_root : parent_old;
            return normalize_key(key);
        }
        // Cross-directory rename: keep ordering relative to the rest of the tree.
        return normalize_key(watch_root);
    }

    if(entry.type == fs::file_type::directory)
    {
        return normalize_key(entry.path);
    }

    const fs::path parent = entry.path.parent_path();
    const fs::path key = parent.empty() ? watch_root : parent;
    return normalize_key(key);
}

syncer::~syncer()
{
    unsync();
}

void syncer::set_mapping(const std::string& ref_ext,
                         const std::vector<std::string>& synced_ext,
                         on_entry_created_t on_entry_created = nullptr,
                         on_entry_modified_t on_entry_modified = nullptr,
                         on_entry_removed_t on_entry_removed = nullptr,
                         on_entry_renamed_t on_entry_renamed = nullptr)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& mapping = mapping_[ref_ext];
    mapping.extensions = synced_ext;
    mapping.on_entry_created = std::move(on_entry_created);
    mapping.on_entry_modified = std::move(on_entry_modified);
    mapping.on_entry_removed = std::move(on_entry_removed);
    mapping.on_entry_renamed = std::move(on_entry_renamed);
}

void syncer::set_directory_mapping(syncer::on_entry_created_t on_entry_created,
                                   syncer::on_entry_modified_t on_entry_modified,
                                   syncer::on_entry_removed_t on_entry_removed,
                                   syncer::on_entry_renamed_t on_entry_renamed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& mapping = mapping_[""];
    mapping.on_entry_created = std::move(on_entry_created);
    mapping.on_entry_modified = std::move(on_entry_modified);
    mapping.on_entry_removed = std::move(on_entry_removed);
    mapping.on_entry_renamed = std::move(on_entry_renamed);
}

void syncer::unsync()
{
    const auto id = watch_id_.exchange(0);
    if(id != 0)
    {
        fs::watcher::unwatch(id);
    }
}

auto syncer::get_mapping(const std::string& ext) -> mapping
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mapping_.find(ext);
    if(it != mapping_.end())
    {
        return it->second;
    }

    return {};
}

auto syncer::get_on_created_callback(const std::string& ext) -> on_entry_created_t
{
    return get_mapping(ext).on_entry_created;
}

auto syncer::get_on_modified_callback(const std::string& ext) -> on_entry_modified_t
{
    return get_mapping(ext).on_entry_modified;
}

auto syncer::get_on_removed_callback(const std::string& ext) -> on_entry_removed_t
{
    return get_mapping(ext).on_entry_removed;
}

auto syncer::get_on_renamed_callback(const std::string& ext) -> on_entry_renamed_t
{
    return get_mapping(ext).on_entry_renamed;
}

void syncer::sync(const fs::path& reference_dir, const fs::path& synced_dir, const on_sync_progress_t& on_progress)
{
    unsync();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        reference_dir_ = reference_dir;
        synced_dir_ = synced_dir;
        reference_dir_.make_preferred();
        synced_dir_.make_preferred();
        ensure_directory_exists(reference_dir_);
        ensure_directory_exists(synced_dir_);
    }

    const auto on_change = [this, on_progress](const auto& entries, bool is_initial_listing)
    {
        const auto process_entry = [this, is_initial_listing](const watcher::entry& entry)
        {
            const bool is_directory = (entry.type == fs::file_type::directory);
            const std::string entry_extension = extract_entry_extension(entry.path);

            switch(entry.status)
            {
                case fs::watcher::entry_status::created:
                {
                    const auto synced_entries = this->get_synced_entries(entry.path, is_directory);

                    for(const auto& synced_entry : synced_entries)
                    {
                        ensure_directory_exists(synced_entry);
                    }

                    auto callback = this->get_on_created_callback(entry_extension);
                    if(callback)
                    {
                        callback(entry_extension, entry.path, synced_entries, is_initial_listing);
                    }
                }
                break;
                case fs::watcher::entry_status::modified:
                {
                    auto callback = this->get_on_modified_callback(entry_extension);
                    if(callback)
                    {
                        const auto synced_entries = this->get_synced_entries(entry.path, is_directory);
                        callback(entry_extension, entry.path, synced_entries, is_initial_listing);
                    }
                }
                break;
                case fs::watcher::entry_status::removed:
                {
                    const auto callback = this->get_on_removed_callback(entry_extension);

                    if(callback)
                    {
                        const auto synced_entries = this->get_synced_entries(entry.path, is_directory);
                        callback(entry_extension, entry.path, synced_entries);
                    }
                }
                break;
                case fs::watcher::entry_status::renamed:
                {
                    const auto last_synced_entries = this->get_synced_entries(entry.last_path, is_directory);
                    const auto synced_entries = this->get_synced_entries(entry.path, is_directory);
                    auto callback = this->get_on_renamed_callback(entry_extension);

                    if(callback && synced_entries.size() == last_synced_entries.size())
                    {
                        std::vector<rename_pair_t> synced_renamed;
                        synced_renamed.reserve(synced_entries.size());

                        for(std::size_t i = 0; i < synced_entries.size(); ++i)
                        {
                            const auto& last_synced_entry = last_synced_entries[i];
                            const auto& synced_entry = synced_entries[i];
                            rename_pair_t p(last_synced_entry, synced_entry);
                            synced_renamed.emplace_back(std::move(p));
                        }
                        rename_pair_t p(entry.last_path, entry.path);
                        callback(entry_extension, p, synced_renamed);
                    }
                }
                break;
                default:
                    break;
            }
        };

        const fs::path watch_root = get_watch_path();

        // Initial listing may include a folder and files under it, but every
        // created entry calls ensure_directory_exists() before its callback, so
        // parent/child create order does not matter. Live batches with
        // renamed/removed still need per-folder ordering (e.g. delete files
        // before their parent directory, create before same-folder rename).
        const auto needs_per_folder_ordering = [&](auto begin, auto end) -> bool
        {
            if(is_initial_listing)
            {
                return false;
            }
            for(auto it = begin; it != end; ++it)
            {
                if(it->status == fs::watcher::entry_status::renamed
                   || it->status == fs::watcher::entry_status::removed)
                {
                    return true;
                }
            }
            return false;
        };

        const auto run_entry_batch = [&](auto begin, auto end)
        {
            if(!needs_per_folder_ordering(begin, end))
            {
                poolstl::for_each_par_if(true, begin, end, process_entry);
                return;
            }

            std::unordered_map<std::string, std::vector<const fs::watcher::entry*>> groups;
            groups.reserve(static_cast<std::size_t>(std::distance(begin, end)) / 4 + 1);

            for(auto it = begin; it != end; ++it)
            {
                const std::string key = entry_serialization_key(watch_root, *it);
                groups[key].push_back(&(*it));
            }

            std::vector<std::vector<const fs::watcher::entry*>> group_list;
            group_list.reserve(groups.size());
            for(auto& group : groups)
            {
                group_list.push_back(std::move(group.second));
            }

            const auto process_group = [&](const std::vector<const fs::watcher::entry*>& group_entries)
            {
                for(const fs::watcher::entry* entry : group_entries)
                {
                    process_entry(*entry);
                }
            };

            poolstl::for_each_par_if(true, group_list.begin(), group_list.end(), process_group);
        };

        if(entries.empty())
        {
            return;
        }

        // Progress must run on the watcher thread (not pool workers). Batch entries so
        // on_progress fires a bounded number of times while work still runs in parallel.
        if(!is_initial_listing || !on_progress)
        {
            run_entry_batch(entries.begin(), entries.end());
            return;
        }

        constexpr size_t k_max_progress_updates = 16;
        const size_t progress_stride =
            std::max<size_t>(1, (entries.size() + k_max_progress_updates - 1) / k_max_progress_updates);

        size_t completed = 0;
        for(size_t offset = 0; offset < entries.size(); offset += progress_stride)
        {
            const auto chunk_begin = entries.begin() + static_cast<std::ptrdiff_t>(offset);
            const auto chunk_end =
                entries.begin() + static_cast<std::ptrdiff_t>(std::min(offset + progress_stride, entries.size()));

            run_entry_batch(chunk_begin, chunk_end);

            completed = std::min(offset + progress_stride, entries.size());
            on_progress(completed, entries.size(), extract_entry_extension(std::prev(chunk_end)->path));
        }
    };


    // const auto on_change = [this, on_progress](const auto& entries, bool is_initial_listing)
    // {
    //     size_t completed = 0;
    //     for(const auto& entry : entries)
    //     {
    //         bool is_directory = (entry.type == fs::file_type::directory);
    //         auto entry_path = entry.path;
    //         std::string entry_extension = extract_entry_extension(entry_path);

    //         // APPLOG_TRACE("Syncer: process entry {}", fs::to_string(entry));

    //         if(is_initial_listing)
    //         {
    //             if(on_progress)
    //             {
    //                 on_progress(completed, entries.size(), entry_extension);
    //             }
    //         }

    //         switch(entry.status)
    //         {
    //             case fs::watcher::entry_status::created:
    //             {
    //                 const auto synced_entries = this->get_synced_entries(entry.path, is_directory);

    //                 for(const auto& synced_entry : synced_entries)
    //                 {
    //                     ensure_directory_exists(synced_entry);
    //                 }

    //                 auto callback = this->get_on_created_callback(entry_extension);
    //                 if(callback)
    //                 {
    //                     callback(entry_extension, entry.path, synced_entries, is_initial_listing);
    //                 }
    //             }
    //             break;
    //             case fs::watcher::entry_status::modified:
    //             {
    //                 auto callback = this->get_on_modified_callback(entry_extension);
    //                 if(callback)
    //                 {
    //                     const auto synced_entries = this->get_synced_entries(entry.path, is_directory);
    //                     callback(entry_extension, entry.path, synced_entries, is_initial_listing);
    //                 }
    //             }
    //             break;
    //             case fs::watcher::entry_status::removed:
    //             {
    //                 const auto callback = this->get_on_removed_callback(entry_extension);

    //                 if(callback)
    //                 {
    //                     const auto synced_entries = this->get_synced_entries(entry.path, is_directory);
    //                     callback(entry_extension, entry.path, synced_entries);
    //                 }
    //             }
    //             break;
    //             case fs::watcher::entry_status::renamed:
    //             {
    //                 const auto last_synced_entries = this->get_synced_entries(entry.last_path, is_directory);
    //                 const auto synced_entries = this->get_synced_entries(entry.path, is_directory);
    //                 auto callback = this->get_on_renamed_callback(entry_extension);

    //                 if(callback && synced_entries.size() == last_synced_entries.size())
    //                 {
    //                     std::vector<rename_pair_t> synced_renamed;
    //                     synced_renamed.reserve(synced_entries.size());

    //                     for(std::size_t i = 0; i < synced_entries.size(); ++i)
    //                     {
    //                         const auto& last_synced_entry = last_synced_entries[i];
    //                         const auto& synced_entry = synced_entries[i];
    //                         rename_pair_t p(last_synced_entry, synced_entry);
    //                         synced_renamed.emplace_back(std::move(p));
    //                     }
    //                     rename_pair_t p(entry.last_path, entry.path);
    //                     callback(entry_extension, p, synced_renamed);
    //                 }
    //             }

    //             break;
    //             default:
    //                 break;
    //         }
    //         completed++;
    //     }
    // };
    using namespace std::literals;
    const fs::path watch_dir = get_watch_path();
    watch_id_ = fs::watcher::watch(watch_dir, pattern_filter("*"), true, true, 500ms, on_change);
}

auto syncer::get_synced_entries(const fs::path& path, bool is_directory) -> std::vector<fs::path>
{
    std::vector<fs::path> synced_entries;
    auto synced_dir = get_synced_directory(path);

    if(is_directory)
    {
        synced_entries.emplace_back(std::move(synced_dir));
    }
    else
    {
        auto entry_path = path;
        std::string entry_extension;
        while(entry_path.has_extension())
        {
            auto ext = entry_path.extension().string() + entry_extension;
            entry_extension = ext;
            entry_path.replace_extension();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = mapping_.find(entry_extension);
            if(it != mapping_.end())
            {
                const auto& mapping = it->second;
                const auto& extensions = mapping.extensions;

                synced_entries.reserve(extensions.size());
                for(const auto& cache_ext : extensions)
                {
                    fs::path file = synced_dir / path.filename();
                    file.concat(cache_ext);

                    synced_entries.emplace_back(std::move(file));
                }
            }
        }
    }

    return synced_entries;
}

auto syncer::get_watch_path() -> fs::path
{
    std::lock_guard<std::mutex> lock(mutex_);
    const fs::path watch_dir = reference_dir_;
    return watch_dir;
}

auto syncer::get_synced_directory(const fs::path& path) -> fs::path
{
    fs::path result;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = fs::replace(path, reference_dir_, synced_dir_);
    }

    fs::error_code err;
    if(fs::is_directory(path, err) || !path.has_extension())
    {
        return result;
    }

    return result.parent_path();
}
} // namespace fs
