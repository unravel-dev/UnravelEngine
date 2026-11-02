#pragma once

namespace unravel
{

/**
 * @brief What to do about the audio device this frame.
 */
struct audio_device_action
{
    /// The device went away since the last update.
    bool is_newly_lost = false;
    /// Try to reopen the device now.
    bool should_reconnect = false;
};

/**
 * @brief Notices an audio device going away and paces the attempts to reopen it. The first attempt comes at
 * once, because a changed audio route has a device ready. The later ones wait longer each time: an attempt
 * blocks the frame that makes it, and without any device they keep failing.
 *
 * It only knows what update is told, so a test can lose a device without having one.
 */
class audio_device_watch
{
public:
    /// Seconds between the first attempt and the second. Each later wait doubles.
    static constexpr float first_retry_wait = 1.0f;
    /// The longest wait between attempts, which is how late sound can return after a device is plugged in.
    static constexpr float longest_retry_wait = 8.0f;

    /**
     * @param is_connected Whether the device plays.
     * @param seconds Real time since the last update.
     */
    auto update(bool is_connected, float seconds) -> audio_device_action;

    /// True from the update that found the device gone until one finds it connected.
    auto is_lost() const -> bool;

private:
    bool is_lost_ = false;
    /// Seconds until the next attempt.
    float wait_ = 0.0f;
    /// The wait after the next attempt.
    float next_wait_ = first_retry_wait;
};

} // namespace unravel
