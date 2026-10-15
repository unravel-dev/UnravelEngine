#pragma once

#include <ospp/keyboard.h>

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

} // namespace unravel::mcp
