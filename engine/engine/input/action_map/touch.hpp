#pragma once

#include "coord.hpp"

#include <cstdint>

namespace input
{
/// Where a finger is in its life on the touch screen, as one frame sees it.
enum class touch_phase : uint8_t
{
    /// The finger touched the screen this frame.
    began,
    /// The finger moved this frame.
    moved,
    /// The finger touches the screen and did not move this frame.
    stationary,
    /// The finger left the screen this frame.
    ended,
    /// The system took the finger away (a gesture of its own, the window lost it); not a release
    /// to act on.
    canceled,
};

/// A finger on the touch screen.
struct touch_point
{
    /// Stays the same while the finger touches the screen: the lowest number no other finger has.
    int32_t id{};
    touch_phase phase{touch_phase::began};
    /// In work zone pixels, as the mouse position.
    coord position{};
    /// The motion this frame, in the same pixels.
    coord delta{};
    /// 0 to 1, as the device reports it.
    float pressure{};
};
} // namespace input
