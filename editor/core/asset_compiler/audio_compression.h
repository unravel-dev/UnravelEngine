#pragma once

#include <engine/assets/asset_storage.h>
#include <engine/settings/settings.h>
#include <filesystem/filesystem.h>

#include <string>

/// The compression an audio clip compiles with: its import settings, or the project's default where they are
/// Project Default. A lossless source (WAV, FLAC) compressed at a quality is stored as Vorbis (vorbis_encoder.h).
namespace unravel::asset_compiler
{

/// The compression a clip compiles with, never Project Default: the clip's own, else the project's default, else
/// Normal Quality.
auto resolve_audio_compression(const audio_importer_meta& importer,
                               const settings::asset_settings::audio_importer_settings& defaults)
    -> audio_importer_meta::compression_quality;

/// Whether a clip of `codec` compiled with `compression` (resolved) is stored as Vorbis: a lossless source at a
/// quality.
auto is_compressed_to_vorbis(audio_importer_meta::compression_quality compression, const std::string& codec) -> bool;

/// What a clip's manifest records of its compression, so it compiles again when the project's default changes.
auto get_audio_compile_parameters(audio_importer_meta::compression_quality compression) -> std::string;

/// The project's audio defaults the editor compiles with; any thread.
void set_editor_audio_defaults(const settings::asset_settings::audio_importer_settings& defaults);
auto get_editor_audio_defaults() -> settings::asset_settings::audio_importer_settings;

/// The import settings of the clip at `key`, read without writing: a clip without settings gets a new clip's.
auto read_audio_importer(const fs::path& key) -> audio_importer_meta;

/// The compile parameters the clip at `key` compiles with in the editor now.
auto get_editor_audio_compile_parameters(const fs::path& key) -> std::string;

} // namespace unravel::asset_compiler
