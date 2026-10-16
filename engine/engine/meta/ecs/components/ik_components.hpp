#pragma once

#include <engine/animation/ecs/components/ik_components.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
REFLECT_EXTERN(chain_ik_solver);

SAVE_EXTERN(spring_bone_collider);
LOAD_EXTERN(spring_bone_collider);
REFLECT_EXTERN(spring_bone_collider);

SAVE_EXTERN(two_bone_ik_component);
LOAD_EXTERN(two_bone_ik_component);
REFLECT_EXTERN(two_bone_ik_component);

SAVE_EXTERN(chain_ik_component);
LOAD_EXTERN(chain_ik_component);
REFLECT_EXTERN(chain_ik_component);

SAVE_EXTERN(look_at_ik_component);
LOAD_EXTERN(look_at_ik_component);
REFLECT_EXTERN(look_at_ik_component);

SAVE_EXTERN(foot_ik_component);
LOAD_EXTERN(foot_ik_component);
REFLECT_EXTERN(foot_ik_component);

SAVE_EXTERN(spring_bone_component);
LOAD_EXTERN(spring_bone_component);
REFLECT_EXTERN(spring_bone_component);

} // namespace unravel
