#include "watcher.h"
#include "watchers/watcher_wtr.h"
#include "watchers/watcher_fallback.h"

namespace fs
{

namespace
{
// #ifndef WIN32
// #define FS_WATCHER_WTR 1
// #endif

#ifdef FS_WATCHER_WTR
using watcher_impl = watcher_wtr;
#else
using watcher_impl = watcher_fallback;
#endif

auto get_watcher() -> watcher_impl&
{
    static watcher_impl wd;
    return wd;
}
} // namespace

auto watcher::watch(const fs::path& path,
                    const pattern_filter& filter,
                    bool recursive,
                    bool initial_list,
                    clock_t::duration poll_interval,
                    notify_callback callback,
                    const std::string& watcher_name) -> std::uint64_t
{
    auto& wd = get_watcher();
    if(!wd.watching_)
    {
        wd.start();
    }
    return wd.watch_impl(path, filter, recursive, initial_list, poll_interval, std::move(callback), watcher_name);
}

void watcher::unwatch(std::uint64_t key)
{
    auto& wd = get_watcher();
    wd.unwatch_impl(key);
}

void watcher::unwatch_all()
{
    auto& wd = get_watcher();
    wd.unwatch_all_impl();
}

void watcher::touch(const fs::path& path, bool recursive, fs::file_time_type time)
{
    fs::error_code err;
    if(fs::exists(path, err))
    {
        if(fs::is_directory(path, err))
        {
            if(recursive)
            {
                for(auto& entry : fs::recursive_directory_iterator(path, err))
                {
                    fs::last_write_time(entry.path(), time, err);
                }
            }
            else
            {
                for(auto& entry : fs::directory_iterator(path, err))
                {
                    fs::last_write_time(entry.path(), time, err);
                }
            }
            fs::last_write_time(path, time, err);
        }
        else
        {
            fs::last_write_time(path, time, err);
        }
    }
}

void watcher::pause()
{
    auto& wd = get_watcher();
    wd.pause();
}

void watcher::resume()
{
    auto& wd = get_watcher();
    wd.resume();
}

auto to_string(const watcher::entry& e) -> std::string
{
    static auto file_type_to_string = [](file_type type) -> std::string
    {
        switch(type)
        {
            case file_type::regular:
                return "file";
            case file_type::directory:
                return "directory";
            default:
                return "other";
        }
    };

    static auto status_to_string = [](watcher::entry_status status) -> std::string
    {
        switch(status)
        {
            case watcher::entry_status::created:
                return "created";
            case watcher::entry_status::modified:
                return "modified";
            case watcher::entry_status::removed:
                return "removed";
            case watcher::entry_status::renamed:
                return "renamed";
            default:
                return "unmodified";
        }
    };


    std::stringstream ss;
    ss << "{\"" << int64_t(e.last_mod_time.time_since_epoch().count()) << "\":[";
    if(e.status == watcher::entry_status::renamed)
    {
        ss << e.last_path << " -> ";
    }
    ss << e.path;
    ss << "," << file_type_to_string(e.type);
    ss << "," << status_to_string(e.status);

    ss << "]}";
    return ss.str();
}
} // namespace fs
