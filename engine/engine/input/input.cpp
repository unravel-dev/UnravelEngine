#include "input.h"
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/settings/settings.h>
#include "action_map/gamepad_360.hpp"

#include <logging/logging.h>

namespace unravel
{


auto input_system::get_default_mapping() -> input::action_map
{
    input::action_map mapper;
    // default mapping
    mapper.map("Mouse Left", input::mouse_button::left_button);
    mapper.map("Mouse Right", input::mouse_button::right_button);
    mapper.map("Mouse Middle", input::mouse_button::middle_button);

    mapper.map("Mouse X", input::mouse_axis::x);
    mapper.map("Mouse Y", input::mouse_axis::y);
    mapper.map("Mouse ScrollWheel", input::mouse_axis::scroll);

    mapper.map("Horizontal", input::key_code::a, -1.0f);
    mapper.map("Horizontal", input::key_code::d, 1.0f);
    mapper.map("Horizontal", input::key_code::left, -1.0f);
    mapper.map("Horizontal", input::key_code::right, 1.0f);

    mapper.map("Horizontal", input::gamepad_360::axis::left_stick_x, input::axis_range::full, -1.0f, 1.0f);

    mapper.map("Vertical", input::key_code::w, 1.0f);
    mapper.map("Vertical", input::key_code::s, -1.0f);
    mapper.map("Vertical", input::key_code::up, 1.0f);
    mapper.map("Vertical", input::key_code::down, -1.0f);
    mapper.map("Vertical", input::gamepad_360::axis::left_stick_y, input::axis_range::full, 1.0f, -1.0f);

    mapper.map("Jump", input::key_code::space, 1.0f);
    mapper.map("Jump", input::gamepad_360::button::a);
    mapper.map("Run", input::gamepad_360::axis::right_trigger, input::axis_range::positive, -1.0f, 1.0f);
    mapper.map("Run", input::key_code::lshift, 1.0f);

    mapper.map("Submit", input::key_code::enter, 1.0f);
    mapper.map("Cancel", input::key_code::escape, 1.0f);

    return mapper;
}

auto input_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    manager.init();

    return true;
}

auto input_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

auto input_system::get_analog_value(const input::action_id_t& action) const -> float
{
    const auto& ctx = engine::context();
    const auto& set = ctx.get<settings>();
    float analog = 0.0f;

    for(const auto& device : manager.get_all_devices())
    {
        analog += set.input.actions.get_analog_value(action, *device);
    }

    return analog;
}

auto input_system::get_digital_value(const input::action_id_t& action) const -> bool
{
    const auto& ctx = engine::context();
    const auto& set = ctx.get<settings>();

    bool digital = false;

    for(const auto& device : manager.get_all_devices())
    {
        digital |= set.input.actions.get_digital_value(action, *device);
    }

    return digital;
}
auto input_system::is_pressed(const input::action_id_t& action) const -> bool
{
    const auto& ctx = engine::context();
    const auto& set = ctx.get<settings>();

    bool pressed = false;

    for(const auto& device : manager.get_all_devices())
    {
        pressed |= set.input.actions.is_pressed(action, *device);
    }

    return pressed;
}

auto input_system::is_released(const input::action_id_t& action) const -> bool
{
    const auto& ctx = engine::context();
    const auto& set = ctx.get<settings>();

    bool released = false;

    for(const auto& device : manager.get_all_devices())
    {
        released |= set.input.actions.is_released(action, *device);
    }

    return released;
}

auto input_system::is_down(const input::action_id_t& action) const -> bool
{
    const auto& ctx = engine::context();
    const auto& set = ctx.get<settings>();

    bool down = false;

    for(const auto& device : manager.get_all_devices())
    {
        down |= set.input.actions.is_down(action, *device);
    }

    return down;
}

} // namespace unravel
