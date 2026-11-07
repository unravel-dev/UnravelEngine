#pragma once

#include <map>
#include <set>

namespace input
{
/**
 * @brief The state of each key or button: pressed in the frame it goes down, down while it is held, released in the
 * frame it is let go, then up.
 *
 * Let go in the frame it went down, it reads pressed that frame and released the next, so a tap shorter than a frame
 * still counts; pressed again in that same frame, it stays pressed.
 * @tparam Key The key or button id.
 * @tparam State An enum with up, pressed, down and released (key_state, button_state).
 */
template<typename Key, typename State>
class input_state_map
{
public:
    void clear()
    {
        map_.clear();
        deferred_releases_.clear();
    }

    auto get_state(Key key) const -> State
    {
        return map_.at(key);
    }

    auto get_state(Key key, const State default_state) const -> State
    {
        const auto found = map_.find(key);
        return found == map_.end() ? default_state : found->second;
    }

    void set_state(Key key, State state)
    {
        const State last_state = get_state(key, State::up);
        if(state == State::pressed)
        {
            // Pressed again in the frame it was let go: the release is undone, and the press still counts.
            const bool undoes_release = deferred_releases_.erase(key) != 0;
            if(!undoes_release && (last_state == State::down || last_state == State::pressed))
            {
                state = State::down;
            }
        }
        else if(state == State::released && last_state == State::pressed)
        {
            deferred_releases_.insert(key);
            return;
        }
        map_[key] = state;
    }

    /// Moves to the next frame: pressed becomes down (or released after a tap within the frame), released up.
    void update()
    {
        for(auto& [key, state] : map_)
        {
            if(state == State::pressed)
            {
                state = deferred_releases_.count(key) != 0 ? State::released : State::down;
            }
            else if(state == State::released)
            {
                state = State::up;
            }
        }
        deferred_releases_.clear();
    }

private:
    std::map<Key, State> map_;
    /// Let go in the frame they went down: they read released only in the next frame.
    std::set<Key> deferred_releases_;
};
} // namespace input
