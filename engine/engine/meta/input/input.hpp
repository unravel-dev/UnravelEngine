#pragma once

#include <engine/input/input.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace input
{


SAVE_EXTERN(mouse_action_map);
LOAD_EXTERN(mouse_action_map);

SAVE_EXTERN(keyboard_action_map);
LOAD_EXTERN(keyboard_action_map);

SAVE_EXTERN(gamepad_action_map);
LOAD_EXTERN(gamepad_action_map);

SAVE_EXTERN(action_map);
LOAD_EXTERN(action_map);
REFLECT_EXTERN(action_map);



// void save_to_file(const std::string& absolute_path, const input::action_map& obj);
// void save_to_file_bin(const std::string& absolute_path, const input::action_map& obj);
// auto load_from_file(const std::string& absolute_path, input::action_map& obj) -> bool;
// auto load_from_file_bin(const std::string& absolute_path, input::action_map& obj) -> bool;

} // namespace unravel
