#pragma once

#include <engine/assets/asset_storage.h>
#include <engine/audio/audio_clip.h>

#include <audiopp/sound_data.h>

#include <cstdint>
#include <string>
#include <vector>

/// Compression of lossless audio sources to Ogg Vorbis at import (libvorbis). Games decode the result with stb_vorbis.
namespace unravel::asset_compiler
{

/// The Vorbis quality (libvorbis' -0.1 to 1.0 scale) of a resolved compression (audio_compression.h): Low, Normal or
/// High Quality.
auto get_vorbis_quality(audio_importer_meta::compression_quality quality) -> float;

/**
 * @brief Encodes 16-bit interleaved PCM to an Ogg Vorbis stream that decodes to exactly as many frames.
 * @param pcm The sound, as audiopp's decoders give it (16 bits per sample).
 * @param quality The Vorbis quality, from get_vorbis_quality.
 * @param encoded Receives the stream.
 * @param error Receives why the sound could not be encoded.
 * @return False when the encoder refuses the sound (no frames, an unsupported rate or channel count).
 */
auto encode_vorbis(const audio::sound_data& pcm, float quality, std::vector<std::uint8_t>& encoded, std::string& error)
    -> bool;

/**
 * @brief Replaces a lossless clip's bytes with its Vorbis encoding.
 * @param decoded The clip decoded with its import settings (mixed to mono when forced), which the stream holds.
 * @param quality The resolved compression: Low, Normal or High Quality.
 * @param clip The compiled clip: its codec becomes ogg and its bytes the stream.
 * @param error Receives why it could not be compressed.
 * @return False when encoding fails or the stream does not decode to the same frames, leaving @p clip as it was.
 */
auto compress_clip_to_vorbis(const audio::sound_data& decoded,
                             audio_importer_meta::compression_quality quality,
                             encoded_sound& clip,
                             std::string& error) -> bool;

} // namespace unravel::asset_compiler
