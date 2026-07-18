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

} // namespace

void register_input_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.Input");
    reg.add_internal_call("internal_m2n_input_get_analog_value",
                            dotnet_internal_call(internal_m2n_input_get_analog_value));
    reg.add_internal_call("internal_m2n_input_get_digital_value",
                            dotnet_internal_call(internal_m2n_input_get_analog_value));
    reg.add_internal_call("internal_m2n_input_is_pressed", dotnet_internal_call(internal_m2n_input_is_pressed));
    reg.add_internal_call("internal_m2n_input_is_released", dotnet_internal_call(internal_m2n_input_is_released));
    reg.add_internal_call("internal_m2n_input_is_down", dotnet_internal_call(internal_m2n_input_is_down));
    reg.add_internal_call("internal_m2n_input_is_key_pressed", dotnet_internal_call(internal_m2n_input_is_key_pressed));
    reg.add_internal_call("internal_m2n_input_is_key_released", dotnet_internal_call(internal_m2n_input_is_key_released));
    reg.add_internal_call("internal_m2n_input_is_key_down", dotnet_internal_call(internal_m2n_input_is_key_down));
    reg.add_internal_call("internal_m2n_input_is_mouse_button_pressed",
                            dotnet_internal_call(internal_m2n_input_is_mouse_button_pressed));
    reg.add_internal_call("internal_m2n_input_is_mouse_button_released",
                            dotnet_internal_call(internal_m2n_input_is_mouse_button_released));
    reg.add_internal_call("internal_m2n_input_is_mouse_button_down",
                            dotnet_internal_call(internal_m2n_input_is_mouse_button_down));
    reg.add_internal_call("internal_m2n_input_get_mouse_position",
                            dotnet_internal_call(internal_m2n_input_get_mouse_position));
}

} // namespace unravel
