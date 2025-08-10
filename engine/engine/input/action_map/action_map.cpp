#include "action_map.hpp"

namespace input
{
//  ----------------------------------------------------------------------------
auto action_map::get_analog_value(const action_id_t& action, const input_device& device) const -> float
{
    switch(device.get_device_type())
    {
        case device_type::gamepad:
            return gamepad_map.get_analog_value(action, static_cast<const gamepad&>(device));
        case device_type::keyboard:
            return keyboard_map.get_analog_value(action, static_cast<const keyboard&>(device));
        case device_type::mouse:
            return mouse_map.get_analog_value(action, static_cast<const mouse&>(device));
    }

    return 0.0f;
}

//  ----------------------------------------------------------------------------
auto action_map::get_digital_value(const action_id_t& action, const input_device& device) const -> bool
{
    switch(device.get_device_type())
    {
        case device_type::gamepad:
            return gamepad_map.get_digital_value(action, static_cast<const gamepad&>(device));
        case device_type::keyboard:
            return keyboard_map.get_digital_value(action, static_cast<const keyboard&>(device));
        case device_type::mouse:
            return mouse_map.get_digital_value(action, static_cast<const mouse&>(device));
    }

    return false;
}

//  ----------------------------------------------------------------------------
auto action_map::is_pressed(const action_id_t& action, const input_device& device) const -> bool
{
    switch(device.get_device_type())
    {
        case device_type::gamepad:
            return gamepad_map.is_pressed(action, static_cast<const gamepad&>(device));
        case device_type::keyboard:
            return keyboard_map.is_pressed(action, static_cast<const keyboard&>(device));
        case device_type::mouse:
            return mouse_map.is_pressed(action, static_cast<const mouse&>(device));
    }

    return false;
}

auto action_map::is_released(const action_id_t& action, const input_device& device) const -> bool
{
    switch(device.get_device_type())
    {
        case device_type::gamepad:
            return gamepad_map.is_released(action, static_cast<const gamepad&>(device));
        case device_type::keyboard:
            return keyboard_map.is_released(action, static_cast<const keyboard&>(device));
        case device_type::mouse:
            return mouse_map.is_released(action, static_cast<const mouse&>(device));
    }

    return false;
}


auto action_map::is_down(const action_id_t& action, const input_device& device) const -> bool
{
    switch(device.get_device_type())
    {
        case device_type::gamepad:
            return gamepad_map.is_down(action, static_cast<const gamepad&>(device));
        case device_type::keyboard:
            return keyboard_map.is_down(action, static_cast<const keyboard&>(device));
        case device_type::mouse:
            return mouse_map.is_down(action, static_cast<const mouse&>(device));
    }

    return false;
}

//  ----------------------------------------------------------------------------
void action_map::map(const action_id_t& action,
                     gamepad_axis axis,
                     axis_range range,
                     float min_analog_value,
                     float max_analog_value)
{
    gamepad_map.map(action, axis, range, min_analog_value, max_analog_value);
}

//  ----------------------------------------------------------------------------
void action_map::map(const action_id_t& action, gamepad_button button)
{
    gamepad_map.map(action, button);
}

//  ----------------------------------------------------------------------------
void action_map::map(const action_id_t& action, key_code key, float analog_value)
{
    keyboard_map.map(action, key, analog_value);
}
void action_map::map(const action_id_t& action, key_code key, const std::vector<key_code>& modifiers, float analog_value)
{
    keyboard_map.map(action, key, modifiers, analog_value);
}

void action_map::map(const action_id_t& action,
                     mouse_axis axis,
                     axis_range range)
{
    mouse_map.map(action, axis, range);
}

//  ----------------------------------------------------------------------------
void action_map::map(const action_id_t& action, mouse_button button)
{
    mouse_map.map(action, button);
}

} // namespace input
