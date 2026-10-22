#include "virtual_gamepad.hpp"

#include <algorithm>

namespace input
{

void virtual_gamepad::set_axis(gamepad_axis axis, float value)
{
    const auto index = static_cast<size_t>(axis);
    if(index >= AXIS_COUNT)
    {
        return;
    }
    requested_axes_[index] = std::clamp(value, -1.0f, 1.0f);
}

void virtual_gamepad::set_button(gamepad_button button, bool is_down)
{
    const auto index = static_cast<size_t>(button);
    if(index >= BUTTON_COUNT)
    {
        return;
    }
    auto& request = requested_buttons_[index];
    request.was_pressed |= is_down && !request.is_down;
    request.is_down = is_down;
}

void virtual_gamepad::release_all()
{
    requested_axes_ = {};
    axes_ = {};
    requested_buttons_ = {};
    button_state_map_.clear();
}

void virtual_gamepad::update()
{
    axes_ = requested_axes_;
    button_state_map_.update();
    for(size_t index = 0; index < BUTTON_COUNT; ++index)
    {
        auto& request = requested_buttons_[index];
        const auto button = static_cast<uint32_t>(index);
        const bool is_held = button_state_to_digital_value(button_state_map_.get_state(button, button_state::up));
        if(!is_held && (request.is_down || request.was_pressed))
        {
            button_state_map_.set_state(button, button_state::pressed);
        }
        else if(is_held && !request.is_down)
        {
            button_state_map_.set_state(button, button_state::released);
        }
        request.was_pressed = false;
    }
}

auto virtual_gamepad::get_axis_value(uint32_t axis) const -> float
{
    if(!is_input_allowed() || axis >= AXIS_COUNT)
    {
        return 0.0f;
    }
    return axes_[axis];
}

auto virtual_gamepad::get_button_state(uint32_t button) const -> button_state
{
    return button_state_map_.get_state(button, button_state::up);
}

auto virtual_gamepad::get_name() const -> const std::string&
{
    static const std::string name = "Virtual Gamepad";
    return name;
}

auto virtual_gamepad::is_connected() const -> bool
{
    return true;
}

auto virtual_gamepad::is_down(uint32_t button) const -> bool
{
    if(!is_input_allowed())
    {
        return false;
    }
    return button_state_to_digital_value(get_button_state(button));
}

auto virtual_gamepad::is_pressed(uint32_t button) const -> bool
{
    if(!is_input_allowed())
    {
        return false;
    }
    return get_button_state(button) == button_state::pressed;
}

auto virtual_gamepad::is_released(uint32_t button) const -> bool
{
    if(!is_input_allowed())
    {
        return false;
    }
    return get_button_state(button) == button_state::released;
}

} // namespace input
