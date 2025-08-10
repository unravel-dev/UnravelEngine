#pragma once

namespace input
{
enum class key_state
{
    up,
    pressed,
    down,
    released,
};

//  ----------------------------------------------------------------------------
inline auto key_state_to_analog_value(const key_state state) -> float
{
    switch(state)
    {
        case key_state::down:
        case key_state::pressed:
            return 1.0;

        case key_state::up:
        case key_state::released:
        default:
            return 0.0;
    }
}

//  ----------------------------------------------------------------------------
inline auto key_state_to_digital_value(const key_state state) -> bool
{
    switch(state)
    {
        case key_state::down:
        case key_state::pressed:
            return true;

        case key_state::up:
        case key_state::released:
        default:
            return false;
    }
}
} // namespace input
