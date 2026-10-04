#ifndef FS_WATCHER_FALLBACK_H
#define FS_WATCHER_FALLBACK_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../filesystem.h"
#include "../pattern_filter.h"
#include "../watcher.h"

namespace fs
{

/**
 * @brief Watcher that finds changes by diffing full directory scans.
 *
 * One directory_listener scans each watched root on the "fs::watcher" thread and diffs the scan
 * against its cache; every watch on that root (or under a recursive root) filters the result.
 * A scan runs shortly after the OS reports a change under the root (see change_trigger) and at
 * least once per poll interval, so platforms and filesystems without notifications still work.
 *
 * Callbacks run on the "fs::watcher" thread, one at a time. unwatch returns only once a running
 * callback of that watch finished, and may be called from inside the callback.
 */
class watcher_fallback
{
public:
    watcher_fallback() = default;
    ~watcher_fallback();
    watcher_fallback(const watcher_fallback&) = delete;
    auto operator=(const watcher_fallback&) -> watcher_fallback& = delete;

    auto watch_impl(const fs::path& path,
                    const pattern_filter& filter,
                    bool recursive,
                    bool initial_list,
                    watcher::clock_t::duration poll_interval,
                    watcher::notify_callback callback,
                    const std::string& watcher_name) -> std::uint64_t;

    void unwatch_impl(std::uint64_t key);

    void unwatch_all_impl();

    /// Pauses every watch. Pauses nest: the last matching resume() lifts them.
    void pause();
    void resume();

    void wait_all(watcher::clock_t::duration duration);

    /// Stops the polling thread and removes every watch.
    void close();

    /// Starts the polling thread once; later calls do nothing while it runs.
    void start();

    /// Set while the polling thread runs.
    std::atomic<bool> watching_ = {false};

private:
    class impl;
    class directory_listener;

    void run();
    void wake();
    void wait_for_wake(watcher::clock_t::time_point deadline);
    void deliver(const std::shared_ptr<directory_listener>& listener, const std::vector<watcher::entry>& changes);
    auto find_listener(const fs::path& root, bool recursive) const -> std::shared_ptr<directory_listener>;
    auto take_stale_listeners() -> std::vector<std::shared_ptr<directory_listener>>;

    /// Guards watchers_, directory_listeners_ and pause_depth_.
    std::mutex mutex_;
    std::map<std::uint64_t, std::shared_ptr<impl>> watchers_;
    std::vector<std::shared_ptr<directory_listener>> directory_listeners_;
    int pause_depth_ = 0;

    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
    bool wake_pending_ = false;

    std::thread thread_;
};

} // namespace fs

#endif
