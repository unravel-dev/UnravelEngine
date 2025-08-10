#include "keyboard_action_map.hpp"

namespace input
{
//  ----------------------------------------------------------------------------
auto keyboard_action_map::get_analog_value(const action_id_t& action, const keyboard& device) const -> float
{
    const auto& find = entries_by_action_id_.find(action);

    if(find == entries_by_action_id_.end())
    {
        return 0.0f;
    }

    const auto& entries = find->second;

    for(const auto& entry : entries)
    {
        if(device.is_down(entry.key))
        {
            bool modifiers_down = true;
            for(auto modifier : entry.modifiers)
            {
                if(!device.is_down(modifier))
                {
                    modifiers_down = false;
                    break;
                }
            }

            if(modifiers_down)
            {
                return entry.analog_value;
            }
        }
    }

    return 0.0f;
}

//  ----------------------------------------------------------------------------
auto keyboard_action_map::get_digital_value(const action_id_t& action, const keyboard& device) const -> bool
{
    const auto& find = entries_by_action_id_.find(action);

    if(find == entries_by_action_id_.end())
    {
        return false;
    }

    const auto& entries = find->second;

    for(const auto& entry : entries)
    {
        if(device.is_down(entry.key))
        {
            bool modifiers_down = true;
            for(auto modifier : entry.modifiers)
            {
                if(!device.is_down(modifier))
                {
                    modifiers_down = false;
                    break;
                }
            }

            if(modifiers_down)
            {
                return true;
            }
        }
    }

    return false;
}

//  ----------------------------------------------------------------------------
auto keyboard_action_map::is_pressed(const action_id_t& action, const keyboard& device) const -> bool
{
    const auto& find = entries_by_action_id_.find(action);

    if(find == entries_by_action_id_.end())
    {
        return false;
    }

    const auto& entries = find->second;

    for(const auto& entry : entries)
    {
        if(device.is_pressed(entry.key))
        {
            bool modifiers_down = true;
            for(auto modifier : entry.modifiers)
            {
                if(!device.is_down(modifier))
                {
                    modifiers_down = false;
                    break;
                }
            }

            if(modifiers_down)
            {
                return true;
            }
        }
    }

    return false;
}

auto keyboard_action_map::is_released(const action_id_t& action, const keyboard& device) const -> bool
{
    const auto& find = entries_by_action_id_.find(action);

    if(find == entries_by_action_id_.end())
    {
        return false;
    }

    const auto& entries = find->second;

    for(const auto& entry : entries)
    {
        if(device.is_released(entry.key))
        {
            bool modifiers_down = true;
            for(auto modifier : entry.modifiers)
            {
                if(!device.is_down(modifier))
                {
                    modifiers_down = false;
                    break;
                }
            }

            if(modifiers_down)
            {
                return true;
            }
        }
    }

    return false;
}

auto keyboard_action_map::is_down(const action_id_t& action, const keyboard& device) const -> bool
{
    const auto& find = entries_by_action_id_.find(action);

    if(find == entries_by_action_id_.end())
    {
        return false;
    }

    const auto& entries = find->second;

    for(const auto& entry : entries)
    {
        if(device.is_down(entry.key))
        {
            bool modifiers_down = true;
            for(auto modifier : entry.modifiers)
            {
                if(!device.is_down(modifier))
                {
                    modifiers_down = false;
                    break;
                }
            }

            if(modifiers_down)
            {
                return true;
            }
        }
    }

    return false;
}

//  ----------------------------------------------------------------------------
void keyboard_action_map::map(const action_id_t& action, key_code key, float analog_value)
{
    map(action, key, {}, analog_value);
}

void keyboard_action_map::map(const action_id_t& action, key_code key, const std::vector<key_code>& modifiers, float analog_value)
{
    key_entry entry = {};
    entry.key = key;
    entry.modifiers = modifiers;
    entry.analog_value = analog_value;

    entries_by_action_id_[action].push_back(entry);
}

} // namespace input
