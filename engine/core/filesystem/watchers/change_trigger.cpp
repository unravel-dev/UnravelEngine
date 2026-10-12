#include "change_trigger.h"

#include <atomic>
#include <thread>
#include <utility>

#include <base/platform/thread.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace fs
{

#ifdef _WIN32

namespace
{
/// Changes are coalesced over this window before the next notification is armed. Changes made
/// meanwhile are recorded by the OS and signal again as soon as it is re-armed, so none is lost;
/// it only bounds the rate at which a constantly signalled handle can wake the watcher.
constexpr DWORD COALESCE_WINDOW_MS = 10;
constexpr DWORD NOTIFY_FILTER = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
} // namespace

class change_trigger::impl
{
public:
    impl(HANDLE change, HANDLE stop, on_change_t on_change)
        : change_(change)
        , stop_(stop)
        , on_change_(std::move(on_change))
    {
        thread_ = std::thread(
            [this]() -> void
            {
                run();
            });
    }

    ~impl()
    {
        SetEvent(stop_);
        if(thread_.joinable())
        {
            thread_.join();
        }
        FindCloseChangeNotification(change_);
        CloseHandle(stop_);
    }

    impl(const impl&) = delete;
    auto operator=(const impl&) -> impl& = delete;

    auto is_active() const -> bool
    {
        return is_active_.load();
    }

private:
    void run()
    {
        platform::set_thread_name("fs::watcher trigger");
        const HANDLE handles[] = {stop_, change_};
        while(true)
        {
            const DWORD signalled = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if(signalled != WAIT_OBJECT_0 + 1)
            {
                break;
            }
            on_change_();
            if(WaitForSingleObject(stop_, COALESCE_WINDOW_MS) == WAIT_OBJECT_0)
            {
                break;
            }
            if(!FindNextChangeNotification(change_))
            {
                break;
            }
        }
        is_active_ = false;
    }

    HANDLE change_;
    HANDLE stop_;
    on_change_t on_change_;
    std::atomic<bool> is_active_{true};
    std::thread thread_;
};

auto change_trigger::start(const fs::path& root, bool recursive, on_change_t on_change) -> bool
{
    stop();
    const HANDLE change = FindFirstChangeNotificationW(root.c_str(), recursive ? TRUE : FALSE, NOTIFY_FILTER);
    if(change == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    const HANDLE stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if(stop_event == nullptr)
    {
        FindCloseChangeNotification(change);
        return false;
    }
    impl_ = std::make_unique<impl>(change, stop_event, std::move(on_change));
    return true;
}

auto change_trigger::is_active() const -> bool
{
    return impl_ && impl_->is_active();
}

#elif defined(__linux__)

namespace
{
/// Changes are coalesced over this window before the queue is read again. Events arriving meanwhile wait
/// in the inotify queue and signal as soon as it is polled, so none is lost; it only bounds the rate at
/// which a constantly written tree can wake the watcher.
constexpr int COALESCE_WINDOW_MS = 10;
/// The changes Windows reports through FILE_NOTIFY_CHANGE_FILE_NAME / DIR_NAME / SIZE / LAST_WRITE, plus
/// the root itself going away. IN_ATTRIB covers a new modification time alone (fs::watcher::touch).
constexpr std::uint32_t WATCH_MASK = IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_MOVED_FROM |
                                     IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;
/// Read buffer for a burst of events; read() returns whole events only.
constexpr std::size_t EVENT_BUFFER_SIZE = 64 * 1024;
} // namespace

/**
 * inotify watches one folder per watch descriptor, so a recursive trigger watches every folder under the
 * root and adds the folders created or moved in later. A folder it cannot watch (the per-user watch limit)
 * only falls back to the listener's poll interval.
 */
class change_trigger::impl
{
public:
    impl(int inotify_fd, int stop_fd, const fs::path& root, int root_wd, bool recursive, on_change_t on_change)
        : inotify_fd_(inotify_fd)
        , stop_fd_(stop_fd)
        , root_(root)
        , root_wd_(root_wd)
        , recursive_(recursive)
        , on_change_(std::move(on_change))
        , buffer_(EVENT_BUFFER_SIZE)
    {
        folders_[root_wd_] = root_;
        if(recursive_)
        {
            watch_subfolders(root_);
        }
        thread_ = std::thread(
            [this]() -> void
            {
                run();
            });
    }

    ~impl()
    {
        const std::uint64_t one = 1;
        [[maybe_unused]] const auto written = ::write(stop_fd_, &one, sizeof(one));
        if(thread_.joinable())
        {
            thread_.join();
        }
        ::close(inotify_fd_);
        ::close(stop_fd_);
    }

    impl(const impl&) = delete;
    auto operator=(const impl&) -> impl& = delete;

    auto is_active() const -> bool
    {
        return is_active_.load();
    }

private:
    void run()
    {
        platform::set_thread_name("fs::trigger");
        while(true)
        {
            pollfd handles[] = {{stop_fd_, POLLIN, 0}, {inotify_fd_, POLLIN, 0}};
            if(::poll(handles, 2, -1) < 0)
            {
                if(errno == EINTR)
                {
                    continue;
                }
                break;
            }
            if(handles[0].revents != 0 || (handles[1].revents & POLLIN) == 0)
            {
                break;
            }
            const bool is_root_watched = read_events();
            on_change_();
            if(!is_root_watched || is_stop_requested(COALESCE_WINDOW_MS))
            {
                break;
            }
        }
        is_active_ = false;
    }

    auto is_stop_requested(int timeout_ms) -> bool
    {
        pollfd handle{stop_fd_, POLLIN, 0};
        return ::poll(&handle, 1, timeout_ms) > 0;
    }

    /// Drains the queue, watching the folders that appeared. False once the root moved or went away.
    auto read_events() -> bool
    {
        bool is_root_watched = true;
        while(true)
        {
            const auto length = ::read(inotify_fd_, buffer_.data(), buffer_.size());
            if(length <= 0)
            {
                break;
            }
            for(ssize_t offset = 0; offset < length;)
            {
                const auto* event = reinterpret_cast<const inotify_event*>(buffer_.data() + offset);
                offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
                is_root_watched &= handle_event(*event);
            }
        }
        return is_root_watched;
    }

    /// False when the event ends the root's watch.
    auto handle_event(const inotify_event& event) -> bool
    {
        if((event.mask & IN_Q_OVERFLOW) != 0)
        {
            // The dropped events may have created folders: walk the tree again to watch them.
            if(recursive_)
            {
                watch_subfolders(root_);
            }
            return true;
        }
        if((event.mask & IN_IGNORED) != 0)
        {
            folders_.erase(event.wd);
            return event.wd != root_wd_;
        }
        if(event.wd == root_wd_ && (event.mask & IN_MOVE_SELF) != 0)
        {
            return false;
        }
        const bool is_new_folder = (event.mask & IN_ISDIR) != 0 && (event.mask & (IN_CREATE | IN_MOVED_TO)) != 0;
        if(recursive_ && is_new_folder && event.len > 0)
        {
            const auto parent = folders_.find(event.wd);
            if(parent != folders_.end())
            {
                const auto folder = parent->second / event.name;
                watch_folder(folder);
                watch_subfolders(folder);
            }
        }
        return true;
    }

    /// Re-watching a folder already watched returns its descriptor, which then maps to its current path.
    void watch_folder(const fs::path& folder)
    {
        const int wd = ::inotify_add_watch(inotify_fd_, folder.c_str(), WATCH_MASK);
        if(wd >= 0)
        {
            folders_[wd] = folder;
        }
    }

    /// Symlinked folders are skipped, as the listener's scan does not descend into them.
    void watch_subfolders(const fs::path& folder)
    {
        fs::error_code err;
        fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, err);
        const fs::recursive_directory_iterator end;
        for(; !err && it != end; it.increment(err))
        {
            fs::error_code type_err;
            if(it->is_directory(type_err) && !it->is_symlink(type_err))
            {
                watch_folder(it->path());
            }
        }
    }

    int inotify_fd_;
    int stop_fd_;
    fs::path root_;
    int root_wd_;
    bool recursive_;
    on_change_t on_change_;
    /// Watch descriptor -> watched folder. Filled before the thread starts, then touched by it only.
    std::unordered_map<int, fs::path> folders_;
    std::vector<char> buffer_;
    std::atomic<bool> is_active_{true};
    std::thread thread_;
};

auto change_trigger::start(const fs::path& root, bool recursive, on_change_t on_change) -> bool
{
    stop();
    const int inotify_fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if(inotify_fd < 0)
    {
        return false;
    }
    const int root_wd = ::inotify_add_watch(inotify_fd, root.c_str(), WATCH_MASK);
    if(root_wd < 0)
    {
        ::close(inotify_fd);
        return false;
    }
    const int stop_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(stop_fd < 0)
    {
        ::close(inotify_fd);
        return false;
    }
    impl_ = std::make_unique<impl>(inotify_fd, stop_fd, root, root_wd, recursive, std::move(on_change));
    return true;
}

auto change_trigger::is_active() const -> bool
{
    return impl_ && impl_->is_active();
}

#else

/// No OS notifications on this platform yet: listeners rely on their poll interval.
class change_trigger::impl
{
};

auto change_trigger::start(const fs::path& /*root*/, bool /*recursive*/, on_change_t /*on_change*/) -> bool
{
    return false;
}

auto change_trigger::is_active() const -> bool
{
    return false;
}

#endif

change_trigger::change_trigger() = default;

change_trigger::~change_trigger()
{
    stop();
}

void change_trigger::stop()
{
    impl_.reset();
}

} // namespace fs
