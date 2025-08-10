#pragma once
#include <engine/rendering/ecs/components/model_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(model_component);
LOAD_EXTERN(model_component);
REFLECT_EXTERN(model_component);

SAVE_EXTERN(bone_component);
LOAD_EXTERN(bone_component);
REFLECT_EXTERN(bone_component);

SAVE_EXTERN(submesh_component);
LOAD_EXTERN(submesh_component);
REFLECT_EXTERN(submesh_component);

} // namespace unravel
