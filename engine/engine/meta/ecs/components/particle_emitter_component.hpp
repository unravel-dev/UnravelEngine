#pragma once
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(particle_emitter_component);
LOAD_EXTERN(particle_emitter_component);
REFLECT_EXTERN(particle_emitter_component);

} // namespace unravel
