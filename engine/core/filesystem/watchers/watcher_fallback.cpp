#include "watcher_fallback.h"
#include <filesystem>
#include <set>
#include <utility>
#include <base/platform/thread.hpp>
#include <hpp/event.hpp>
namespace fs
{
using namespace std::literals;

namespace
{

void log_path(const fs::path& /*unused*/)
{
}

} // namespace

class watcher_fallback::directory_listener
{
public:

    struct observed_changes
    {
        std::vector<watcher::entry> entries;

        std::vector<size_t> created;
        std::vector<size_t> modified;

        void append(const observed_changes& rhs)
        {

            for(const auto& e : rhs.entries)
            {
                entries.emplace_back(e);
            }

            auto created_sz_before = created.size();
            for(auto idx : rhs.created)
            {
                created.emplace_back(created_sz_before + idx);
            }

            auto modified_sz_before = modified.size();
            for(auto idx : rhs.modified)
            {
                modified.emplace_back(modified_sz_before + idx);
            }
        }

        void append(observed_changes&& rhs)
        {

            for(auto& e : rhs.entries)
            {
                entries.emplace_back(std::move(e));
            }

            auto created_sz_before = created.size();
            for(auto idx : rhs.created)
            {
                created.emplace_back(created_sz_before + idx);
            }

            auto modified_sz_before = modified.size();
            for(auto idx : rhs.modified)
            {
                modified.emplace_back(modified_sz_before + idx);
            }

            rhs = {};
        }
    };
    //-----------------------------------------------------------------------------
    //  Name : impl ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    directory_listener(const fs::path& path,
                       bool recursive,
                       watcher::clock_t::duration poll_interval)
        : root_(path)
        , poll_interval_(poll_interval)
        , recursive_(recursive)
        , init_time_timestamp_(std::chrono::system_clock::now())
    {
        observed_changes changes;
        if(recursive_)
        {
            fs::error_code err;
            for(auto& entry : fs::recursive_directory_iterator(root_, err))
            {
                poll_entry(entry, changes);
            }
        }
        else
        {
            fs::error_code err;
            for(auto& entry : fs::directory_iterator(root_, err))
            {
                poll_entry(entry, changes);
            }
        }
    }

    void pause()
    {
        paused_ = true;
    }

    void resume()
    {
        paused_ = false;
    }

    //-----------------------------------------------------------------------------
    //  Name : watch ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void watch()
    {
        observed_changes changes;
        bool paused = paused_;
        if(!paused)
        {
            if(!buffered_changes_.entries.empty())
            {
                std::swap(changes, buffered_changes_);
            }
        }
        if(recursive_)
        {
            fs::error_code err;
            for(auto& entry : fs::recursive_directory_iterator(root_, err))
            {
                poll_entry(entry, changes);
            }
        }
        else
        {
            fs::error_code err;
            for(auto& entry : fs::directory_iterator(root_, err))
            {
                poll_entry(entry, changes);
            }
        }
        if(paused)
        {
            if(!changes.entries.empty())
            {
                buffered_changes_.append(std::move(changes));
            }
        }
        else
        {
            process_modifications(entries_, changes);
            if(!changes.entries.empty())
            {
                on_changes.emit(changes.entries);
            }
        }
    }

    static auto get_original_path(const fs::path& old_path, const fs::path& renamed_path, const fs::path& new_path) -> fs::path
    {
        fs::path relative_path = fs::relative(new_path, renamed_path);
        fs::path original_path = old_path / relative_path;
        return original_path;
    }

    static auto check_if_same_extension(const fs::path& p1, const fs::path& p2) -> bool
    {
        bool same_extensions = true;

        auto ep = p1;
        auto fp = p2;

        while(ep.has_extension() || fp.has_extension())
        {
            same_extensions &= ep.extension() == fp.extension();
            ep = ep.stem();
            fp = fp.stem();
        }

        return same_extensions;
    };

    static auto check_if_parent_dir_was_renamed(const std::vector<size_t>& renamed_dirs, const std::vector<watcher::entry>& entries, watcher::entry& e) -> bool
    {
        //check if parent_dir was renamed
        for(const auto& renamed_idx : renamed_dirs)
        {
            const auto& renamed_e = entries[renamed_idx];

            if(fs::is_any_parent_path(renamed_e.path, e.path))
            {
                e.status = watcher::entry_status::renamed;
                e.last_path = get_original_path(renamed_e.last_path, renamed_e.path, e.path);
                e.event_time = std::chrono::system_clock::now();

                return true;
            }
        }
        return false;
    };


