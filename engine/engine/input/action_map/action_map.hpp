#pragma once

#include "action_id.hpp"
#include "device.hpp"
#include "gamepad_action_map.hpp"
#include "keyboard_action_map.hpp"
#include "mouse_action_map.hpp"
#include "to_string.hpp"

namespace input
{
class action_map
{
public:
    keyboard_action_map keyboard_map;
    gamepad_action_map gamepad_map;
    mouse_action_map mouse_map;

    auto get_analog_value(const action_id_t& action, const input_device& device) const -> float;
    auto get_digital_value(const action_id_t& action, const input_device& device) const -> bool;
    auto is_pressed(const action_id_t& action, const input_device& device) const -> bool;
    auto is_released(const action_id_t& action, const input_device& device) const -> bool;
    auto is_down(const action_id_t& action, const input_device& device) const -> bool;
    void map(const action_id_t& action,
             gamepad_axis axis,
             axis_range range = axis_range::full,
             float min_analog_value = -1.0f,
             float max_analog_value = 1.0f);
    void map(const action_id_t& action, gamepad_button button);
    void map(const action_id_t& action, key_code key, float analog_value = 1.0f);
    void map(const action_id_t& action, key_code key, const std::vector<key_code>& modifiers, float analog_value = 1.0f);

    void map(const action_id_t& action,
             mouse_axis axis,
             axis_range range = axis_range::full);
    void map(const action_id_t& action, mouse_button button);

    friend auto operator==(const action_map& lhs, const action_map& rhs) -> bool = default;
};

} // namespace InputLib
