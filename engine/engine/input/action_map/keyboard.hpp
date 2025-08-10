#pragma once

#include "device.hpp"
#include "key.hpp"
#include "key_state.hpp"

namespace input
{
class keyboard : public input_device
{
public:
    keyboard() : input_device(device_type::keyboard)
    {
    }

    virtual auto get_key_state(key_code key) const -> key_state = 0;
    virtual auto is_down(key_code key) const -> bool = 0;
    virtual auto is_pressed(key_code key) const -> bool = 0;
    virtual auto is_released(key_code key) const -> bool = 0;

};
} // namespace input
