#include "watcher_wtr.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <utility>
#include <set>
#include <base/platform/thread.hpp>
#include <iostream>
#include <wtr/watcher.hpp>
namespace fs
{
using namespace std::literals;

namespace
{

void log_path(const fs::path& /*unused*/)
{
}

struct observed_changes
{
    std::vector<watcher::entry> entries;

    std::vector<size_t> created;
    std::vector<size_t> modified;
};

} // namespace

class watcher_wtr::directory_listener
{
public:
    directory_listener(const fs::path& path, bool recursive)
        : root_(path)
        , recursive_(recursive)
        , processing_timer_(std::chrono::steady_clock::now())
        , stop_processing_{false}
        , active_watcher_index_{0}
    {
        // Create initial wtr::watch instance in ring buffer
        // Capture watcher index to filter out events from inactive watchers
        file_watchers_[0] = std::make_unique<::wtr::watch>(path, [this, watcher_idx = 0](const ::wtr::event& event) -> void
        {
            handle_raw_event(event, watcher_idx);
        });
        
        // Start the processing thread
        processing_thread_ = std::thread([this]() -> void 
        { 
            processing_thread_func(); 
        });
    }

    ~directory_listener()
    {
        // Stop processing thread
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_processing_ = true;
        }
        processing_cv_.notify_all();
        
        if(processing_thread_.joinable())
        {
            processing_thread_.join();
        }
        
        // Process any remaining events before destruction
        process_events();
        
        // Close both watchers in ring buffer
        for(auto& watcher : file_watchers_)
        {
            if(watcher)
            {
                watcher->close();
            }
        }
    }

    auto get_path() const -> const fs::path&
    {
        return root_;
    }

    auto get_recursive() const -> bool
    {
        return recursive_;
    }

private:
    void advance_watcher()
    {

        int current_index = active_watcher_index_;

        auto& current_watcher = file_watchers_[current_index];
        if(current_watcher)
        {
            current_watcher->close();
            current_watcher.reset();
        }

        // Get next index in ring buffer (0 -> 1, 1 -> 0)
        int next_index = (active_watcher_index_ + 1) % 2;
        
        // Create new watcher at next index with the new path
        // This creates a fresh wtr::watch with correct inotify path mappings
        // Capture the watcher index so events from this watcher can be identified
        // Note: We don't lock here because this is called from handle_raw_event which already holds the lock
        file_watchers_[next_index] = std::make_unique<::wtr::watch>(root_, [this, watcher_idx = next_index](const ::wtr::event& event) -> void
        {
            handle_raw_event(event, watcher_idx);
        });
        
        // Switch to new watcher atomically
        active_watcher_index_ = next_index;

    }

    void handle_raw_event(const ::wtr::event& event, int watcher_idx)
    {
        // Skip events from inactive watcher (has stale path mappings)
        if(watcher_idx != active_watcher_index_)
        {
            return;
        }
        
        // Skip watcher events (status messages)
        if(event.path_type == ::wtr::event::path_type::watcher)
        {
            return;
        }

        // Skip other event types (owner, other)
        if(event.effect_type != ::wtr::event::effect_type::create &&
           event.effect_type != ::wtr::event::effect_type::modify &&
           event.effect_type != ::wtr::event::effect_type::destroy &&
           event.effect_type != ::wtr::event::effect_type::rename)
        {
            return;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);

        
        pending_events_.push_back(event);
        
        processing_timer_ = std::chrono::steady_clock::now();
        processing_cv_.notify_one();
    }

