#pragma once

#include <engine/physics/ecs/components/physics_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(physics_box_shape);
LOAD_EXTERN(physics_box_shape);
REFLECT_EXTERN(physics_box_shape);

SAVE_EXTERN(physics_sphere_shape);
LOAD_EXTERN(physics_sphere_shape);
REFLECT_EXTERN(physics_sphere_shape);

SAVE_EXTERN(physics_capsule_shape);
LOAD_EXTERN(physics_capsule_shape);
REFLECT_EXTERN(physics_capsule_shape);

SAVE_EXTERN(physics_cylinder_shape);
LOAD_EXTERN(physics_cylinder_shape);
REFLECT_EXTERN(physics_cylinder_shape);

SAVE_EXTERN(physics_compound_shape);
LOAD_EXTERN(physics_compound_shape);
REFLECT_EXTERN(physics_compound_shape);

SAVE_EXTERN(physics_component);
LOAD_EXTERN(physics_component);
REFLECT_EXTERN(physics_component);
} // namespace unravel
