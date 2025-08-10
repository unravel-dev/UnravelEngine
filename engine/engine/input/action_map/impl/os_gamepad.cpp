#include "os_gamepad.hpp"
#include <cmath>
#include <logging/logging.h>

namespace input
{

// ----------------------------------------------------------------------------
os_gamepad::os_gamepad(int index) : index_(index), name_("Gamepad" + std::to_string(index_ + 1))
{
}

// ----------------------------------------------------------------------------
os_gamepad::~os_gamepad()
{
    close_device();
}

void os_gamepad::open_device()
{
    device_ = os::gamepad::open_device(index_);

    if(device_.data)
    {
        name_ = os::gamepad::get_device_name(device_);

        APPLOG_INFO("Joystick connected ({}).", name_);
    }
}

void os_gamepad::close_device()
{
    if(device_.data)
    {
        os::gamepad::close_device(device_);

        APPLOG_WARNING("Joystick disconnected ({}).", name_);
        device_ = {};
    }
}

void os_gamepad::refresh_device()
{
    refresh_device_ = true;
}

// ----------------------------------------------------------------------------
void os_gamepad::update()
{
    if(refresh_device_)
    {
        close_device();
    }

    // If we never opened or it failed, try again or bail:
    if(!device_.data)
    {
        open_device();
    }

    refresh_device_ = false;

    if(!device_.data)
    {
        // Clear states if it’s still not available
        button_state_map_.clear();
        axis_map_.clear();

        return;
    }

    // 1) Update the button_state_map to handle transitions
    button_state_map_.update();

    uint32_t buttons_count = os::gamepad::get_buttons_count(device_);
    for(uint32_t b = 0; b < buttons_count; ++b)
    {
        auto state = os::gamepad::get_button_state(device_, b);

        button_state bs = button_state::up;
        if(state == os::gamepad::button_state::pressed)
        {
            bs = button_state::pressed;
        }

        button_state_map_.set_state(b, bs);
    }

    uint32_t axis_count = os::gamepad::get_axis_count(device_);
    for(int a = 0; a < axis_count; ++a)
    {
        float normalized = os::gamepad::get_axis_value_normalized(device_, a);

        axis_map_[a] = normalized;
    }
}

// ----------------------------------------------------------------------------
auto os_gamepad::get_axis_value(uint32_t axis) const -> float
{
    if(!is_input_allowed())
    {
        return 0.0f;
    }
    auto it = axis_map_.find(axis);
    if(it == axis_map_.end())
    {
        return 0.0f;
    }

    return it->second;
}

// ----------------------------------------------------------------------------
auto os_gamepad::get_button_state(uint32_t button) const -> button_state
{
    return button_state_map_.get_state(button, button_state::up);
}

// ----------------------------------------------------------------------------
auto os_gamepad::get_name() const -> const std::string&
{
    return name_;
}

// ----------------------------------------------------------------------------
auto os_gamepad::is_connected() const -> bool
{
    return os::gamepad::is_device_connected(device_);
}

// ----------------------------------------------------------------------------
auto os_gamepad::is_down(uint32_t button) const -> bool
{
    if(!is_input_allowed())
        return false;

    button_state st = button_state_map_.get_state(button, button_state::up);
    return (st == button_state::down || st == button_state::pressed);
}

// ----------------------------------------------------------------------------
auto os_gamepad::is_pressed(uint32_t button) const -> bool
{
    if(!is_input_allowed())
        return false;

    return button_state_map_.get_state(button, button_state::up) == button_state::pressed;
}

// ----------------------------------------------------------------------------
auto os_gamepad::is_released(uint32_t button) const -> bool
{
    if(!is_input_allowed())
        return false;

    return button_state_map_.get_state(button, button_state::up) == button_state::released;
}

} // namespace input
