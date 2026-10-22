#pragma once

#include "button_state_map.hpp"
#include "gamepad.hpp"
#include "gamepad_axis.hpp"
#include "gamepad_button.hpp"

#include <array>
#include <string>

namespace input
{
/**
 * @brief A gamepad that scripts drive, for on-screen controls such as a touch stick. The action map
 * reads it as it reads a connected gamepad, so actions mapped to gamepad axes and buttons answer to it.
 *
 * What scripts set during a frame reaches the game with the next frame, as a device's input does:
 * update() applies it at the frame boundary. Every reader of a frame sees the same state, whatever
 * order the scripts run in, and a press set and released within one frame still reads as pressed for
 * one frame.
 */
class virtual_gamepad : public gamepad
{
public:
    /// Sets an axis from -1 to 1 (clamped). Sticks read negative to the left and up, as a gamepad's.
    void set_axis(gamepad_axis axis, float value);
    /// Holds a button down or lets it go.
    void set_button(gamepad_button button, bool is_down);
    /// Centers every axis and lets go of every button, set or applied: nothing reads as held after it.
    void release_all();
    /// The frame boundary: applies what was set since the last call.
    void update();

    auto get_axis_value(uint32_t axis) const -> float override;
    auto get_button_state(uint32_t button) const -> button_state override;
    auto get_name() const -> const std::string& override;
    auto is_connected() const -> bool override;
    auto is_down(uint32_t button) const -> bool override;
    auto is_pressed(uint32_t button) const -> bool override;
    auto is_released(uint32_t button) const -> bool override;

private:
    static constexpr size_t AXIS_COUNT = static_cast<size_t>(gamepad_axis::count);
    static constexpr size_t BUTTON_COUNT = static_cast<size_t>(gamepad_button::count);

    struct button_request
    {
        bool is_down{};
        /// A press was set since the last update, even if the button was let go again.
        bool was_pressed{};
    };

    std::array<float, AXIS_COUNT> requested_axes_{};
    std::array<float, AXIS_COUNT> axes_{};
    std::array<button_request, BUTTON_COUNT> requested_buttons_{};
    button_state_map button_state_map_;
};
} // namespace input
