#pragma once
#include <engine/engine_export.h>

#include <audiopp/sound.h>
#include <audiopp/sound_data.h>

namespace unravel
{

/**
 * @brief Struct representing an audio clip.
 *
 * This struct inherits from `audio::sound` and provides additional functionality for audio clips.
 */
struct audio_clip : public audio::sound
{
    using base_type = audio::sound;
    using base_type::base_type;
};

} // namespace unravel
