#pragma once

#include <engine/audio/audio_clip.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace unravel
{
SAVE_EXTERN(audio_clip);
LOAD_EXTERN(audio_clip);
REFLECT_EXTERN(audio_clip);
SAVE_EXTERN(encoded_sound);
LOAD_EXTERN(encoded_sound);

/// Writes a compiled audio clip. False when the file could not be written.
auto save_to_file_bin(const std::string& absolute_path, const encoded_sound& obj) -> bool;
/// Reads a compiled audio clip. False when the file is missing or not a compiled clip.
auto load_from_file_bin(const std::string& absolute_path, encoded_sound& obj) -> bool;

} // namespace unravel