    template<typename Container>
    static auto check_if_renamed(watcher::entry& e, Container& container) -> bool
    {

        auto it = std::begin(container);
        while(it != std::end(container))
        {
            auto& fi = it->second;
            fs::error_code err;
            if(!fs::exists(fi.path, err))
            {

                if(e.size == fi.size)
                {
                    auto diff = (e.last_mod_time - fi.last_mod_time);
                    auto d = std::chrono::duration_cast<std::chrono::milliseconds>(diff);

                    if(d <= std::chrono::milliseconds(0))
                    {
                        bool same_extensions = check_if_same_extension(e.path, fi.path);
                        if(same_extensions)
                        {
                            e.status = watcher::entry_status::renamed;
                            e.last_path = fi.path;
                            e.event_time = std::chrono::system_clock::now();

                                   // remove the cached old path entry
                            container.erase(it);
                            return true;
                        }
                    }

                }

            }

            it++;
        }

        return false;

    };

    template<typename Container>
    static void check_for_removed(std::vector<watcher::entry>& entries, Container& container)
    {

        auto it = std::begin(container);
        while(it != std::end(container))
        {
            auto& fi = it->second;
            fs::error_code err;
            if(!fs::exists(fi.path, err))
            {
                fi.status = watcher::entry_status::removed;
                fi.event_time = std::chrono::system_clock::now();
                entries.push_back(fi);

                it = container.erase(it);
            }
            else
            {
                it++;
            }
        }
    }


    template<typename Container>
    static void process_modifications(Container& old_entries,
                                      observed_changes& changes)
    {
        using namespace std::literals;


        std::vector<size_t> renamed_dirs;

        for(auto idx : changes.created)
        {
            auto& e = changes.entries[idx];

            //check if parent_dir was renamed
            if(check_if_parent_dir_was_renamed(renamed_dirs, changes.entries, e))
            {

                // remove the cached old path entry
                old_entries.erase(e.last_path.string());
                continue;
            }

            // check for rename heuristic
            if(check_if_renamed(e, old_entries))
            {
                if(e.type == fs::file_type::directory)
                {
                    renamed_dirs.emplace_back(idx);
                }
                continue;
            }

        }
        check_for_removed(changes.entries, old_entries);
    }
  
    //-----------------------------------------------------------------------------
    //  Name : poll_entry ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void poll_entry(const fs::directory_entry& entry,
                    observed_changes& changes)
    {
        // get the last modification time
        fs::error_code err;
        auto time = entry.last_write_time( err);
        auto size = entry.file_size( err);
        fs::file_status status = entry.status( err);
        // add a new modification time to the map
        std::string key = entry.path().string();
        auto it = entries_.find(key);
        if(it != entries_.end())
        {
            auto& fi = it->second;

            if(fi.last_mod_time != time || fi.size != size || fi.type != status.type())
            {
                fi.size = size;
                fi.last_mod_time = time;
                fi.status = watcher::entry_status::modified;
                fi.type = status.type();
                
                // on modify set the event time to the last modification time.
                // since modifications are always observed while watching, we can use the last modification time.
                auto last_mod_time = fs::filetime_to_system_clock(time);
                fi.event_time = last_mod_time;

                changes.entries.push_back(fi);
                changes.modified.push_back(changes.entries.size() - 1);
            }
            else
            {
                fi.status = watcher::entry_status::unmodified;
                fi.type = status.type();
            }
        }
        else
        {
            // or compare with an older one
            auto& fi = entries_[key];
            fi.path = entry.path();
            fi.last_path = entry.path();
            fi.last_mod_time = time;
            fi.status = watcher::entry_status::created;
            fi.size = size;
            fi.type = status.type();

            // on create set the event time to the current time
            fi.event_time = std::chrono::system_clock::now();
            changes.entries.push_back(fi);
            changes.created.push_back(changes.entries.size() - 1);
        }
    }

    /// Event that emits changes to all connected impls
    hpp::event<void(const std::vector<watcher::entry>&)> on_changes;
    
    auto get_path() const -> const fs::path&
    {
        return root_;
    }
    
    auto get_recursive() const -> bool
    {
        return recursive_;
    }

private:
    friend class watcher_fallback;

    /// Path to watch
    fs::path root_;
    /// Cache watched files
    std::map<std::string, watcher::entry> entries_;

    std::chrono::system_clock::time_point init_time_timestamp_;
    ///
    watcher::clock_t::duration poll_interval_ = 500ms;

    watcher::clock_t::time_point last_poll_ = watcher::clock_t::now();
    ///
    bool recursive_ = false;

    std::atomic<bool> paused_ = {false};

    observed_changes buffered_changes_;
};

