#pragma once

#include "action_id.hpp"
#include "mouse.hpp"
#include "mouse_button.hpp"
#include "axis_range.hpp"
#include "input_type.hpp"
#include <map>
#include <vector>

namespace input
{

enum class mouse_axis
{
    x,
    y,
    scroll,
};

class mouse_action_map
{
public:

    struct mouse_entry
    {
        input_type type{};
        axis_range range{};
        uint32_t value{};

        friend auto operator==(const mouse_entry& lhs, const mouse_entry& rhs) -> bool = default;
    };

    std::map<action_id_t, std::vector<mouse_entry>> entries_by_action_id_;

    auto get_analog_value(const action_id_t& action, const mouse& device) const -> float;
    auto get_digital_value(const action_id_t& action, const mouse& device) const -> bool;
    auto is_pressed(const action_id_t& action, const mouse& device) const -> bool;
    auto is_released(const action_id_t& action, const mouse& device) const -> bool;
    auto is_down(const action_id_t& action, const mouse& device) const -> bool;

    void map(const action_id_t& action,
             mouse_axis axis,
             axis_range range = axis_range::full);
    void map(const action_id_t& action, mouse_button button);

    friend auto operator==(const mouse_action_map& lhs, const mouse_action_map& rhs) -> bool = default;
};
} // namespace input
