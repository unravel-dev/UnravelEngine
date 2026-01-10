#ifndef FS_WATCHER_H
#define FS_WATCHER_H

#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

#include "filesystem.h"
#include "pattern_filter.h"

namespace fs
{



class watcher
{
public:
    enum entry_status
    {
        created,
        modified,
        removed,
        renamed,
        unmodified,
    };

    struct entry
    {
        fs::path path;
        fs::path last_path;
        entry_status status = unmodified;
        fs::file_time_type last_mod_time;
        std::uintmax_t size = 0;
        fs::file_type type;
    };

    using notify_callback = std::function<void(const std::vector<entry>&, bool)>;
    using clock_t = std::chrono::steady_clock;
    //-----------------------------------------------------------------------------
    //  Name : watch ()
    /// <summary>
    /// Watches a file or directory for modification and call back the specified
    /// std::function. A list of modified files or directory is passed as argument
    /// of the callback. Use this version only if you are watching multiple files
    /// or a directory.
    /// </summary>
    //-----------------------------------------------------------------------------
    static auto watch(const fs::path& path,
                      const pattern_filter& filter,
                      bool recursive,
                      bool initial_list,
                      clock_t::duration poll_interval,
                      notify_callback callback,
                      const std::string& watcher_name = "") -> std::uint64_t;

    //-----------------------------------------------------------------------------
    //  Name : unwatch ()
    /// <summary>
    /// Un-watches a previously registered file or directory
    /// </summary>
    //-----------------------------------------------------------------------------
    static void unwatch(std::uint64_t key);

    //-----------------------------------------------------------------------------
    //  Name : unwatch_all ()
    /// <summary>
    /// Un-watches all previously registered file or directory
    /// </summary>
    //-----------------------------------------------------------------------------
    static void unwatch_all();

    //-----------------------------------------------------------------------------
    //  Name : touch ()
    /// <summary>
    /// Sets the last modification time of a file or directory. by default sets
    /// the time to the current time
    /// </summary>
    //-----------------------------------------------------------------------------
    static void touch(const fs::path& path, bool recursive, fs::file_time_type time = fs::now());

    //-----------------------------------------------------------------------------
    //  Name : pause ()
    /// <summary>
    /// Pauses all watchers
    /// </summary>
    //-----------------------------------------------------------------------------
    static void pause();

    //-----------------------------------------------------------------------------
    //  Name : resume ()
    /// <summary>
    /// Resumes all watchers
    /// </summary>
    //-----------------------------------------------------------------------------
    static void resume();
};

auto to_string(const watcher::entry& e) -> std::string;

} // namespace fs

#endif
