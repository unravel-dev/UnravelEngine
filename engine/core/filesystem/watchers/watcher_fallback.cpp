#include "watcher_fallback.h"
#include <sstream>
#include <utility>
#include <base/platform/thread.hpp>
namespace fs
{
using namespace std::literals;

namespace
{

void log_path(const fs::path& /*unused*/)
{
}

} // namespace

class watcher_fallback::impl
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
    impl(const fs::path& path,
         const pattern_filter& filter,
         bool recursive,
         bool initial_list,
         watcher::clock_t::duration poll_interval,
         watcher::notify_callback list_callback,
         const std::string& watcher_name)
        : filter_(filter)
        , callback_(std::move(list_callback))
        , poll_interval_(poll_interval)
        , recursive_(recursive)
        , watcher_name_(watcher_name)
    {
        root_ = path;
        observed_changes changes;
        if(recursive_)
        {
            fs::error_code err;
            for(auto& entry : fs::recursive_directory_iterator(root_, err))
            {
                if(filter_.should_include(entry.path()))
                    poll_entry(entry.path(), changes);
            }
        }
        else
        {
            fs::error_code err;
            for(auto& entry : fs::directory_iterator(root_, err))
            {
                if(filter_.should_include(entry.path()))
                    poll_entry(entry.path(), changes);
            }
        }
        if(initial_list)
        {
            if(!changes.entries.empty() && callback_)
            {
                callback_(changes.entries, true);
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
                if(filter_.should_include(entry.path()))
                    poll_entry(entry.path(), changes);
            }
        }
        else
        {
            fs::error_code err;
            for(auto& entry : fs::directory_iterator(root_, err))
            {
                if(filter_.should_include(entry.path()))
                    poll_entry(entry.path(), changes);
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
            if(!changes.entries.empty() && callback_)
            {
                callback_(changes.entries, false);
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
    void poll_entry(const fs::path& path,
                    observed_changes& changes)
    {
        // get the last modification time
        fs::error_code err;
        auto time = fs::last_write_time(path, err);
        auto size = fs::file_size(path, err);
        fs::file_status status = fs::status(path, err);
        // add a new modification time to the map
        std::string key = path.string();
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
            fi.path = path;
            fi.last_path = path;
            fi.last_mod_time = time;
            fi.status = watcher::entry_status::created;
            fi.size = size;
            fi.type = status.type();

            changes.entries.push_back(fi);
            changes.created.push_back(changes.entries.size() - 1);
        }
    }

protected:
    friend class watcher_fallback;


    /// Path to watch
    fs::path root_;
    /// Filter applied
    pattern_filter filter_;
    /// Callback for list of modifications
    watcher::notify_callback callback_;
    /// Cache watched files
    std::map<std::string, watcher::entry> entries_;
    ///
    watcher::clock_t::duration poll_interval_ = 500ms;

    watcher::clock_t::time_point last_poll_ = watcher::clock_t::now();
    ///
    bool recursive_ = false;

    std::atomic<bool> paused_ = {false};

    observed_changes buffered_changes_;

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

                // iterate through each watcher and check for modification
                std::map<std::uint64_t, std::shared_ptr<impl>> watchers;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    watchers = watchers_;
                }

                for(auto& pair : watchers)
                {
                    auto watcher = pair.second;

                    auto now = watcher::clock_t::now();

                    auto diff = (watcher->last_poll_ + watcher->poll_interval_) - now;
                    if(diff <= watcher::clock_t::duration(0))
                    {
                        watcher->watch();
                        watcher->last_poll_ = now;

                        sleep_time = std::min(sleep_time, watcher->poll_interval_);
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
    static std::atomic<std::uint64_t> free_id = {1};
    auto key = free_id++;
    {
        auto imp = std::make_shared<impl>(path, filter, recursive, initial_list, poll_interval, std::move(callback), watcher_name);
        std::lock_guard<std::mutex> lock(mutex_);
        watchers_.emplace(key, std::move(imp));
    }
    cv_.notify_all();
    return key;
}

void watcher_fallback::unwatch_impl(std::uint64_t key)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        watchers_.erase(key);
    }
    cv_.notify_all();
}

void watcher_fallback::unwatch_all_impl()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        watchers_.clear();
    }
    cv_.notify_all();
}


} // namespace fs
