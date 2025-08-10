#pragma once

#include "button_state.hpp"
#include <iostream>
#include <map>

namespace input
{
class button_state_map
{
    std::map<unsigned, button_state> map_;

public:
    void clear()
    {
        map_.clear();
    }

    auto get_state(uint32_t button) const -> button_state
    {
        return map_.at(button);
    }

    auto get_state(uint32_t button, const button_state default_state) const -> button_state
    {
        const auto& find = map_.find(button);
        if(find == map_.end())
        {
            return default_state;
        }

        return find->second;
    }

    void set_state(uint32_t button, button_state state)
    {
        const button_state last_state = get_state(button, button_state::up);

        if(state == button_state::pressed)
        {
            if(last_state == button_state::down || last_state == button_state::pressed)
            {
                state = button_state::down;
            }
        }

        map_[button] = state;
    }

    void update()
    {
        //  Update Pressed and Released states to Down and Up
        for(auto& p : map_)
        {
            switch(p.second)
            {
                case button_state::pressed:
                    p.second = button_state::down;
                    break;

                case button_state::released:
                    p.second = button_state::up;
                    break;
                default:
                    break;
            }
        }
    }
};
} // namespace input
