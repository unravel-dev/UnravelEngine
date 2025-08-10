#pragma once

namespace input
{
enum class gamepad_button
{
    south,           /**< Bottom face button (e.g. Xbox A button, PS Cross button) */
    east,            /**< Right face button (e.g. Xbox B button, PS Circle button) */
    west,            /**< Left face button (e.g. Xbox X button, PS Square button) */
    north,           /**< Top face button (e.g. Xbox Y button, PS Triangle button) */
    back,
    guide,
    start,
    left_stick,
    right_stick,
    left_shoulder,
    right_shoulder,
    dpad_up,
    dpad_down,
    dpad_left,
    dpad_right,
    misc1,           /**< Additional button (e.g. Xbox Series X share button, PS5 microphone button, Nintendo Switch Pro capture button, Amazon Luna microphone button, Google Stadia capture button) */
    right_paddle1,   /**< Upper or primary paddle, under your right hand (e.g. Xbox Elite paddle P1) */
    left_paddle1,    /**< Upper or primary paddle, under your left hand (e.g. Xbox Elite paddle P3) */
    right_paddle2,   /**< Lower or secondary paddle, under your right hand (e.g. Xbox Elite paddle P2) */
    left_paddle2,    /**< Lower or secondary paddle, under your left hand (e.g. Xbox Elite paddle P4) */
    touchpad,        /**< PS4/PS5 touchpad button */
    misc2,           /**< Additional button */
    misc3,           /**< Additional button */
    misc4,           /**< Additional button */
    misc5,           /**< Additional button */
    misc6,           /**< Additional button */

    count,
};
}
