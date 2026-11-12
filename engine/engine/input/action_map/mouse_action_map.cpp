#include "mouse_action_map.hpp"

#include <algorithm>
#include <stdexcept>
namespace input
{

namespace
{
auto get_axis_value(const float value,
                    const axis_range range) -> float
{
    float v{};

    switch(range)
    {
        case axis_range::full:
            v = value;
            break;
        case axis_range::positive:
            v = value > 0.0f ? value : 0.0f;
            break;
        case axis_range::negative:
            v = value < 0.0f ? value : 0.0f;
            break;
        default:
            throw std::runtime_error("Case not implemented.");
    }

    return v;
}
} // namespace
//  ----------------------------------------------------------------------------
auto mouse_action_map::get_analog_value(const action_id_t& action, const mouse& device) const -> float
{
    const auto& find = entries_by_action_id_.find(action);

    if(find == entries_by_action_id_.end())
    {
        return 0.0f;
    }

    const auto& entries = find->second;

    for(const auto& entry : entries)
    {
        switch(entry.type)
        {
            case input_type::axis:
            {
                const float value = get_axis_value(device.get_axis_value(entry.value),
                                                   entry.range);

                if(epsilon_not_equal(value, 0.0f))
                {
                    return value;
                }
            }
            break;

            case input_type::button:
            {
                if(device.is_down(entry.value))
                {
                    return 1.0f;
                }
            }
            break;

            default:
                break;
        }


    }

    return 0.0f;
}

//  ----------------------------------------------------------------------------
auto mouse_action_map::get_digital_value(const action_id_t& action, const mouse& device) const -> bool
{
    const auto& find = entries_by_action_id_.find(action);

    if(find == entries_by_action_id_.end())
    {
        return false;
    }

    const auto& entries = find->second;

    for(const auto& entry : entries)
    {
        switch(entry.type)
        {
            case input_type::axis:
            {
                const float value = get_axis_value(device.get_axis_value(entry.value),
                                                   entry.range);

                if(epsilon_not_equal(value, 0.0f))
                {
                    return true;
                }
            }
            break;

            case input_type::button:
            {
                if(device.is_down(entry.value))
                {
                    return true;
                }
            }
            break;

            default:
                break;
        }
    }

    return false;
}

//  ----------------------------------------------------------------------------
auto mouse_action_map::is_pressed(const action_id_t& action, const mouse& device) const -> bool
{
    const auto& find = entries_by_action_id_.find(action);

    if(find == entries_by_action_id_.end())
    {
        return false;
    }

    const auto& entries = find->second;

    for(const auto& entry : entries)
    {
        switch(entry.type)
        {
            case input_type::axis:
            {
            }
            break;

            case input_type::button:
            {
                if(device.is_pressed(entry.value))
                {
                    return true;
                }
            }
            break;

            default:
                break;
        }
    }

    return false;
}

auto mouse_action_map::is_released(const action_id_t& action, const mouse& device) const -> bool
{
    const auto& find = entries_by_action_id_.find(action);

    if(find == entries_by_action_id_.end())
    {
        return false;
    }

    const auto& entries = find->second;

    for(const auto& entry : entries)
    {
        switch(entry.type)
        {
            case input_type::axis:
            {
            }
            break;

            case input_type::button:
            {
                if(device.is_released(entry.value))
                {
                    return true;
                }
            }
            break;

            default:
                break;
        }
    }

    return false;
}

auto mouse_action_map::is_down(const action_id_t& action, const mouse& device) const -> bool
{
    const auto& find = entries_by_action_id_.find(action);

    if(find == entries_by_action_id_.end())
    {
        return false;
    }

    const auto& entries = find->second;

    for(const auto& entry : entries)
    {
        switch(entry.type)
        {
            case input_type::axis:
            {
            }
            break;

            case input_type::button:
            {
                if(device.is_down(entry.value))
                {
                    return true;
                }
            }
            break;

            default:
                break;
        }
    }

    return false;
}


//  ----------------------------------------------------------------------------
namespace
{
/// Adds @p entry unless the action has it already: mapping a binding twice keeps one.
void add_unique(std::vector<mouse_action_map::mouse_entry>& entries, const mouse_action_map::mouse_entry& entry)
{
    if(std::find(entries.begin(), entries.end(), entry) == entries.end())
    {
        entries.push_back(entry);
    }
}
} // namespace

void mouse_action_map::map(const action_id_t& action, mouse_button button)
{
    mouse_entry entry = {};
    entry.type = input_type::button;
    entry.value = static_cast<uint32_t>(button);
    add_unique(entries_by_action_id_[action], entry);
}

void mouse_action_map::map(const action_id_t& action,
         mouse_axis axis,
         axis_range range)
{
    mouse_entry entry = {};
    entry.type = input_type::axis;
    entry.range = range;
    entry.value = static_cast<uint32_t>(axis);
    add_unique(entries_by_action_id_[action], entry);
}
} // namespace input
