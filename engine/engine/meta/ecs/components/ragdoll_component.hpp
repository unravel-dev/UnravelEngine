#pragma once

#include <engine/physics/ecs/components/ragdoll_component.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
REFLECT_EXTERN(ragdoll_state);
REFLECT_EXTERN(ragdoll_joint_type);

SAVE_EXTERN(ragdoll_body);
LOAD_EXTERN(ragdoll_body);
REFLECT_EXTERN(ragdoll_body);

SAVE_EXTERN(ragdoll_bone_pair);
LOAD_EXTERN(ragdoll_bone_pair);
REFLECT_EXTERN(ragdoll_bone_pair);

SAVE_EXTERN(ragdoll_drive);
LOAD_EXTERN(ragdoll_drive);
REFLECT_EXTERN(ragdoll_drive);

SAVE_EXTERN(ragdoll_component);
LOAD_EXTERN(ragdoll_component);
REFLECT_EXTERN(ragdoll_component);
} // namespace unravel
