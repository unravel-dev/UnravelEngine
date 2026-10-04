#ifndef FS_CHANGE_TRIGGER_H
#define FS_CHANGE_TRIGGER_H

#include <functional>
#include <memory>

#include "../filesystem.h"

namespace fs
{

/**
 * @brief Wakes a polling directory listener when the OS reports a change under its root.
 *
 * A hint that something changed, never what changed: the listener still decides that with a full scan,
 * so a lost or coalesced notification costs latency, not correctness. Where the platform has no
 * implementation, or the OS refuses the request (a missing folder, a filesystem without change
 * notifications), is_active() is false and the listener keeps relying on its poll interval.
 */
class change_trigger
{
public:
    using on_change_t = std::function<void()>;

    change_trigger();
    ~change_trigger();
    change_trigger(const change_trigger&) = delete;
    auto operator=(const change_trigger&) -> change_trigger& = delete;

    /**
     * @brief Starts watching root, replacing any previous watch.
     *
     * on_change runs on a background thread, at most once per short coalescing window.
     * @return True when the OS accepted the watch.
     */
    auto start(const fs::path& root, bool recursive, on_change_t on_change) -> bool;

    /// Stops watching. on_change does not run after this returns.
    void stop();

    /// True while the OS delivers notifications.
    auto is_active() const -> bool;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

} // namespace fs

#endif
