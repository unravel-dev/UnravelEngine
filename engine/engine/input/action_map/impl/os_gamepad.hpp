#pragma once

#include "../bimap.hpp"
#include "../button_state_map.hpp"
#include "../gamepad.hpp"
#include <ospp/gamepad.h>
#include <map>
#include <string>

namespace input
{
class os_gamepad : public gamepad
{
    int index_;
    std::map<unsigned, float> axis_map_;
    button_state_map button_state_map_;
    std::string name_;

    bool refresh_device_{true};
    os::gamepad::device_t device_;
public:
    os_gamepad(int index);
    ~os_gamepad() override;
    auto get_axis_value(uint32_t axis) const -> float override;
    auto get_button_state(uint32_t button) const -> button_state override;
    auto get_name() const -> const std::string& override;
    auto is_connected() const -> bool override;
    auto is_down(uint32_t button) const -> bool override;
    auto is_pressed(uint32_t button) const -> bool override;
    auto is_released(uint32_t button) const -> bool override;
    void update();
    void refresh_device();
    void open_device();
    void close_device();
};
} // namespace input
