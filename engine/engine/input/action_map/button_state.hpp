#pragma once

namespace input
{
enum class button_state
{
    up,
    pressed,
    down,
    released,
};

//  ----------------------------------------------------------------------------
inline auto button_state_to_analog_value(const button_state state) -> float
{
    switch(state)
    {
        case button_state::down:
        case button_state::pressed:
            return 1.0f;

        case button_state::up:
        case button_state::released:
        default:
            return 0.0f;
    }
}

//  ----------------------------------------------------------------------------
inline auto button_state_to_digital_value(const button_state state) -> bool
{
    switch(state)
    {
        case button_state::down:
        case button_state::pressed:
            return true;

        case button_state::up:
        case button_state::released:
        default:
            return false;
    }
}
} // namespace input
