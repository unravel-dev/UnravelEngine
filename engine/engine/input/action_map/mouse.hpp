#pragma once

#include "button_state.hpp"
#include "coord.hpp"
#include "device.hpp"

namespace input
{
class mouse : public input_device
{
public:
    mouse()
    : input_device(device_type::mouse) {
    }

    virtual auto get_axis_value(uint32_t axis) const -> float = 0;
    virtual auto get_button_state(uint32_t button) const -> button_state = 0;
    virtual auto get_left_button_state() const -> button_state = 0;
    virtual auto get_middle_button_state() const -> button_state = 0;
    virtual auto get_right_button_state() const -> button_state = 0;
    virtual auto get_position() const -> coord = 0;
    virtual auto get_scroll() const -> float = 0;
    virtual auto is_down(uint32_t button) const -> bool= 0;
    virtual auto is_pressed(uint32_t button) const -> bool= 0;
    virtual auto is_released(uint32_t button) const -> bool= 0;

};
}
