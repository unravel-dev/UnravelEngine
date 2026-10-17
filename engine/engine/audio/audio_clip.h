#pragma once
#include <engine/engine_export.h>

#include <audiopp/sound.h>
#include <audiopp/sound_data.h>

#include <cstdint>
#include <string>
#include <vector>

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

/**
 * @brief The compiled form of an audio clip: the source file's bytes in their own codec.
 *
 * Decoded when the clip loads, so a compressed source stays compressed on disk and in a deployed
 * game; decoded PCM is several times larger and general-purpose compression barely shrinks it.
 */
struct encoded_sound
{
    /// The codec, named by the source file's extension without the dot: "ogg", "mp3", "wav", "flac".
    std::string codec;
    /// The source file's contents.
    std::vector<std::uint8_t> bytes;
    /// Mix the decoded channels down to one.
    bool force_to_mono{};
};

/**
 * @brief Decodes a compiled clip to PCM, applying its import settings.
 * @param encoded The compiled clip.
 * @param result Receives the decoded sound.
 * @param err Receives why decoding failed.
 * @return False when the codec is unknown or the bytes do not decode.
 */
ENGINE_EXPORT auto decode_sound(const encoded_sound& encoded, audio::sound_data& result, std::string& err) -> bool;

} // namespace unravel
