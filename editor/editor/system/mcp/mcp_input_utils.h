#pragma once

#include <context/context.hpp>
#include <engine/input/input.h>
#include <ospp/mouse.h>

#include <cstdint>
#include <functional>
#include <string>

namespace unravel
{
class mcp_manager;
}

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
 * @brief Gets the Game panel ready for synthesized input, from a worker thread: focuses it unless
 *        @p focus_game_panel is false, then waits until it accepts input. Focus lands during the
 *        next UI pass, after that frame's input was read, so events pushed right away were dropped.
 * @param window_id Receives the window the events go to.
 */
auto prepare_game_input(mcp_manager& mcp,
                        rtti::context& ctx,
                        bool focus_game_panel,
                        uint32_t& window_id,
                        std::string& error) -> bool;

/**
 * @brief Runs @p push on the main thread, where OS events must be queued. Worker thread only.
 */
auto push_on_main(mcp_manager& mcp, const std::function<void()>& push) -> bool;

/**
 * @brief Moves the virtual pointer to @p game_position (pixels in the game's render target, the
 *        space a script's Input.mousePosition reads) and tells the UI, so hover follows it.
 *        Refuses a point outside the Game panel's view: a press there lands on the editor.
 *        Main thread.
 */
auto move_game_pointer(rtti::context& ctx, uint32_t window_id, input::coord game_position, std::string& error)
    -> bool;

/**
 * @brief Presses @p button at the virtual pointer, holds it @p frames input frames and releases it.
 *        The release is sent even when the frames do not pass, so no button stays held. Worker
 *        thread only.
 */
auto click_mouse_button(mcp_manager& mcp,
                        rtti::context& ctx,
                        uint32_t window_id,
                        os::mouse::button button,
                        uint64_t frames,
                        std::string& error) -> bool;

/**
 * @brief Queues the characters typing produces, for the focused text field.
 */
void push_text_input_event(uint32_t window_id, const std::string& text);

} // namespace unravel::mcp
