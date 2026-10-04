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
