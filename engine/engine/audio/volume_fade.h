#pragma once

#include <algorithm>

namespace unravel
{

/**
 * @brief A linear volume ramp advanced by elapsed seconds.
 */
struct volume_fade
{
    /// Volume when the fade started.
    float from = 0.0f;
    /// Volume the fade ends at.
    float to = 0.0f;
    /// Length of the fade in seconds.
    float duration = 0.0f;
    /// Seconds advanced so far.
    float elapsed = 0.0f;
    /// Whether the faded sound stops when the fade ends.
    bool stop_at_end = false;
    /// Whether the fade is running.
    bool is_active = false;

    /**
     * @brief Starts a fade from @p current to @p target over @p seconds (0 jumps there at the next advance).
     */
    void start(float current, float target, float seconds, bool stop_when_done)
    {
        from = current;
        to = target;
        duration = std::max(seconds, 0.0f);
        elapsed = 0.0f;
        stop_at_end = stop_when_done;
        is_active = true;
    }

    /**
     * @brief Advances the fade by @p seconds and returns the volume it reached; the fade ends at its target.
     */
    auto advance(float seconds) -> float
    {
        elapsed = std::min(elapsed + std::max(seconds, 0.0f), duration);
        const bool is_done = elapsed >= duration;
        const float progress = is_done ? 1.0f : elapsed / duration;
        is_active = !is_done;
        return from + (to - from) * progress;
    }
};

} // namespace unravel
