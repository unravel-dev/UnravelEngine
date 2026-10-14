#pragma once
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
namespace ps_soa
{
// In the type's own namespace: ser20 finds them by argument-dependent lookup.
SAVE_EXTERN(emitter_burst);
LOAD_EXTERN(emitter_burst);
} // namespace ps_soa
REFLECT_EXTERN(ps_soa::emitter_burst);

SAVE_EXTERN(particle_sub_emitter);
LOAD_EXTERN(particle_sub_emitter);
REFLECT_EXTERN(particle_sub_emitter);

SAVE_EXTERN(particle_emitter_component);
LOAD_EXTERN(particle_emitter_component);
REFLECT_EXTERN(particle_emitter_component);
} // namespace unravel
