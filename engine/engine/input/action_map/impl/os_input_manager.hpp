#pragma once

#include "../action_map.hpp"
#include "../input_manager.hpp"
#include "os_gamepad.hpp"
#include "os_keyboard.hpp"
#include "os_mouse.hpp"

#include <ospp/event.h>
#include <hpp/optional.hpp>

#include <atomic>

struct GLFWwindow;

namespace input
{
struct zone
{
    int x{};
    int y{};

    int w{};
    int h{};
};

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

    /// Lets go of every key and mouse button: nothing reads as held until it is pressed again.
    void release_all();

    /// Counts calls to before_events_update, which is the boundary where a pressed state
    /// becomes down and a released state becomes up - so it is the unit a synthesized tap or
    /// hold has to be measured in.
    auto get_input_frame() const -> uint64_t;

};

} // namespace input
