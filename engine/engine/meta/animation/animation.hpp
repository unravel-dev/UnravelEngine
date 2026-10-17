#pragma once
#include <engine/engine_export.h>

#include <engine/animation/animation.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>
#include <serialization/types/chrono.hpp>

namespace unravel
{
SAVE_EXTERN(animation_clip);
LOAD_EXTERN(animation_clip);
REFLECT_EXTERN(animation_clip);

SAVE_EXTERN(animation_channel);
LOAD_EXTERN(animation_channel);
REFLECT_EXTERN(animation_channel);

SERIALIZE_EXTERN(root_motion_params);
REFLECT_EXTERN(root_motion_params);

SERIALIZE_EXTERN(animation_event);
REFLECT_EXTERN(animation_event);

SAVE_EXTERN(animation_curve);
LOAD_EXTERN(animation_curve);
REFLECT_EXTERN(animation_curve);

REFLECT_EXTERN(humanoid_bone);

SERIALIZE_EXTERN(humanoid_bone_override);
REFLECT_EXTERN(humanoid_bone_override);

SERIALIZE_EXTERN(humanoid_pose_offset);
REFLECT_EXTERN(humanoid_pose_offset);

/**
 * @brief Brings authored events into a valid state: positions and weights clamped to 0..1, sorted
 * by position. Run on everything an importer meta feeds a clip.
 */
void sanitize_animation_events(std::vector<animation_event>& events);

/**
 * @brief Drops unnamed curves; a curve is looked up by name, so an unnamed one is unreachable.
 */
void sanitize_animation_curves(std::vector<animation_curve>& curves);


template<typename Archive, typename T>
void SERIALIZE_FUNCTION_NAME(Archive& ar, animation_channel::key<T>& obj)
{
    try_serialize(ar, ser20::make_nvp("time", obj.time));
    try_serialize(ar, ser20::make_nvp("value", obj.value));
}

void save_to_file(const std::string& absolute_path, const animation_clip& obj);
void save_to_file_bin(const std::string& absolute_path, const animation_clip& obj);
void load_from_file(const std::string& absolute_path, animation_clip& obj);
void load_from_file_bin(const std::string& absolute_path, animation_clip& obj);

} // namespace unravel