class watcher_fallback::impl
{
public:
    impl(const fs::path& path,
         const pattern_filter& filter,
         bool recursive,
         bool initial_list,
         watcher::clock_t::duration poll_interval,
         watcher::notify_callback callback,
         std::shared_ptr<directory_listener> listener,
         const std::string& watcher_name)
        : path_(path)
        , filter_(filter)
        , recursive_(recursive)
        , callback_(std::move(callback))
        , listener_(std::move(listener))
        , init_time_timestamp_(std::chrono::system_clock::now())
        , watcher_name_(watcher_name)
    {
        // Initialize entries cache and optionally emit initial list
        initialize_entries(initial_list);
        
        // Connect to the listener's changes
        slot_key_ = listener_->on_changes.connect([this](const std::vector<watcher::entry>& changes) -> void
        {
            handle_changes(changes);
        });
    }
    
    ~impl()
    {
        if(listener_)
        {
            listener_->on_changes.disconnect(slot_key_);
        }
    }
    
    void pause()
    {
        paused_ = true;
    }
    
    void resume()
    {
        paused_ = false;
        // Process buffered changes
        if(!buffered_changes_.empty())
        {
            std::vector<watcher::entry> changes_to_process;
            std::swap(changes_to_process, buffered_changes_);
            
            if(!changes_to_process.empty() && callback_)
            {
                callback_(changes_to_process, false);
            }
        }
    }
    
    auto get_path() const -> const fs::path&
    {
        return path_;
    }
    
    auto get_listener() const -> std::shared_ptr<directory_listener>
    {
        return listener_;
    }

private:
    void poll_entry(const fs::directory_entry& entry, std::vector<watcher::entry>& initial_entries, bool emit_initial_list)
    {
        bool filter_passed = filter_.should_include(entry.path());
        fs::error_code err2;
        fs::file_status file_status = entry.status(err2);

        auto file_type = file_status.type();
        if(filter_passed || (file_type == fs::file_type::directory))
        {
            watcher::entry e;
            e.path = entry.path();
            e.last_path = entry.path();
            e.status = watcher::entry_status::created;
            
            fs::error_code err3;
            e.last_mod_time = entry.last_write_time(err3);
            e.size = entry.file_size(err3);
            e.type = file_type;
            
            // on create set the event time to the current time
            e.event_time = std::chrono::system_clock::now();
            
            // Add to cache
            std::string key = e.path.string();
            
            if(emit_initial_list && filter_passed)
            {
                initial_entries.push_back(e);
            }
        }

    }

    void initialize_entries(bool emit_initial_list)
    {
        // Iterate through the directory and populate entries_ cache
        fs::error_code err;
        std::vector<watcher::entry> initial_entries;
        
        if(recursive_)
        {
            for(auto& entry : fs::recursive_directory_iterator(path_, err))
            {
                poll_entry(entry, initial_entries, emit_initial_list); 
            }
        }
        else
        {
            for(auto& entry : fs::directory_iterator(path_, err))
            {
                poll_entry(entry, initial_entries, emit_initial_list);
            }
        }
        
        // Emit initial list if requested
        if(emit_initial_list && !initial_entries.empty() && callback_)
        {
            callback_(initial_entries, true);
        }
    }

    auto get_system_timestamp(const watcher::entry& entry) -> std::chrono::system_clock::time_point
    {
        // if(entry.status == watcher::entry_status::renamed || entry.status == watcher::entry_status::removed)
        {
            return entry.event_time;
        }
        // return fs::filetime_to_system_clock(entry.last_mod_time);

    }
    
    void handle_changes(const std::vector<watcher::entry>& changes)
    {
        // Filter changes according to this impl's filter and path
        std::vector<watcher::entry> filtered_changes;
        
        for(const auto& entry : changes)
        {
            // Check if event is under our watched path (for parent listener reuse)
            if(!is_path_under_watch(entry.path))
            {
                continue;
            }
            
            // Check filter
            if(!filter_.should_include(entry.path))
            {
                continue;
            }
            
            // Check timestamp
            auto system_timestamp = get_system_timestamp(entry);
            
            if(system_timestamp < init_time_timestamp_)
            {
                continue;
            }
            
            filtered_changes.push_back(entry);
        }
        
        if(filtered_changes.empty())
        {
            return;
        }
        
        // Check if paused
        if(paused_)
        {
            // Buffer changes when paused
            buffered_changes_.insert(buffered_changes_.end(), filtered_changes.begin(), filtered_changes.end());
            return;
        }
        
        // Call callback
        if(callback_)
        {
            callback_(filtered_changes, false);
        }
    }
    
