#pragma once

#include "key.hpp"
#include "key_state.hpp"
#include <iostream>
#include <map>

namespace input
{
class key_state_map
{
    std::map<key_code, key_state> map_;

public:
    void clear()
    {
        map_.clear();
    }

    auto get_state(key_code key) const -> key_state
    {
        return map_.at(key);
    }

    auto get_state(key_code key, const key_state default_state) const -> key_state
    {
        const auto& find = map_.find(key);
        if(find == map_.end())
        {
            return default_state;
        }

        return find->second;
    }

    void set_state(key_code key, key_state state)
    {
        const key_state last_state = get_state(key, key_state::up);

        if(state == key_state::pressed)
        {
            if(last_state == key_state::down || last_state == key_state::pressed)
            {
                state = key_state::down;
            }
        }

        map_[key] = state;
    }

    void update()
    {
        //  Update Pressed and Released states to Down and Up
        for(auto& p : map_)
        {
            switch(p.second)
            {
                case key_state::pressed:
                    p.second = key_state::down;
                    break;

                case key_state::released:
                    p.second = key_state::up;
                    break;
                default:
                    break;
            }
        }
    }
};
} // namespace input
