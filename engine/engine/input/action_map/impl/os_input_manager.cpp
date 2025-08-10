#include "os_input_manager.hpp"
#include "../bimap.hpp"
#include "os_key_map.hpp"
#include <cassert>
#include <logging/logging.h>

namespace input
{

namespace
{
auto get_key_map() -> bimap<key_code, int>&
{
    static bimap<key_code, int> keyMap;
    return keyMap;
}
} // namespace

//  ----------------------------------------------------------------------------
os_input_manager::os_input_manager()
{
    initialize_os_key_map(get_key_map());
}

void os_input_manager::init()
{
    for(uint32_t index = 0; index < get_max_gamepads(); ++index)
    {
        auto gamepad = std::make_shared<os_gamepad>(index);
        devices_.emplace_back(gamepad);
        gamepads_[index] = gamepad.get();
    }

    auto keyboard = std::make_shared<os_keyboard>();
    devices_.emplace_back(keyboard);
    keyboard_ = keyboard.get();

    auto mouse = std::make_shared<os_mouse>();
    devices_.emplace_back(mouse);
    mouse_ = mouse.get();
}

auto os_input_manager::get_all_devices() const -> const std::vector<std::shared_ptr<input_device>>&
{
    return devices_;
}

//  ----------------------------------------------------------------------------
auto os_input_manager::get_gamepad(uint32_t index) const -> const gamepad&
{
    return *gamepads_.at(index);
}

auto os_input_manager::get_max_gamepads() const -> uint32_t
{
    return 16;
}

//  ----------------------------------------------------------------------------
auto os_input_manager::get_mouse() const -> const mouse&
{
    return *mouse_;
}

//  ----------------------------------------------------------------------------
auto os_input_manager::get_keyboard() const -> const keyboard&
{
    return *keyboard_;
}

//  ----------------------------------------------------------------------------
void os_input_manager::before_events_update()
{
    //  Update Pressed and Released states to Down and Up
    keyboard_->update();

    mouse_->update();

    auto pos = os::mouse::get_position();
    mouse_->set_position(remap_to_work_zone({pos.x, pos.y}));
}

void os_input_manager::after_events_update()
{
    //  Update gamepads
    for(auto& kvp : gamepads_)
    {
        auto& gamepad = kvp.second;
        gamepad->update();
    }
}
void os_input_manager::set_window_zone(const zone& window_zone)
{
    window_input_zone_ = window_zone;
}

void os_input_manager::set_work_zone(const zone& work_zone)
{
    work_input_zone_ = work_zone;
}

void os_input_manager::set_reference_size(const input_reference_size& reference_size)
{
    input_reference_size_ = reference_size;
}

void os_input_manager::on_os_event(const os::event& e)
{
    switch(e.type)
    {
        case os::events::key_down:
        {
            auto& state_map = keyboard_->get_key_state_map();
            key_code key = get_key_map().get_key(e.key.code, key_code::unknown);
            state_map.set_state(key, key_state::pressed);
            break;
        }
        case os::events::key_up:
        {
            auto& state_map = keyboard_->get_key_state_map();
            key_code key = get_key_map().get_key(e.key.code, key_code::unknown);
            state_map.set_state(key, key_state::released);
            break;
        }

        case os::events::mouse_button:
        {
            auto& state_map = mouse_->get_button_state_map();

            button_state state{};
            switch(e.button.state_id)
            {
                case os::state::pressed:
                {
                    state = button_state::pressed;
                    break;
                }
                case os::state::released:
                {
                    state = button_state::released;
                    break;
                }
                default:
                    break;
            }

            mouse_button mouse_button{mouse_button::left_button};
            switch(e.button.button)
            {
                case os::mouse::button::left:
                {
                    mouse_button = mouse_button::left_button;
                    break;
                }
                case os::mouse::button::right:
                {
                    mouse_button = mouse_button::right_button;
                    break;
                }
                case os::mouse::button::middle:
                {
                    mouse_button = mouse_button::middle_button;
                    break;
                }
                case os::mouse::button::x1:
                {
                    mouse_button = mouse_button::button4;
                    break;
                }

                case os::mouse::button::x2:
                {
                    mouse_button = mouse_button::button5;
                    break;
                }
                default:
                    break;
            }

            if(is_inside_work_zone(mouse_->get_position()))
            {
                state_map.set_state(static_cast<uint32_t>(mouse_button), state);
            }
            break;
        }

        case os::events::mouse_motion:
        {
            break;
        }

        case os::events::mouse_wheel:
        {
            if(is_inside_work_zone(mouse_->get_position()))
            {
                mouse_->set_scroll(e.wheel.y);
            }
            break;
        }

        case os::events::gamepad_added:
        {
            for(auto& kvp : gamepads_)
            {
                auto& gamepad = kvp.second;
                gamepad->refresh_device();
            }
            break;
        }
        case os::events::gamepad_removed:
        {
            for(auto& kvp : gamepads_)
            {
                auto& gamepad = kvp.second;
                gamepad->refresh_device();
            }
            break;
        }

        default:
            break;
    }
}

auto os_input_manager::remap_to_work_zone(coord global_pos) -> coord
{
    auto calc_zone = work_input_zone_ ? work_input_zone_ : window_input_zone_;
    // Ensure both zones are defined
    if(!calc_zone)
    {
        return global_pos;
    }

    const auto& work_zone = *calc_zone;

    coord remapped_pos{};
    remapped_pos.x = global_pos.x - work_zone.x;
    remapped_pos.y = global_pos.y - work_zone.y;

    if(input_reference_size_)
    {
        remapped_pos.x *= float(input_reference_size_->w) / float(work_zone.w);
        remapped_pos.y *= float(input_reference_size_->h) / float(work_zone.h);
    }

    return remapped_pos;
}

auto os_input_manager::is_inside_work_zone(coord pos) -> bool
{
    auto calc_zone = work_input_zone_ ? work_input_zone_ : window_input_zone_;
    // Ensure both zones are defined
    if(!calc_zone)
    {
        return true;
    }

    const auto& work_zone = *calc_zone;
    auto left = 0;
    auto right = work_zone.w;
    auto top = 0;
    auto bottom = work_zone.h;


    if(input_reference_size_)
    {
        float scale_x = float(input_reference_size_->w) / float(work_zone.w);
        float scale_y = float(input_reference_size_->h) / float(work_zone.h);
        left *= scale_x;
        right *= scale_x;
        top *= scale_y;
        bottom *= scale_y;
    }

    return pos.x >= left && pos.x <= right && pos.y >= top && pos.y <= bottom;
}

void os_input_manager::set_is_input_allowed(bool allowed)
{
    is_input_allowed_ = allowed;

    for(const auto& dev : devices_)
    {
        dev->set_is_input_allowed(allowed);
    }
}

auto os_input_manager::is_input_allowed() const -> bool
{
    return is_input_allowed_;
}
} // namespace input
