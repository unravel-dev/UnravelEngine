#ifndef FS_WATCHER_WTR_H
#define FS_WATCHER_WTR_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "../filesystem.h"
#include "../pattern_filter.h"
#include "../watcher.h"

#include <hpp/event.hpp>

namespace fs
{

class watcher_wtr
{
public:
    //-----------------------------------------------------------------------------
    //  Name : ~watcher_wtr ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    ~watcher_wtr();
    watcher_wtr() = default;

    //-----------------------------------------------------------------------------
    //  Name : watch_impl ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    auto watch_impl(const fs::path& path,
                    const pattern_filter& filter,
                    bool recursive,
                    bool initial_list,
                    watcher::clock_t::duration poll_interval,
                    watcher::notify_callback callback,
                    const std::string& watcher_name) -> std::uint64_t;

    void unwatch_impl(std::uint64_t key);

    void unwatch_all_impl();

    void pause();
    void resume();

    //-----------------------------------------------------------------------------
    //  Name : close ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void close();

    //-----------------------------------------------------------------------------
    //  Name : start ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void start();

    /// Mutex for the file watchers
    std::mutex mutex_;
    /// Atomic bool sync
    std::atomic<bool> watching_ = {false};

    std::condition_variable cv_;
    
    /// Implementation class for each watch
    class impl;
    std::map<std::uint64_t, std::shared_ptr<impl>> watchers_;
    
    /// Directory listeners (shared per directory)
    class directory_listener;
    std::map<fs::path, std::shared_ptr<directory_listener>> directory_listeners_;
    
};

} // namespace fs

#endif
