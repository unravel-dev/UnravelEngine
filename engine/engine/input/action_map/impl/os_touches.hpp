#pragma once

#include "../touch.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace input
{
/**
 * @brief The fingers on the touch screen, frame by frame.
 *
 * A finger begins in the frame it touches, is moved or stationary in each frame it stays, and is ended
 * (or canceled) for one frame after it leaves. A finger that touches and leaves within one frame still
 * reads as began first: its end shows in the next frame. The input manager calls update() at the start
 * of each frame, before it hands over the frame's finger events.
 */
class os_touches
{
public:
    /// Starts a frame: the fingers that ended last frame are gone, the others are stationary.
    void update();

    /// A finger of a touch device touched at position.
    void begin(uint64_t device_id, uint64_t finger_id, coord position, float pressure);
    /// A finger moved to position. Unknown fingers (one that began outside the work zone) are ignored.
    void move(uint64_t device_id, uint64_t finger_id, coord position, float pressure);
    /// A finger left the screen at position, or was canceled.
    void end(uint64_t device_id, uint64_t finger_id, coord position, bool is_canceled);
    /// Cancels every finger: none reads as touching after the next update().
    void cancel_all();

    auto get_count() const -> size_t;
    /// The fingers in the order they touched the screen.
    auto get(size_t index) const -> const touch_point&;

private:
    struct tracked_touch
    {
        uint64_t device_id{};
        uint64_t finger_id{};
        /// The end of a finger that began this frame, shown next frame.
        bool is_end_pending{};
        touch_phase pending_phase{};
        touch_point point{};
    };

    auto find(uint64_t device_id, uint64_t finger_id) -> tracked_touch*;
    auto get_free_id() const -> int32_t;

    std::vector<tracked_touch> touches_;
};
} // namespace input
