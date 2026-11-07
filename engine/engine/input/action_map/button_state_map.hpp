#pragma once

#include "button_state.hpp"
#include "input_state_map.hpp"

namespace input
{
/// The states of a mouse's or a gamepad's buttons, by button number.
using button_state_map = input_state_map<unsigned, button_state>;
} // namespace input