    auto is_path_under_watch(const fs::path& event_path) const -> bool
    {
        // Path-level filtering: When reusing a parent listener, we receive events for
        // the entire parent directory tree. We must filter to only events under our specific path.
        fs::error_code ec;
        auto canonical_event_path = fs::weakly_canonical(event_path, ec);
        auto canonical_watch_path = fs::weakly_canonical(path_, ec);
        
        // Check if event path is under our watched path
        auto rel = canonical_event_path.lexically_relative(canonical_watch_path);
        return !(rel.empty() || rel.string().substr(0, 2) == "..");
    }
    
    fs::path path_;
    pattern_filter filter_;
    bool recursive_;
    watcher::notify_callback callback_;
    std::shared_ptr<directory_listener> listener_;
    
    std::chrono::system_clock::time_point init_time_timestamp_;
    uint64_t slot_key_ = 0;
    std::atomic<bool> paused_ = false;
    std::vector<watcher::entry> buffered_changes_;
    std::string watcher_name_;
};

watcher_fallback::~watcher_fallback()
{
    close();
}

void watcher_fallback::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto& kvp : watchers_)
    {
        auto& w = kvp.second;
        w->pause();
    }
}

void watcher_fallback::resume()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto& kvp : watchers_)
    {
        auto& w = kvp.second;
        w->resume();
    }
}

void watcher_fallback::wait_all(watcher::clock_t::duration duration)
{
    cv_.notify_all();
    std::this_thread::sleep_for(duration);
}
void watcher_fallback::close()
{
    // stop the thread
    watching_ = false;
    // remove all watchers
    unwatch_all_impl();
    
    if(thread_.joinable())
    {
        thread_.join();
    }
}

void watcher_fallback::start()
{
    watching_ = true;
    thread_ = std::thread(
        [this]() -> void
        {
            platform::set_thread_name("fs::watcher");
            // keep watching for modifications every ms milliseconds
            using namespace std::literals;
            while(watching_)
            {
                watcher::clock_t::duration sleep_time = 99999h;

                // iterate through each directory listener and check for modification
                std::map<fs::path, std::shared_ptr<directory_listener>> listeners;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    listeners = directory_listeners_;
                }

                for(auto& pair : listeners)
                {
                    auto listener = pair.second;

                    auto now = watcher::clock_t::now();

                    auto diff = (listener->last_poll_ + listener->poll_interval_) - now;
                    if(diff <= watcher::clock_t::duration(0))
                    {
                        listener->watch();
                        listener->last_poll_ = now;

                        sleep_time = std::min(sleep_time, listener->poll_interval_);
                    }
                    else
                    {
                        sleep_time = std::min(sleep_time, diff);
                    }
                }

                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, sleep_time);
            }
        });
}

auto watcher_fallback::watch_impl(const fs::path& path,
                              const pattern_filter& filter,
                              bool recursive,
                              bool initial_list,
                              watcher::clock_t::duration poll_interval,
                              watcher::notify_callback callback,
                              const std::string& watcher_name) -> std::uint64_t
{
    if(!callback)
    {
        return 0;
    }

    std::shared_ptr<directory_listener> listener;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fs::error_code err;
        fs::path abs_path = fs::absolute(path, err);
        auto it = directory_listeners_.find(abs_path);
        if(it != directory_listeners_.end())
        {
            listener = it->second;
        }
        else
        {
            for(auto& [watched_path, existing_listener] : directory_listeners_)
            {
                if(existing_listener->get_recursive() && fs::is_any_parent_path(watched_path, abs_path))
                {
                    listener = existing_listener;
                    break;
                }
            }
            if(!listener)
            {
                listener = std::make_shared<directory_listener>(abs_path, recursive, poll_interval);
                directory_listeners_[abs_path] = listener;
            }
        }
    }
    static std::atomic<std::uint64_t> free_id = {1};
    auto key = free_id++;
    auto impl = std::make_shared<watcher_fallback::impl>(path, filter, recursive, initial_list, poll_interval, std::move(callback), listener, watcher_name);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        watchers_[key] = impl;
    }
    cv_.notify_all();
    return key;
}

void watcher_fallback::unwatch_impl(std::uint64_t key)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        watchers_.erase(key);
        
        std::set<fs::path> stale_listeners;
        {
            for(auto& [path, listener] : directory_listeners_)
            {
                if(listener.use_count() == 1)
                {
                    stale_listeners.insert(path);
                }
            }
        }
        for(const auto& path : stale_listeners)
        {
            directory_listeners_.erase(path);
        }
    }
    cv_.notify_all();
}

void watcher_fallback::unwatch_all_impl()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        watchers_.clear();
        directory_listeners_.clear();
    }
    cv_.notify_all();
}


} // namespace fs
