#pragma once

#include <context/context.hpp>
#include <engine/input/input.h>
#include <ospp/mouse.h>
#include <ser20/external/simdjson/simdjson.h>

#include <cstdint>
#include <string>

namespace unravel::mcp
{

/**
 * @brief The id of the editor's main window, the window synthesized input events are addressed to.
 */
auto get_main_window_id(rtti::context& ctx, uint32_t& out, std::string& error) -> bool;

/**
 * @brief The OS input manager the game and the editor read, or null without an input system.
 */
auto get_input_manager(rtti::context& ctx) -> input::os_input_manager*;

/**
 * @brief Waits on the calling (worker) thread until the editor consumed @p frames input frames.
 * @return false when the editor stops pumping frames.
 */
auto wait_input_frames(rtti::context& ctx, uint64_t frames) -> bool;

/**
 * @brief Focuses the Game panel unless the call's "focus_game_panel" argument is false: without
 * focus nothing reaches the game's input.
 */
auto ensure_game_input_allowed(rtti::context& ctx, const simdjson::dom::object& args, std::string& error) -> bool;

/**
 * @brief Queues a mouse button press or release at @p position (screen pixels).
 */
void push_mouse_button_event(uint32_t window_id, os::mouse::button button, bool down, input::coord position);

/**
 * @brief Queues the characters typing produces, for the focused text field.
 */
void push_text_input_event(uint32_t window_id, const std::string& text);

/**
 * @brief Puts the virtual pointer at @p game_position: pixels in the game's render target, the
 * space a script's Input.mousePosition reads.
 */
void set_game_pointer(input::os_input_manager& manager, input::coord game_position);

} // namespace unravel::mcp