    void processing_thread_func()
    {
        platform::set_thread_name("fs::watcher");

        std::unique_lock<std::mutex> lock(mutex_);
        
        while(!stop_processing_.load())
        {
            // Wait for events or timeout (250ms debounce delay)
            auto timeout = std::chrono::milliseconds(250);
            
            processing_cv_.wait(lock, [this]() -> bool { return stop_processing_.load() || !pending_events_.empty(); });

            // Check if enough time has passed since last event
            auto now = std::chrono::steady_clock::now();
            auto time_since_last_event = now - processing_timer_;
            auto diff = timeout - time_since_last_event;

            while(diff > std::chrono::milliseconds(0))
            {
                lock.unlock();
                std::this_thread::sleep_for(diff);
                lock.lock();

                now = std::chrono::steady_clock::now();

                if(processing_timer_ > now)
                {
                    diff = std::chrono::milliseconds(0);
                    break;
                }
                time_since_last_event = now - processing_timer_;

                if(time_since_last_event > timeout)
                {
                    diff = std::chrono::milliseconds(0);
                    break;
                }
                diff = timeout - time_since_last_event;
            }
            
            if(!pending_events_.empty())
            {
                // Process events
                process_events_unlocked();
            }
        }
        
        // Process any remaining events before thread exits
        if(!pending_events_.empty())
        {
            process_events_unlocked();
        }
    }
    
    void process_events_unlocked()
    {
        if(pending_events_.empty())
        {
            return;
        }
        
        bool should_advance_watcher = false;
        // Check if this is a rename any directory
        for(const auto& event : pending_events_)
        {
            if(event.effect_type == ::wtr::event::effect_type::rename && 
               event.associated &&
               event.path_type == ::wtr::event::path_type::dir)
            {
                // Advance the watcher
                should_advance_watcher = true;
                break;
            }
        }
        
        std::vector<::wtr::event> flattened_events = flatten_raw_events(pending_events_);
        
        // Clear processed events
        pending_events_.clear();
        
        // Unlock mutex before emitting to avoid deadlock
        mutex_.unlock();

        if(should_advance_watcher)
        {
            advance_watcher();
        }

        if(!flattened_events.empty())
        {
            std::cout << "--------------------------------" << std::endl;

            std::cout << "Emitting " << flattened_events.size() << " raw events" << std::endl;
            for(const auto& event : flattened_events)
            {
                std::cout << "Event: " << event << std::endl;
            }
            std::cout << "--------------------------------" << std::endl;

            on_raw_events.emit(flattened_events);
        }
        mutex_.lock();
    }
    
    // Flatten raw events: create + modify -> create (with modify timestamp)
    // Multiple modify -> single modify (with latest timestamp)
    // rename + destroy (of old path) -> rename only
    static auto flatten_raw_events(const std::vector<::wtr::event>& raw_events) -> std::vector<::wtr::event>
    {
        // First pass: collect rename events and track old paths that were renamed
        std::set<fs::path> renamed_old_paths;
        std::vector<::wtr::event> rename_events;
        
        for(const auto& event : raw_events)
        {
            if(event.effect_type == ::wtr::event::effect_type::rename)
            {
                renamed_old_paths.insert(event.path_name);
                rename_events.push_back(event);
            }
        }
        
        // Group non-rename events by path
        std::map<fs::path, std::vector<::wtr::event>> path_to_events;
        
        for(const auto& event : raw_events)
        {
            // Skip rename events in this pass (handled separately)
            if(event.effect_type == ::wtr::event::effect_type::rename)
            {
                continue;
            }
            
            // Skip destroy events for paths that were renamed (they're part of the rename)
            if(event.effect_type == ::wtr::event::effect_type::destroy && renamed_old_paths.contains(event.path_name))
            {
                continue;
            }
            
            path_to_events[event.path_name].push_back(event);
        }
        
        std::vector<::wtr::event> flattened;
        
        // Process each path's events
        for(auto& [path, events] : path_to_events)
        {
            // Sort by effect_time
            std::sort(events.begin(), events.end(),
                [](const ::wtr::event& a, const ::wtr::event& b) -> bool
                {
                    return a.effect_time < b.effect_time;
                });
            
            // Check what types of events we have
            bool has_create = false;
            bool has_modify = false;
            bool has_destroy = false;
            ::wtr::event last_modify;
            ::wtr::event last_destroy;
            
            for(const auto& event : events)
            {
                if(event.effect_type == ::wtr::event::effect_type::create)
                {
                    has_create = true;
                }
                else if(event.effect_type == ::wtr::event::effect_type::modify)
                {
                    has_modify = true;
                    last_modify = event;
                }
                else if(event.effect_type == ::wtr::event::effect_type::destroy)
                {
                    has_destroy = true;
                    last_destroy = event;
                }
            }
            
            // If create + modify, convert to single create with modify timestamp
            if(has_create && has_modify)
            {
                // Find the create event
                for(auto& event : events)
                {
                    if(event.effect_type == ::wtr::event::effect_type::create)
                    {
                        // Use the effect_time from the last modify event
                        event.effect_time = last_modify.effect_time;
                        flattened.push_back(event);
                        break;
                    }
                }
            }
            // If multiple modify (without create), keep only the last one
            else if(has_modify && !has_create)
            {
                flattened.push_back(last_modify);
            }
            // If destroy, keep it (only one destroy per path expected, but keep last if multiple)
            else if(has_destroy)
            {
                flattened.push_back(last_destroy);
            }
            // If only create (no modify), keep it
            else if(has_create)
            {
                for(const auto& event : events)
                {
                    if(event.effect_type == ::wtr::event::effect_type::create)
                    {
                        flattened.push_back(event);
                        break;
                    }
                }
            }
        }
        
        // Add rename events (they're already flattened - one per old path)
        flattened.insert(flattened.end(), rename_events.begin(), rename_events.end());
        
        return flattened;
    }

public:
    /// Event that emits raw events to all connected impls
    hpp::event<void(const std::vector<::wtr::event>&)> on_raw_events;
    
