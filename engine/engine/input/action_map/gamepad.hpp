#pragma once

#include "button_state.hpp"
#include "device.hpp"

namespace input
{
class gamepad : public input_device
{
public:
    gamepad() : input_device(device_type::gamepad)
    {
    }

    virtual auto get_axis_value(uint32_t axis) const -> float = 0;
    virtual auto get_button_state(uint32_t button) const -> button_state = 0;
    virtual auto is_connected() const -> bool = 0;
    virtual auto is_down(uint32_t button) const -> bool= 0;
    virtual auto is_pressed(uint32_t button) const -> bool= 0;
    virtual auto is_released(uint32_t button) const -> bool= 0;
};
} // namespace InputLib
