#pragma once

#include <engine/rendering/ecs/components/post_process_material_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(post_process_material_entry);
LOAD_EXTERN(post_process_material_entry);
REFLECT_EXTERN(post_process_material_entry);
SAVE_EXTERN(post_process_material_component);
LOAD_EXTERN(post_process_material_component);
REFLECT_EXTERN(post_process_material_component);
} // namespace unravel
