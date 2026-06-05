#ifndef FS_WATCHER_FALLBACK_H
#define FS_WATCHER_FALLBACK_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "../filesystem.h"
#include "../pattern_filter.h"
#include "../watcher.h"

namespace fs
{

class watcher_fallback
{
public:
    //-----------------------------------------------------------------------------
    //  Name : ~watcher_fallback ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    ~watcher_fallback();
    watcher_fallback() = default;

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

    void wait_all(watcher::clock_t::duration duration);

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

    std::atomic<bool> globally_paused_ = {false};

    std::condition_variable cv_;
    /// Thread that polls for changes
    std::thread thread_;
    /// Implementation class for each watch
    class impl;
    std::map<std::uint64_t, std::shared_ptr<impl>> watchers_;
    
    /// Directory listeners (shared per directory)
    class directory_listener;
    std::map<fs::path, std::shared_ptr<directory_listener>> directory_listeners_;

    void prune_stale_listeners();
};

} // namespace fs

#endif
