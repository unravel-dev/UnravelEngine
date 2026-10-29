#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/input/input.h>

namespace unravel
{
namespace
{

auto internal_m2n_input_get_analog_value(const std::string& name) -> float
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.get_analog_value(name);
}

auto internal_m2n_input_get_digital_value(const std::string& name) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.get_digital_value(name);
}

auto internal_m2n_input_is_pressed(const std::string& name) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.is_pressed(name);
}

auto internal_m2n_input_is_released(const std::string& name) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.is_released(name);
}

auto internal_m2n_input_is_down(const std::string& name) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.is_down(name);
}

auto internal_m2n_input_is_key_pressed(input::key_code code) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_keyboard().is_pressed(code);
}

auto internal_m2n_input_is_key_released(input::key_code code) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_keyboard().is_released(code);
}

auto internal_m2n_input_is_key_down(input::key_code code) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_keyboard().is_down(code);
}

auto internal_m2n_input_is_mouse_button_pressed(int32_t button) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_mouse().is_pressed(button);
}

auto internal_m2n_input_is_mouse_button_released(int32_t button) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_mouse().is_released(button);
}

auto internal_m2n_input_is_mouse_button_down(int32_t button) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_mouse().is_down(button);
}

auto internal_m2n_input_get_mouse_position() -> math::vec2
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    auto coord = input.manager.get_mouse().get_position();
    return {coord.x, coord.y};
}

/// A finger as it crosses to C#: the layout of the C# Touch struct (TouchPhase crosses as an int).
struct script_touch
{
    int32_t finger_id{};
    int32_t phase{};
    math::vec2 position{};
    math::vec2 delta_position{};
    float pressure{};
};

auto internal_m2n_input_get_touch_count() -> int32_t
{
    auto& input = engine::context().get_cached<input_system>();
    return static_cast<int32_t>(input.manager.get_touches().get_count());
}

auto to_script_touch(const input::touch_point& point) -> script_touch
{
    script_touch touch;
    touch.finger_id = point.id;
    touch.phase = static_cast<int32_t>(point.phase);
    touch.position = {point.position.x, point.position.y};
    touch.delta_position = {point.delta.x, point.delta.y};
    touch.pressure = point.pressure;
    return touch;
}

auto internal_m2n_input_get_touch(int32_t index) -> script_touch
{
    auto& input = engine::context().get_cached<input_system>();
    const auto& touches = input.manager.get_touches();
    if(index < 0 || static_cast<size_t>(index) >= touches.get_count())
    {
        return {};
    }
    return to_script_touch(touches.get(static_cast<size_t>(index)));
}

auto internal_m2n_input_is_touch_supported() -> bool
{
    auto& input = engine::context().get_cached<input_system>();
    return input.manager.is_touch_supported();
}

auto internal_m2n_input_get_simulate_mouse_with_touches() -> bool
{
    auto& input = engine::context().get_cached<input_system>();
    return input.manager.is_mouse_simulated_with_touches();
}

void internal_m2n_input_set_simulate_mouse_with_touches(bool simulated)
{
    auto& input = engine::context().get_cached<input_system>();
    input.manager.set_mouse_simulated_with_touches(simulated);
}

/// GamepadAxis and GamepadButton cross as ints, the C# enums' underlying type; the gamepad ignores
/// values out of range.
void internal_m2n_virtual_gamepad_set_axis(int32_t axis, float value)
{
    auto& input = engine::context().get_cached<input_system>();
    input.manager.get_virtual_gamepad().set_axis(static_cast<input::gamepad_axis>(axis), value);
}

void internal_m2n_virtual_gamepad_set_button(int32_t button, bool is_down)
{
    auto& input = engine::context().get_cached<input_system>();
    input.manager.get_virtual_gamepad().set_button(static_cast<input::gamepad_button>(button), is_down);
}

void internal_m2n_virtual_gamepad_release_all()
{
    auto& input = engine::context().get_cached<input_system>();
    input.manager.get_virtual_gamepad().release_all();
}

auto internal_m2n_pointer_zone_create() -> uint32_t
{
    auto& input = engine::context().get_cached<input_system>();
    return input.manager.get_pointer_zones().create();
}

void internal_m2n_pointer_zone_destroy(uint32_t id)
{
    auto& input = engine::context().get_cached<input_system>();
    input.manager.get_pointer_zones().destroy(id);
}

void internal_m2n_pointer_zone_set_area(uint32_t id, float x, float y, float width, float height)
{
    auto& input = engine::context().get_cached<input_system>();
    const input::zone area{static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height)};
    input.manager.get_pointer_zones().set_area(id, area);
}

auto internal_m2n_pointer_zone_has_pointer(uint32_t id) -> bool
{
    auto& input = engine::context().get_cached<input_system>();
    return input.manager.get_pointer_zones().get_pointer(id) != nullptr;
}

/// The mouse crosses with finger id -1, as it is no finger.
auto internal_m2n_pointer_zone_get_pointer(uint32_t id) -> script_touch
{
    constexpr int32_t MOUSE_FINGER_ID = -1;
    auto& input = engine::context().get_cached<input_system>();
    const auto& zones = input.manager.get_pointer_zones();
    const input::touch_point* point = zones.get_pointer(id);
    if(point == nullptr)
    {
        return {};
    }
    script_touch touch = to_script_touch(*point);
    if(zones.is_mouse_pointer(id))
    {
        touch.finger_id = MOUSE_FINGER_ID;
    }
    return touch;
}