    void process_events()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        process_events_unlocked();
    }

    /// Path being watched
    fs::path root_;
    /// Whether to watch recursively
    bool recursive_;
    /// Ring buffer of wtr::watch instances (2 slots for seamless recreation)
    std::array<std::unique_ptr<::wtr::watch>, 2> file_watchers_;
    /// Active watcher index in ring buffer (0 or 1)
    int active_watcher_index_;
    /// Raw events waiting to be processed
    std::vector<::wtr::event> pending_events_;
    /// Timer for debouncing event processing
    std::chrono::steady_clock::time_point processing_timer_;
    /// Mutex for thread safety
    std::mutex mutex_;
    /// Condition variable for processing thread
    std::condition_variable processing_cv_;
    /// Processing thread
    std::thread processing_thread_;
    /// Flag to stop processing thread
    std::atomic<bool> stop_processing_;
};

class watcher_wtr::impl
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
        , init_time_(watcher::clock_t::now())
        , init_time_timestamp_(std::chrono::system_clock::now())
        , watcher_name_(watcher_name)
    {
        // Initialize entries cache and optionally emit initial list
        initialize_entries(initial_list);

        // Connect to the listener's raw events
        slot_key_ = listener_->on_raw_events.connect([this](const std::vector<::wtr::event>& raw_events) -> void
        {
            handle_raw_events(raw_events);
        });
    }

    ~impl()
    {
        if(listener_)
        {
            listener_->on_raw_events.disconnect(slot_key_);
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

    void watch()
    {
        
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
    void initialize_entries(bool emit_initial_list)
    {
        // Iterate through the directory and populate entries_ cache
        fs::error_code err;
        std::vector<watcher::entry> initial_entries;
        
        if(recursive_)
        {
            for(auto& entry : fs::recursive_directory_iterator(path_, err))
            {
                bool filter_passed = filter_.should_include(entry.path());

                fs::error_code err2;
                fs::file_status file_status = fs::status(entry.path(), err2);
                auto file_type = file_status.type();
                if(filter_passed || (file_type == fs::file_type::directory))
                {
                    watcher::entry e;
                    e.path = entry.path();
                    e.last_path = entry.path();
                    e.status = watcher::entry_status::created;
                    
                    fs::error_code err3;
                    e.last_mod_time = fs::last_write_time(entry.path(), err3);
                    e.size = fs::file_size(entry.path(), err3);
                    e.type = file_type;
                    
                    // Add to cache
                    std::string key = e.path.string();
                    entries_[key] = e;
                    
                    if(emit_initial_list && filter_passed)
                    {
                        initial_entries.push_back(e);
                    }
                }
            }
        }
        else
        {
            for(auto& entry : fs::directory_iterator(path_, err))
            {
                bool filter_passed = filter_.should_include(entry.path());
                fs::error_code err2;
                fs::file_status file_status = fs::status(entry.path(), err2);
                auto file_type = file_status.type();
                if(filter_passed || (file_type == fs::file_type::directory))
                {
                    watcher::entry e;
                    e.path = entry.path();
                    e.last_path = entry.path();
                    e.status = watcher::entry_status::created;
                    
                    fs::error_code err3;
                    e.last_mod_time = fs::last_write_time(entry.path(), err3);
                    e.size = fs::file_size(entry.path(), err3);
                    e.type = file_type;
                    
                    // Add to cache
                    std::string key = e.path.string();
                    entries_[key] = e;
                    
                    if(emit_initial_list && filter_passed)
                    {
                        initial_entries.push_back(e);
                    }
                }
            }
        }
        
        // Emit initial list if requested
        if(emit_initial_list && !initial_entries.empty() && callback_)
        {
            callback_(initial_entries, true);
        }
    }

    void handle_raw_events(const std::vector<::wtr::event>& raw_events)
    {
        // Process and filter events according to this impl's filter
        observed_changes changes = process_and_filter_events(raw_events);
        
        if(changes.entries.empty())
        {
            return;
        }
        
        // Check if paused
        if(paused_)
        {
            // Buffer changes when paused
            buffered_changes_.insert(buffered_changes_.end(), changes.entries.begin(), changes.entries.end());
            return;
        }
        
        // Call callback
        if(callback_)
        {
            callback_(changes.entries, false);
        }
    }

    auto is_path_under_watch(const fs::path& event_path) const -> bool
    {
        // Path-level filtering: When reusing a parent listener, we receive events for
        // the entire parent directory tree. We must filter to only events under our specific path.
        // Example: If parent listener watches /home/default and we watch /home/default/test,
        // we should skip events from /home/default/other
        fs::error_code ec;
        auto canonical_event_path = fs::weakly_canonical(event_path, ec);
        auto canonical_watch_path = fs::weakly_canonical(path_, ec);
        
        // Check if event path is under our watched path
        auto rel = canonical_event_path.lexically_relative(canonical_watch_path);
        return !(rel.empty() || rel.string().substr(0, 2) == "..");
    }


    auto get_system_timestamp(const ::wtr::event& event) -> std::chrono::system_clock::time_point
    {

        // For these we need the last write time of the file, not the event processed time
        if(event.effect_type == ::wtr::event::effect_type::modify || event.effect_type == ::wtr::event::effect_type::create)
        {
            std::chrono::system_clock::time_point system_timestamp;
            fs::error_code err;
            auto file_timestamp = fs::last_write_time(event.path_name, err);
            if(!err)
            {
                system_timestamp = std::chrono::clock_cast<std::chrono::system_clock>(file_timestamp);
            }
            else 
            {
                system_timestamp = std::chrono::system_clock::now();
            }

            return system_timestamp;
        }
        auto effect_time = std::chrono::nanoseconds(event.effect_time);
        auto system_timestamp = std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(effect_time));
        return system_timestamp;
    }

    auto get_file_type(const ::wtr::event& event) -> fs::file_type
    {
        switch(event.path_type)
        {
            case ::wtr::event::path_type::dir:
            {
                return fs::file_type::directory;
            }
            case ::wtr::event::path_type::file:
            {
                return fs::file_type::regular;
            }
            case ::wtr::event::path_type::hard_link:
            {
                return fs::file_type::symlink;
            }
            case ::wtr::event::path_type::sym_link:
            {
                return fs::file_type::symlink;
            }
            default:
            {
                return fs::file_type::not_found;
            }
        }
        return fs::file_type::not_found;
    }
    
    auto process_and_filter_events(const std::vector<::wtr::event>& raw_events) -> observed_changes
    {
        observed_changes changes;

        if(!watcher_name_.empty())
        {
            std::cout << "--------------------------------" << std::endl;
        }

        if(entries_.size() > 1)
        {
            int a = 0;
            a++;

        }
        
        // Process events: renames are taken as-is, creates/destroys/modifications are collected for post-processing
        for(const auto& event : raw_events)
        {
            watcher::entry e;
            
            // Handle rename events separately - they are correct as-is from wtr
            if(event.effect_type == ::wtr::event::effect_type::rename && event.associated)
            {
                e.path = event.associated->path_name;
                e.last_path = event.path_name;
                
                e.type = get_file_type(event);

                if(e.type == fs::file_type::not_found)
                {
                    continue;
                }

                auto system_timestamp = get_system_timestamp(event);
                
                if(system_timestamp < init_time_timestamp_)
                {
                    continue;
                }
                
                // Get file info first to determine if it's a directory
                fs::error_code err;
                if(fs::exists(e.path, err))
                {
                    e.last_mod_time = fs::last_write_time(e.path, err);
                    e.size = fs::file_size(e.path, err);
                }
                else if(fs::exists(e.last_path, err))
                {
                    e.last_mod_time = fs::last_write_time(e.path, err);
                    e.size = fs::file_size(e.path, err);
                }
                
                // Check if we should apply the filter
                // Returns false for directory renames (parent or subdirectory)
                if(e.type == fs::file_type::regular)
                {
                    // Check if event is under our watched path (for parent listener reuse)
                    if(!is_path_under_watch(e.path) && !is_path_under_watch(e.last_path))
                    {
                        continue;
                    }

                    if(!filter_.should_include(e.path))
                    {
                        continue;
                    }
                }
                else if(e.type == fs::file_type::directory)
                {
                    // Directory rename - check if we need to update our watch path
                    // Only update path_ if it's a parent directory rename or exact match
                    fs::error_code ec;
                    auto canonical_old_path = fs::weakly_canonical(e.last_path, ec);
                    auto canonical_watch_path = fs::weakly_canonical(path_, ec);
                    
                    if(canonical_old_path == canonical_watch_path)
                    {
                        // Exact match - update watch path directly
                        // Example: watching /test/f1 and /test/f1 renamed to /test/f2
                        path_ = e.path;
                    }
                    else if(fs::is_any_parent_path(canonical_old_path, canonical_watch_path))
                    {
                        // Parent was renamed - update watch path accordingly
                        // Example: watching /test/f1/sub/*.png and /test/f1 renamed to /test/f2
                        // Update path_ from /test/f1/sub to /test/f2/sub
                        fs::path relative_path = canonical_watch_path.lexically_relative(canonical_old_path);
                        path_ = e.path / relative_path;
                    }
                    // Note: For subdirectory renames (e.g., watching /test/*.png and /test/subdir renamed),
                    // we don't update path_ - only process_modifications will update cached entries
                }
                
                // Check if old path was in cache
                if(entries_.contains(e.last_path.string()))
                {
                    e.status = watcher::entry_status::renamed;
                    
                    // Remove old path from cache and add new path
                    std::string old_key = e.last_path.string();
                    std::string new_key = e.path.string();
                    entries_.erase(old_key);
                    entries_[new_key] = e;
                    
                    changes.entries.push_back(e);
                }
                else if(e.type == fs::file_type::directory)
                {
                    // Directory rename not in cache - pass to process_modifications
                    // to update all cached child entries
                    e.status = watcher::entry_status::renamed;
                    changes.entries.push_back(e);
                }
                else
                {
                    // Old path not in cache, treat as create
                    e.status = watcher::entry_status::created;
                    e.last_path = e.path;
                    
                    std::string new_key = e.path.string();
                    entries_[new_key] = e;
                    
                    changes.entries.push_back(e);
                    changes.created.push_back(changes.entries.size() - 1);
                }
                
                continue;
            }
            
            // For non-rename events, use path_name directly
            e.path = event.path_name;
            e.last_path = event.path_name;
            e.type = get_file_type(event);

            if(e.type == fs::file_type::not_found)
            {
                continue;
            }

            // Check if event is under our watched path (for parent listener reuse)
            if(!is_path_under_watch(e.path))
            {
                continue;
            }
            
            // Check filter
            if(!filter_.should_include(e.path))
            {
                continue;
            }
            
            // Check timestamp
            auto system_timestamp = get_system_timestamp(event);
            
            if(system_timestamp < init_time_timestamp_)
            {
                continue;
            }
            
            // Process create/destroy/modify events
            switch(event.effect_type)
            {
                case ::wtr::event::effect_type::create:
                {
                    std::string key = e.path.string();

                    // Get file info from filesystem
                    fs::error_code err;
                    if(fs::exists(e.path, err))
                    {
                        e.last_mod_time = fs::last_write_time(e.path, err);
                        e.size = fs::file_size(e.path, err);
                        e.status = watcher::entry_status::created;
                        
                        changes.entries.push_back(e);
                        changes.created.push_back(changes.entries.size() - 1);

                        entries_[key] = e;

                    }
                    break;
                }
                case ::wtr::event::effect_type::destroy:
                {
                    // Get file info from cache and add to cache temporarily for rename detection
                    std::string key = e.path.string();
                    auto it = entries_.find(key);
                    if(it != entries_.end())
                    {
                        e.last_mod_time = it->second.last_mod_time;
                        e.size = it->second.size;
                        e.status = watcher::entry_status::removed;
                        
                        // Add to cache temporarily (will be removed if matched with rename)
                        entries_[key] = e;

                        changes.entries.push_back(e);

                    }
                    break;
                }
                case ::wtr::event::effect_type::modify:
                {
                    // Get file info from filesystem
                    std::string key = e.path.string();
                    fs::error_code err;
                    if(fs::exists(e.path, err))
                    {
                        e.last_mod_time = fs::last_write_time(e.path, err);
                        e.size = fs::file_size(e.path, err);
                        e.status = watcher::entry_status::modified;
                        
                        changes.entries.push_back(e);
                        changes.modified.push_back(changes.entries.size() - 1);

                        entries_[key] = e;
                    }
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
        
        // Post-process: detect renames from create+destroy pairs (cross-folder moves)
        this->process_modifications(entries_, changes);
        if(changes.entries.size() > 0)
        {
            std::cout << "--------------------------------" << std::endl;
            std::cout << "Watcher : " << watcher_name_ << std::endl;
            std::cout << "Path: " << path_.string() << std::endl;
            std::cout << "Recursive: " << recursive_ << std::endl;
            std::cout << "Filter Exclude Patterns: ";
            for(const auto& pattern : filter_.get_exclude_patterns())
            {
                std::cout << pattern.get_pattern() << " ";
            }
            std::cout << std::endl;
            std::cout << "Filter Include Patterns: ";
            for(const auto& pattern : filter_.get_include_patterns())
            {
                std::cout << pattern.get_pattern() << " ";
            }
            std::cout << std::endl;
            std::cout << "Changes: " << changes.entries.size() << std::endl;
            for(const auto& entry : changes.entries)
            {
                std::cout << "Status: " << to_string(entry) << std::endl;
            }
            std::cout << "--------------------------------" << std::endl;

        }
        return changes;
    }

    
    static auto get_original_path(const fs::path& old_path, const fs::path& renamed_path, const fs::path& new_path) -> fs::path
    {
        fs::path relative_path = fs::relative(new_path, renamed_path);
        fs::path original_path = old_path / relative_path;
        fs::error_code err;
        return fs::weakly_canonical(original_path, err);
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
    }

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

                return true;
            }
        }
        return false;
    }

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

                    if(d <= std::chrono::milliseconds(10))
                    {
                        bool same_extensions = check_if_same_extension(e.path, fi.path);
                        if(same_extensions)
                        {
                            if(d <= std::chrono::milliseconds(0))
                            {
                                e.status = watcher::entry_status::renamed;
                                e.last_path = fi.path;

                                // remove the cached old path entry
                                container.erase(it);
                                return true;
                            }

                            std::cout << "Same file modification time difference: " << std::endl;
                            std::cout << to_string(e) << std::endl;
                            std::cout << to_string(fi) << std::endl;
                            std::cout << "Difference: " << d.count() << " milliseconds" << std::endl;
                            std::cout << "--------------------------------" << std::endl;
                        }
                    }
                }
            }

            it++;
        }

        return false;
    }

    template<typename Container>
    static void prune_removed_entries(Container& container)
    {

        auto it = std::begin(container);
        while(it != std::end(container))
        {
            auto& fi = it->second;
            fs::error_code err;
            // if(!fs::exists(fi.path, err))
            if(fi.status == watcher::entry_status::removed)
            {
                it = container.erase(it);
            }
            else
            {
                it++;
            }
        }
    }

    template<typename Container>
    void process_modifications(Container& old_entries, observed_changes& changes)
    {
        if(changes.entries.empty())
        {
            return;
        }

        std::vector<size_t> renamed_dirs;

        // First pass: detect renames from create/destroy pairs
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
        
        // Second pass: for each renamed directory in changes, update all child paths in the cache
        for(const auto& entry : changes.entries)
        {
            // Skip if not a renamed directory
            if(entry.status != watcher::entry_status::renamed || entry.type != fs::file_type::directory)
            {
                continue;
            }
            
            // Find all entries in cache that are children of the old path
            std::vector<std::pair<std::string, watcher::entry>> children_to_update;
            
            for(auto& [key, cached_entry] : old_entries)
            {
                // Check if this cached entry is a child of the OLD directory path
                // (cached entries still have their old paths)
                if(fs::is_any_parent_path(entry.last_path, cached_entry.path))
                {
                    // Calculate the new path for this child
                    // get_original_path(A, B, C) returns: A / relative(C, B)
                    // We want: new_dir / relative(old_child, old_dir)
                    watcher::entry updated_entry = cached_entry;
                    updated_entry.last_path = cached_entry.path; // old path
                    updated_entry.path = get_original_path(entry.path, entry.last_path, cached_entry.path); // new path
                    updated_entry.status = watcher::entry_status::renamed;
                    
                    // Store for updating after iteration
                    children_to_update.push_back({key, updated_entry});
                }
            }
            
            // Update cache: remove old paths and add new paths
            for(const auto& [old_key, updated_entry] : children_to_update)
            {
                old_entries.erase(old_key);
                std::string new_key = updated_entry.path.string();
                old_entries[new_key] = updated_entry;

                // Add to changes
                changes.entries.push_back(updated_entry);
            }
        }
        
        prune_removed_entries(old_entries);
    }

    fs::path path_;
    pattern_filter filter_;
    bool recursive_;
    watcher::notify_callback callback_;
    std::shared_ptr<directory_listener> listener_;

    std::chrono::steady_clock::time_point init_time_;
    std::chrono::system_clock::time_point init_time_timestamp_;
    uint64_t slot_key_ = 0;
    std::atomic<bool> paused_ = false;
    std::vector<watcher::entry> buffered_changes_;
    /// Cache watched files
    std::map<std::string, watcher::entry> entries_;
    std::string watcher_name_;
};

