#pragma once

#include "../action_map.hpp"
#include "../input_manager.hpp"
#include "os_gamepad.hpp"
#include "os_keyboard.hpp"
#include "os_mouse.hpp"

#include <ospp/event.h>
#include <hpp/optional.hpp>

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

    auto remap_to_work_zone(coord global_pos) -> coord;
    auto is_inside_work_zone(coord global_pos) -> bool;

    void set_is_input_allowed(bool allowed);
    auto is_input_allowed() const -> bool;

};

} // namespace input
