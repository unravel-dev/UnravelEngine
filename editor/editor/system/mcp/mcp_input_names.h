#pragma once

#include <engine/input/action_map/gamepad_axis.hpp>
#include <engine/input/action_map/gamepad_button.hpp>
#include <engine/input/action_map/to_string.hpp>
#include <ospp/keyboard.h>
#include <ospp/mouse.h>

#include <map>
#include <string>
#include <hpp/string_view.hpp>

namespace unravel::mcp
{

/// Lower-cases and drops spaces, dashes and underscores, so "Left Shift", "left_shift" and
/// "leftshift" compare equal. Every MCP input name (keys, buttons, axes) is matched this way.
auto normalize_input_name(hpp::string_view name) -> std::string;

/**
 * @brief Resolves a key name to the platform's key code.
 *
 * Accepts the platform layer's own names ("W", "Space", "Left Shift") in any case and spacing,
 * plus common short forms ("lshift", "esc", "enter"). On failure `error` lists near misses.
 */
auto parse_key_name(const std::string& name, os::key::code& out, std::string& error) -> bool;

/**
 * @brief Resolves a mouse button name: the engine's own (Left, Right, Middle, Button 4, Button 5, as action maps name
 *        them) in any case and spacing, or x1 / x2 for the side buttons 4 and 5.
 */
auto parse_mouse_button(hpp::string_view name, os::mouse::button& out) -> bool;

/// Normalized name -> value over the first @p count values of an input enum, named by the input library's own
/// to_string, so the names a caller passes are the names the editor and the action maps show.
template<typename Enum>
auto build_input_name_table(uint32_t count) -> std::map<std::string, Enum>
{
    std::map<std::string, Enum> table;
    for(uint32_t i = 0; i < count; ++i)
    {
        const auto value = static_cast<Enum>(i);
        const auto name = input::to_string(value);
        if(name.empty() || name == "Unknown")
        {
            continue;
        }
        table.emplace(normalize_input_name(name), value);
    }
    return table;
}

/// Gamepad buttons by normalized name ("south", "dpadup", "leftshoulder").
auto gamepad_button_table() -> const std::map<std::string, input::gamepad_button>&;
/// Gamepad axes by normalized name ("leftx", "righttrigger").
auto gamepad_axis_table() -> const std::map<std::string, input::gamepad_axis>&;

} // namespace unravel::mcp
