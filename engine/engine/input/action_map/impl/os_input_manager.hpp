#pragma once

#include "../action_map.hpp"
#include "../input_manager.hpp"
#include "../virtual_gamepad.hpp"
#include "os_cursor.hpp"
#include "os_gamepad.hpp"
#include "os_keyboard.hpp"
#include "os_mouse.hpp"
#include "os_touches.hpp"
#include "pointer_zones.hpp"

#include <ospp/event.h>
#include <hpp/optional.hpp>

#include <atomic>

struct GLFWwindow;

namespace input
{
struct input_reference_size
{
    float w{};
    float h{};
};

class os_input_manager : public input_manager
{
    os_keyboard* keyboard_{};
    os_mouse* mouse_{};
    std::map<uint32_t, os_gamepad*> gamepads_{};

    std::vector<std::shared_ptr<input_device>> devices_;

    hpp::optional<zone> window_input_zone_;
    hpp::optional<zone> work_input_zone_;
    hpp::optional<input_reference_size> input_reference_size_;

    bool is_input_allowed_{true};

    /// The OS cursor as the game holds it; this manager reports its input conditions and applies it.
    os_cursor cursor_;
    /// The fingers on the touch screen. The platform also makes mouse events from them, so the mouse
    /// keeps working for touch.
    os_touches touches_;
    /// Driven by scripts (on-screen controls); listed with the devices, so the action map reads it.
    virtual_gamepad* virtual_gamepad_{};
    /// The areas of on-screen controls: a pointer pressed inside one is the control's, not the game's.
    pointer_zones pointer_zones_;
    /// The last mouse event came from a finger. The platform puts the pointer where each finger lands,
    /// so its motion then is no mouse motion: a finger's motion is its touch's.
    bool is_pointer_from_touch_{};
    /// The platform's mouse press made from a finger landed in a zone; its release is dropped too.
    bool is_touch_press_taken_{};
    /// A zone held the mouse during this frame: its motion was the zone's drag.
    bool is_mouse_motion_taken_{};
    /// Fingers press the mouse's left button (set_mouse_simulated_with_touches).
    bool is_mouse_simulated_with_touches_{true};
    /// A finger has touched the screen since the application started.
    bool has_seen_touch_{};

    /// Stands in for the OS cursor while set. The pointer position is polled rather than
    /// event driven, so synthesized motion has to replace the poll, not add an event.
    hpp::optional<coord> virtual_mouse_position_;
    /// Read from MCP/HTTP workers while the main thread advances it.
    std::atomic<uint64_t> input_frame_{};


public:

    os_input_manager();
    void init();

    auto get_all_devices() const -> const std::vector<std::shared_ptr<input_device>>&;
    auto get_gamepad(uint32_t index) const -> const gamepad& override;
    auto get_max_gamepads() const -> uint32_t override;

    auto get_mouse() const -> const mouse& override;

    auto get_keyboard() const -> const keyboard& override;

    void before_events_update() override;
    void after_events_update() override;

    void on_os_event(const os::event& e);

    void set_window_zone(const zone& window_zone);
    void set_work_zone(const zone& work_zone);
    void set_reference_size(const input_reference_size& reference_size);
    auto get_work_zone() const -> zone;

    auto remap_to_work_zone(coord global_pos) -> coord;
    /// Inverse of remap_to_work_zone: a position in work-zone space back to global.
    auto remap_from_work_zone(coord work_pos) -> coord;
    auto is_inside_work_zone(coord global_pos) -> bool;

    void set_is_input_allowed(bool allowed);
    auto is_input_allowed() const -> bool;

    /// Drives the pointer from `position` (global space) instead of the OS cursor, so the
    /// cursor itself is left where the user put it. Reset it to hand control back.
    void set_virtual_mouse_position(const hpp::optional<coord>& position);
    auto get_virtual_mouse_position() const -> const hpp::optional<coord>&;

    /// Lets go of every key, mouse button, finger and zone pointer, and of the virtual gamepad: nothing
    /// reads as held until it is pressed again.
    void release_all();

    /// The OS cursor: how scripts hold it, whether the host suspends it, what it shows.
    auto get_cursor() -> os_cursor&;
    auto get_cursor() const -> const os_cursor&;
    /// The fingers on the touch screen this frame, in work zone pixels. Fingers a zone holds are not listed.
    auto get_touches() const -> const os_touches&;

    /// The gamepad on-screen controls drive.
    auto get_virtual_gamepad() -> virtual_gamepad&;
    /// The areas of on-screen controls, in work zone pixels.
    auto get_pointer_zones() -> pointer_zones&;
    auto get_pointer_zones() const -> const pointer_zones&;

    /// Whether fingers press the mouse's left button. On by default, so a game made for the mouse takes
    /// taps as clicks; a game with on-screen controls turns it off, so that a tap off the controls is no
    /// click. Either way a finger's motion never reads as mouse motion.
    void set_mouse_simulated_with_touches(bool simulated);
    auto is_mouse_simulated_with_touches() const -> bool;
    /// Whether the game runs on a touch screen: on a phone or a tablet, or a finger has touched the screen.
    auto is_touch_supported() const -> bool;

    /// Counts calls to before_events_update, which is the boundary where a pressed state
    /// becomes down and a released state becomes up - so it is the unit a synthesized tap or
    /// hold has to be measured in.
    auto get_input_frame() const -> uint64_t;

    /// A position in the main window's pixels, in work zone pixels. The window zone places the window
    /// in the global space the mouse reports.
    auto map_window_position(coord window_position) -> coord;
    /// A finger's position in work zone pixels. Fingers come normalized to the window under them.
    auto map_finger_position(const os::touch_finger_event& finger) -> coord;

private:
    void on_finger_event(const os::event& e);
    /// Whether a mouse button event is the game's: a zone takes a left press inside it (and that press's
    /// release), and fingers press the mouse only while it is simulated with touches.
    auto is_mouse_button_for_game(const os::event& e) -> bool;
};

} // namespace input