watcher_wtr::~watcher_wtr()
{
    close();
}

void watcher_wtr::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto& kvp : watchers_)
    {
        kvp.second->pause();
    }
}

void watcher_wtr::resume()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto& kvp : watchers_)
    {
        kvp.second->resume();
    }
}

void watcher_wtr::close()
{
    // Remove all watchers
    unwatch_all_impl();
    
    // Clear directory listeners
    {
        std::lock_guard<std::mutex> lock(mutex_);
        directory_listeners_.clear();
    }
}

void watcher_wtr::start()
{
    watching_ = true;
    
}

auto watcher_wtr::watch_impl(const fs::path& path,
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
                listener = std::make_shared<directory_listener>(abs_path, recursive);
                directory_listeners_[abs_path] = listener;
            }
        }
    }
    static std::atomic<std::uint64_t> free_id = {1};
    auto key = free_id++;
    auto impl = std::make_shared<watcher_wtr::impl>(path, filter, recursive, initial_list, poll_interval, std::move(callback), listener, watcher_name);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        watchers_[key] = impl;
    }
    return key;
}

void watcher_wtr::unwatch_impl(std::uint64_t key)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        watchers_.erase(key);
    }
    std::set<std::string> stale_listeners;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& [path, listener] : directory_listeners_)
        {
            if(listener.use_count() == 1)
            {
                stale_listeners.insert(path.string());
            }
        }
    }
    for(auto& path : stale_listeners)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        directory_listeners_.erase(path);
    }
}

void watcher_wtr::unwatch_all_impl()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        watchers_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        directory_listeners_.clear();
    }
}


} // namespace fs
