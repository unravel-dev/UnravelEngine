#pragma once

#include <engine/physics/ecs/components/joint_component.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
REFLECT_EXTERN(joint_type);

SAVE_EXTERN(joint_limits);
LOAD_EXTERN(joint_limits);
REFLECT_EXTERN(joint_limits);

SAVE_EXTERN(joint_spring);
LOAD_EXTERN(joint_spring);
REFLECT_EXTERN(joint_spring);

SAVE_EXTERN(joint_motor);
LOAD_EXTERN(joint_motor);
REFLECT_EXTERN(joint_motor);

SAVE_EXTERN(joint_steering);
LOAD_EXTERN(joint_steering);
REFLECT_EXTERN(joint_steering);

REFLECT_EXTERN(joint_state);

SAVE_EXTERN(joint_component);
LOAD_EXTERN(joint_component);
REFLECT_EXTERN(joint_component);
} // namespace unravel
