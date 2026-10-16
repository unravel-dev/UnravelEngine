#pragma once
#include <engine/engine_export.h>

#include <engine/animation/animation_controller.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
REFLECT_EXTERN(animation_layer_blend_mode);
REFLECT_EXTERN(animation_parameter_type);
REFLECT_EXTERN(animation_condition_mode);
REFLECT_EXTERN(animation_motion_type);

SERIALIZE_EXTERN(animation_parameter_def);
REFLECT_EXTERN(animation_parameter_def);

SERIALIZE_EXTERN(animation_condition_def);
REFLECT_EXTERN(animation_condition_def);

SAVE_EXTERN(animation_blend_point_def);
LOAD_EXTERN(animation_blend_point_def);
REFLECT_EXTERN(animation_blend_point_def);

SAVE_EXTERN(animation_state_def);
LOAD_EXTERN(animation_state_def);
REFLECT_EXTERN(animation_state_def);

SERIALIZE_EXTERN(animation_transition_def);
REFLECT_EXTERN(animation_transition_def);

SERIALIZE_EXTERN(animation_mask_entry);
REFLECT_EXTERN(animation_mask_entry);

SAVE_EXTERN(animation_layer_def);
LOAD_EXTERN(animation_layer_def);
REFLECT_EXTERN(animation_layer_def);

SAVE_EXTERN(animation_controller);
LOAD_EXTERN(animation_controller);
REFLECT_EXTERN(animation_controller);

void save_to_file(const std::string& absolute_path, const animation_controller::sptr& obj);
void save_to_file_bin(const std::string& absolute_path, const animation_controller::sptr& obj);
void load_from_file(const std::string& absolute_path, animation_controller::sptr& obj);
void load_from_file_bin(const std::string& absolute_path, animation_controller::sptr& obj);

/**
 * @brief The controller as the associative (JSON) text it is saved as, and back. Tools read and
 * write whole controllers through these; load returns false when the text does not parse.
 */
auto save_to_string(const animation_controller& obj) -> std::string;
auto load_from_string(const std::string& text, animation_controller& obj) -> bool;

} // namespace unravel
