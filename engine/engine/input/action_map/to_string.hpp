#pragma once

#include "coord.hpp"
#include "gamepad_axis.hpp"
#include "gamepad_button.hpp"
#include "key_state.hpp"
#include "mouse_action_map.hpp"
#include "mouse_button.hpp"
#include "point.hpp"
#include <string>

namespace input
{

inline auto to_string(axis_range range) -> const std::string&
{
    switch(range)
    {
        case axis_range::full:
        {
            static const std::string text = "Full";
            return text;
        }
        case axis_range::positive:
        {
            static const std::string text = "Positive";
            return text;
        }
        case axis_range::negative:
        {
            static const std::string text = "Negative";
            return text;
        }
        default:
        {
            static const std::string text = "Unknown";
            return text;
        }
    }
}

inline auto to_string(input_type type) -> const std::string&
{
    switch(type)
    {
        case input_type::axis:
        {
            static const std::string text = "Axis";
            return text;
        }
        case input_type::button:
        {
            static const std::string text = "Button";
            return text;
        }
        case input_type::key:
        {
            static const std::string text = "Key";
            return text;
        }
        default:
        {
            static const std::string text = "Unknown";
            return text;
        }
    }
}

inline auto to_string(const coord& coord) -> std::string
{
    return "(" + std::to_string(coord.x) + ", " + std::to_string(coord.y) + ")";
}

inline auto to_string(const gamepad_axis axis) -> std::string
{
    switch(axis)
    {
        case gamepad_axis::left_x:
            return "Left X";
        case gamepad_axis::left_y:
            return "Left Y";
        case gamepad_axis::right_x:
            return "Right X";
        case gamepad_axis::right_y:
            return "Right Y";
        case gamepad_axis::left_trigger:
            return "Left Trigger";
        case gamepad_axis::right_trigger:
            return "Right Trigger";
        default:
            return "Unknown";
    }
}

inline auto to_string(gamepad_button button) -> std::string
{
    switch(button)
    {
        case gamepad_button::south:
            return "South";
        case gamepad_button::east:
            return "East";
        case gamepad_button::west:
            return "West";
        case gamepad_button::north:
            return "North";
        case gamepad_button::back:
            return "Back";
        case gamepad_button::guide:
            return "Guide";
        case gamepad_button::start:
            return "Start";
        case gamepad_button::left_stick:
            return "Left Stick";
        case gamepad_button::right_stick:
            return "Right Stick";
        case gamepad_button::left_shoulder:
            return "Left Shoulder";
        case gamepad_button::right_shoulder:
            return "Right Shoulder";
        case gamepad_button::dpad_up:
            return "DPad Up";
        case gamepad_button::dpad_down:
            return "DPad Down";
        case gamepad_button::dpad_left:
            return "DPad left";
        case gamepad_button::dpad_right:
            return "DPad Right";
        case gamepad_button::misc1:
            return "Misc 1";
        case gamepad_button::right_paddle1:
            return "Right Paddle 1";
        case gamepad_button::left_paddle1:
            return "Left Paddle 1";
        case gamepad_button::right_paddle2:
            return "Right Paddle 2";
        case gamepad_button::left_paddle2:
            return "Left Paddle 2";
        case gamepad_button::touchpad:
            return "Touchpad";
        case gamepad_button::misc2:
            return "Misc 2";
        case gamepad_button::misc3:
            return "Misc 3";
        case gamepad_button::misc4:
            return "Misc 4";
        case gamepad_button::misc5:
            return "Misc 5";
        case gamepad_button::misc6:
            return "Misc 6";
        default:
            return "Unknown";
    }
}

inline auto get_description(gamepad_button button) -> std::string
{
    switch(button)
    {
        case gamepad_button::south:
            return "Bottom face button (e.g. Xbox A button, PS Cross button)";
        case gamepad_button::east:
            return "Right face button (e.g. Xbox B button, PS Circle button)";
        case gamepad_button::west:
            return "Left face button (e.g. Xbox X button, PS Square button)";
        case gamepad_button::north:
            return "Top face button (e.g. Xbox Y button, PS Triangle button)";
        case gamepad_button::misc1:
            return "Additional button (e.g. Xbox Series X share button, PS5 microphone button, Nintendo Switch Pro capture button, Amazon Luna microphone button, Google Stadia capture button)";
        case gamepad_button::right_paddle1:
            return "Upper or primary paddle, under your right hand (e.g. Xbox Elite paddle P1)";
        case gamepad_button::left_paddle1:
            return "Upper or primary paddle, under your left hand (e.g. Xbox Elite paddle P3)";
        case gamepad_button::right_paddle2:
            return "Lower or secondary paddle, under your right hand (e.g. Xbox Elite paddle P2)";
        case gamepad_button::left_paddle2:
            return "Lower or secondary paddle, under your left hand (e.g. Xbox Elite paddle P4)";
        case gamepad_button::touchpad:
            return "PS4/PS5 touchpad button";
        default:
            return "";
    }
}

inline auto to_string(const key_state state) -> std::string
{
    switch(state)
    {
        case key_state::down:
            return "Down";
        case key_state::up:
            return "Up";
        case key_state::released:
            return "Released";
        case key_state::pressed:
            return "Pressed";
    }
}

inline auto to_string(const point& p) -> std::string
{
    return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
}

inline auto to_string(mouse_button button) -> std::string
{
    switch(button)
    {
        case mouse_button::left_button:
            return "Left";
        case mouse_button::right_button:
            return "Right";
        case mouse_button::middle_button:
            return "Middle";
        case mouse_button::button4:
            return "Button 4";
        case mouse_button::button5:
            return "Button 5";
        case mouse_button::button6:
            return "Button 6";
        case mouse_button::button7:
            return "Button 7";
        case mouse_button::button8:
            return "Button 8";
        case mouse_button::button9:
            return "Button 9";
        case mouse_button::button10:
            return "Button 10";
        case mouse_button::button11:
            return "Button 11";
        case mouse_button::button12:
            return "Button 12";
        case mouse_button::button13:
            return "Button 13";
        case mouse_button::button14:
            return "Button 14";
        case mouse_button::button15:
            return "Button 15";
        case mouse_button::button16:
            return "Button 16";
        default:
            return "Unknown";
    }
}

inline auto to_string(mouse_axis axis) -> const std::string&
{
    switch(axis)
    {
        case mouse_axis::x:
        {
            static const std::string text = "X";
            return text;
        }
        case mouse_axis::y:
        {
            static const std::string text = "Y";
            return text;
        }
        case mouse_axis::scroll:
        {
            static const std::string text = "Scroll";
            return text;
        }
        default:
        {
            static const std::string text = "Unknown";
            return text;
        }
    }
}

} // namespace input
