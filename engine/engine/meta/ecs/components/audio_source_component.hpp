#pragma once
#include <engine/audio/ecs/components/audio_source_component.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(audio_source_component);
LOAD_EXTERN(audio_source_component);
REFLECT_EXTERN(audio_source_component);

} // namespace unravel