/// CursorLockMode crosses as an int: the C# enum's underlying type.
auto internal_m2n_cursor_get_lock_state() -> int32_t
{
    auto& input = engine::context().get_cached<input_system>();
    return int32_t(input.manager.get_cursor().get_lock_mode());
}

void internal_m2n_cursor_set_lock_state(int32_t mode)
{
    auto& input = engine::context().get_cached<input_system>();
    const bool is_known = mode >= int32_t(input::cursor_lock_mode::none) && mode <= int32_t(input::cursor_lock_mode::confined);
    input.manager.get_cursor().set_lock_mode(is_known ? input::cursor_lock_mode(mode) : input::cursor_lock_mode::none);
}

auto internal_m2n_cursor_get_visible() -> bool
{
    auto& input = engine::context().get_cached<input_system>();
    return input.manager.get_cursor().is_visible();
}

void internal_m2n_cursor_set_visible(bool visible)
{
    auto& input = engine::context().get_cached<input_system>();
    input.manager.get_cursor().set_visible(visible);
}

} // namespace

void register_input_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.Input");
    reg.add_internal_call("internal_m2n_input_get_analog_value",
                            script_internal_call(internal_m2n_input_get_analog_value));
    reg.add_internal_call("internal_m2n_input_get_digital_value",
                            script_internal_call(internal_m2n_input_get_digital_value));
    reg.add_internal_call("internal_m2n_input_is_pressed", script_internal_call(internal_m2n_input_is_pressed));
    reg.add_internal_call("internal_m2n_input_is_released", script_internal_call(internal_m2n_input_is_released));
    reg.add_internal_call("internal_m2n_input_is_down", script_internal_call(internal_m2n_input_is_down));
    reg.add_internal_call("internal_m2n_input_is_key_pressed", script_internal_call(internal_m2n_input_is_key_pressed));
    reg.add_internal_call("internal_m2n_input_is_key_released", script_internal_call(internal_m2n_input_is_key_released));
    reg.add_internal_call("internal_m2n_input_is_key_down", script_internal_call(internal_m2n_input_is_key_down));
    reg.add_internal_call("internal_m2n_input_is_mouse_button_pressed",
                            script_internal_call(internal_m2n_input_is_mouse_button_pressed));
    reg.add_internal_call("internal_m2n_input_is_mouse_button_released",
                            script_internal_call(internal_m2n_input_is_mouse_button_released));
    reg.add_internal_call("internal_m2n_input_is_mouse_button_down",
                            script_internal_call(internal_m2n_input_is_mouse_button_down));
    reg.add_internal_call("internal_m2n_input_get_mouse_position",
                            script_internal_call(internal_m2n_input_get_mouse_position));
    reg.add_internal_call("internal_m2n_input_get_touch_count", script_internal_call(internal_m2n_input_get_touch_count));
    reg.add_internal_call("internal_m2n_input_get_touch", script_internal_call(internal_m2n_input_get_touch));
    reg.add_internal_call("internal_m2n_input_is_touch_supported", script_internal_call(internal_m2n_input_is_touch_supported));
    reg.add_internal_call("internal_m2n_input_get_simulate_mouse_with_touches",
                          script_internal_call(internal_m2n_input_get_simulate_mouse_with_touches));
    reg.add_internal_call("internal_m2n_input_set_simulate_mouse_with_touches",
                          script_internal_call(internal_m2n_input_set_simulate_mouse_with_touches));

    auto gamepad = dotnet::internal_call_registry("Unravel.Core.VirtualGamepad");
    gamepad.add_internal_call("internal_m2n_virtual_gamepad_set_axis", script_internal_call(internal_m2n_virtual_gamepad_set_axis));
    gamepad.add_internal_call("internal_m2n_virtual_gamepad_set_button",
                              script_internal_call(internal_m2n_virtual_gamepad_set_button));
    gamepad.add_internal_call("internal_m2n_virtual_gamepad_release_all",
                              script_internal_call(internal_m2n_virtual_gamepad_release_all));

    auto zone = dotnet::internal_call_registry("Unravel.Core.PointerZone");
    zone.add_internal_call("internal_m2n_pointer_zone_create", script_internal_call(internal_m2n_pointer_zone_create));
    zone.add_internal_call("internal_m2n_pointer_zone_destroy", script_internal_call(internal_m2n_pointer_zone_destroy));
    zone.add_internal_call("internal_m2n_pointer_zone_set_area", script_internal_call(internal_m2n_pointer_zone_set_area));
    zone.add_internal_call("internal_m2n_pointer_zone_has_pointer", script_internal_call(internal_m2n_pointer_zone_has_pointer));
    zone.add_internal_call("internal_m2n_pointer_zone_get_pointer", script_internal_call(internal_m2n_pointer_zone_get_pointer));

    auto cursor = dotnet::internal_call_registry("Unravel.Core.Cursor");
    cursor.add_internal_call("internal_m2n_cursor_get_lock_state", script_internal_call(internal_m2n_cursor_get_lock_state));
    cursor.add_internal_call("internal_m2n_cursor_set_lock_state", script_internal_call(internal_m2n_cursor_set_lock_state));
    cursor.add_internal_call("internal_m2n_cursor_get_visible", script_internal_call(internal_m2n_cursor_get_visible));
    cursor.add_internal_call("internal_m2n_cursor_set_visible", script_internal_call(internal_m2n_cursor_set_visible));
}

} // namespace unravel
