#pragma once

#include "input_state_map.hpp"
#include "key.hpp"
#include "key_state.hpp"

namespace input
{
/// The keyboard's key states.
using key_state_map = input_state_map<key_code, key_state>;
} // namespace input
