#pragma once

#include "gamepad_axis.hpp"
#include "gamepad_button.hpp"

namespace input
{
struct gamepad_360
{
    //  Axes
    struct axis
    {
        // clang-format off
        static const gamepad_axis left_stick_x      = gamepad_axis::left_x;
        static const gamepad_axis left_stick_y      = gamepad_axis::left_y;
        static const gamepad_axis left_trigger      = gamepad_axis::left_trigger;
        static const gamepad_axis right_stick_x     = gamepad_axis::right_x;
        static const gamepad_axis right_stick_y     = gamepad_axis::right_y;
        static const gamepad_axis right_trigger     = gamepad_axis::right_trigger;
        // clang-format on
    };

    struct button
    {
        //  Buttons
        // clang-format off
        static const gamepad_button a               = gamepad_button::south;
        static const gamepad_button b               = gamepad_button::east;
        static const gamepad_button x               = gamepad_button::west;
        static const gamepad_button y               = gamepad_button::north;
        static const gamepad_button left_bumper     = gamepad_button::left_shoulder;
        static const gamepad_button right_bumper    = gamepad_button::right_shoulder;
        static const gamepad_button back            = gamepad_button::back;
        static const gamepad_button start           = gamepad_button::start;
        static const gamepad_button guide           = gamepad_button::guide;
        static const gamepad_button left_thumb      = gamepad_button::left_stick;
        static const gamepad_button right_thumb     = gamepad_button::right_stick;
        static const gamepad_button dpad_up         = gamepad_button::dpad_up;
        static const gamepad_button dpad_down       = gamepad_button::dpad_down;
        static const gamepad_button dpad_left       = gamepad_button::dpad_left;
        static const gamepad_button dpad_right      = gamepad_button::dpad_right;
        // clang-format on
    };
};
} // namespace input
