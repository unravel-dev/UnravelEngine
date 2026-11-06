#pragma once

#include "key.hpp"
#include "key_state.hpp"
#include <iostream>
#include <map>
#include <set>

namespace input
{
class key_state_map
{
    std::map<key_code, key_state> map_;
    /// Let go in the frame they went down: they read pressed this frame and released the next, so a tap
    /// shorter than a frame still counts.
    std::set<key_code> deferred_releases_;

public:
    void clear()
    {
        map_.clear();
        deferred_releases_.clear();
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
            deferred_releases_.erase(key);
            if(last_state == key_state::down || last_state == key_state::pressed)
            {
                state = key_state::down;
            }
        }
        else if(state == key_state::released && last_state == key_state::pressed)
        {
            deferred_releases_.insert(key);
            return;
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
                    p.second = deferred_releases_.count(p.first) != 0 ? key_state::released : key_state::down;
                    break;

                case key_state::released:
                    p.second = key_state::up;
                    break;
                default:
                    break;
            }
        }
        deferred_releases_.clear();
    }
};
} // namespace input
