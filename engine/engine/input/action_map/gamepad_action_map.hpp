#pragma once

#include "action_id.hpp"
#include "axis_range.hpp"
#include "gamepad.hpp"
#include "gamepad_axis.hpp"
#include "gamepad_button.hpp"
#include "input_type.hpp"
#include <map>
#include <vector>

namespace input
{
class gamepad_action_map
{
public:
    struct gamepad_entry
    {
        input_type type{};
        axis_range range{};
        uint32_t value{};
        float min_analog_value{};
        float max_analog_value{};

        friend auto operator==(const gamepad_entry& lhs, const gamepad_entry& rhs) -> bool = default;
    };

    std::map<action_id_t, std::vector<gamepad_entry>> entries_by_action_id_;

    auto get_analog_value(const action_id_t& action, const gamepad& device) const -> float;
    auto get_digital_value(const action_id_t& action, const gamepad& device) const -> bool;
    auto is_pressed(const action_id_t& action, const gamepad& device) const -> bool;
    auto is_released(const action_id_t& action, const gamepad& device) const -> bool;
    auto is_down(const action_id_t& action, const gamepad& device) const -> bool;


    void map(const action_id_t& action,
             gamepad_axis axis,
             axis_range range = axis_range::full,
             float min_analog_value = -1.0f,
             float max_analog_value = 1.0f);
    void map(const action_id_t& action, gamepad_button button);

    friend auto operator==(const gamepad_action_map& lhs, const gamepad_action_map& rhs) -> bool = default;
};
} // namespace InputLib
