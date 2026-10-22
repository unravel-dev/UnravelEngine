#pragma once

#include "os_touches.hpp"

#include <cstdint>
#include <vector>

namespace input
{
/**
 * @brief Screen areas for on-screen controls, such as a touch stick or a button. A finger, or a left
 * mouse press, that starts inside a zone belongs to the zone until it lifts, and the input manager keeps
 * it from the game's mouse and touches: a drag on a stick neither turns a mouse-look camera nor clicks.
 *
 * A zone holds one pointer at a time and reads it frame by frame as a touch: it begins, is moved or
 * stationary while it stays, and ends (or is canceled) for one frame. Where zones overlap, the newest
 * free one takes a press. Areas are in work zone pixels, the space of the mouse position.
 */
class pointer_zones
{
public:
    using zone_id = uint32_t;

    /// A new zone with no area: it takes nothing until set_area. Ids are never reused.
    auto create() -> zone_id;
    /// Removes a zone. The pointer it held stays nobody's until it lifts.
    void destroy(zone_id id);
    /// Removes every zone.
    void clear();
    void set_area(zone_id id, const zone& area);

    /// Starts a frame: the pointers that lifted last frame are gone, the others are stationary.
    void update();

    /// Whether a zone at position holds no pointer: a press there would be taken.
    auto is_free_zone_at(coord position) const -> bool;

    /// A finger touched at position: the free zone there takes it. Returns whether one did.
    auto take_finger(uint64_t device_id, uint64_t finger_id, coord position, float pressure) -> bool;
    /// Returns whether a zone holds the finger; its motion is then the zone's.
    auto move_finger(uint64_t device_id, uint64_t finger_id, coord position, float pressure) -> bool;
    /// Returns whether a zone held the finger; its end is then the zone's.
    auto end_finger(uint64_t device_id, uint64_t finger_id, coord position, bool is_canceled) -> bool;

    /// The left mouse button went down at position: the free zone there takes the mouse.
    auto take_mouse(coord position) -> bool;
    /// The mouse is at position; a zone holding it follows.
    void move_mouse(coord position);
    /// The left mouse button went up. Returns whether a zone held the mouse.
    auto end_mouse(coord position) -> bool;
    /// Whether a zone holds the mouse button down.
    auto is_mouse_held() const -> bool;

    /// The pointer holding a zone this frame (in the frame it lifts too), or nullptr.
    auto get_pointer(zone_id id) const -> const touch_point*;
    /// Whether the pointer holding a zone is the mouse.
    auto is_mouse_pointer(zone_id id) const -> bool;

    /// Cancels every pointer: none holds a zone after the next update().
    void cancel_all();

private:
    /// The mouse, as the device of a pointer: finger ids come from touch devices.
    static constexpr uint64_t MOUSE_DEVICE = ~uint64_t{0};
    static constexpr uint64_t MOUSE_FINGER = 0;

    struct pointer_zone
    {
        zone_id id{};
        zone area{};
        /// At most one pointer touches; the one that lifted this frame may be listed before it.
        os_touches pointer;
        /// The newest pointer is the mouse.
        bool is_mouse{};
    };

    auto find_zone(zone_id id) -> pointer_zone*;
    auto find_zone(zone_id id) const -> const pointer_zone*;
    auto find_free_zone_at(coord position) -> pointer_zone*;
    auto find_holder(uint64_t device_id, uint64_t finger_id) -> pointer_zone*;
    auto take(uint64_t device_id, uint64_t finger_id, coord position, float pressure) -> bool;

    std::vector<pointer_zone> zones_;
    zone_id next_id_{1};
};
} // namespace input
