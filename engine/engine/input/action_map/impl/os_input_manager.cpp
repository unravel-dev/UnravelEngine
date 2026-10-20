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
    ++input_frame_;

    //  Update Pressed and Released states to Down and Up
    keyboard_->update();

    mouse_->update();

    touches_.update();

    if(virtual_mouse_position_)
    {
        mouse_->set_position(remap_to_work_zone(*virtual_mouse_position_));
        return;
    }

    auto pos = os::mouse::get_position();
    mouse_->set_position(remap_to_work_zone({pos.x, pos.y}));
    // This frame's motion is read; the next one is measured from where the cursor is put.
    if(const auto held = cursor_.apply({pos.x, pos.y}, get_work_zone()))
    {
        mouse_->rebase_position(remap_to_work_zone(*held));
    }
}

auto os_input_manager::get_cursor() -> os_cursor&
{
    return cursor_;
}

auto os_input_manager::get_cursor() const -> const os_cursor&
{
    return cursor_;
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

auto os_input_manager::get_work_zone() const -> zone
{

    if(work_input_zone_)
    {
        return *work_input_zone_;
    }
    if(window_input_zone_)
    {
        return *window_input_zone_;
    }

    return {0, 0, 1024, 768};
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

        case os::events::finger_down:
        case os::events::finger_motion:
        case os::events::finger_up:
        case os::events::finger_canceled:
        {
            on_finger_event(e);
            break;
        }

        case os::events::window:
        {
            if(e.window.type == os::window_event_id::focus_gained)
            {
                cursor_.set_window_focus(true);
            }
            else if(e.window.type == os::window_event_id::focus_lost)
            {
                cursor_.set_window_focus(false);
            }
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

auto os_input_manager::remap_from_work_zone(coord work_pos) -> coord
{
    auto calc_zone = work_input_zone_ ? work_input_zone_ : window_input_zone_;
    if(!calc_zone)
    {
        return work_pos;
    }

    const auto& work_zone = *calc_zone;

    coord global_pos = work_pos;
    if(input_reference_size_ && input_reference_size_->w > 0.0f && input_reference_size_->h > 0.0f)
    {
        global_pos.x = int(float(global_pos.x) * float(work_zone.w) / input_reference_size_->w);
        global_pos.y = int(float(global_pos.y) * float(work_zone.h) / input_reference_size_->h);
    }

    global_pos.x += work_zone.x;
    global_pos.y += work_zone.y;
    return global_pos;
}

void os_input_manager::set_virtual_mouse_position(const hpp::optional<coord>& position)
{
    virtual_mouse_position_ = position;
    cursor_.set_pointer_overridden(position.has_value());
}

void os_input_manager::release_all()
{
    keyboard_->get_key_state_map().clear();
    mouse_->get_button_state_map().clear();
    touches_.cancel_all();
}

auto os_input_manager::get_touches() const -> const os_touches&
{
    return touches_;
}

auto os_input_manager::map_window_position(coord window_position) -> coord
{
    const zone window = window_input_zone_ ? *window_input_zone_ : get_work_zone();
    return remap_to_work_zone({window.x + window_position.x, window.y + window_position.y});
}

auto os_input_manager::map_finger_position(const os::touch_finger_event& finger) -> coord
{
    const zone window = window_input_zone_ ? *window_input_zone_ : get_work_zone();
    return map_window_position({static_cast<int>(finger.x * static_cast<float>(window.w)),
                                static_cast<int>(finger.y * static_cast<float>(window.h))});
}

void os_input_manager::on_finger_event(const os::event& e)
{
    const os::touch_finger_event& finger = e.finger;
    const coord position = map_finger_position(finger);
    switch(e.type)
    {
        case os::events::finger_down:
            // As a mouse press, a finger that lands outside the work zone is not the game's.
            if(is_inside_work_zone(position))
            {
                touches_.begin(finger.touch_id, finger.finger_id, position, finger.pressure);
            }
            break;
        case os::events::finger_motion:
            touches_.move(finger.touch_id, finger.finger_id, position, finger.pressure);
            break;
        case os::events::finger_up:
            touches_.end(finger.touch_id, finger.finger_id, position, false);
            break;
        case os::events::finger_canceled:
            touches_.end(finger.touch_id, finger.finger_id, position, true);
            break;
        default:
            break;
    }
}

auto os_input_manager::get_virtual_mouse_position() const -> const hpp::optional<coord>&
{
    return virtual_mouse_position_;
}

auto os_input_manager::get_input_frame() const -> uint64_t
{
    return input_frame_;
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
    cursor_.set_input_allowed(allowed);

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
