#pragma once

#include <engine/assets/asset_storage.h>
#include <engine/settings/settings.h>
#include <filesystem/filesystem.h>

#include <string>

/// The compression an audio clip compiles with: its import settings, or the project's default where they are
/// Project Default. A lossless source (WAV, FLAC) compressed at a quality is stored as Vorbis (vorbis_encoder.h); any
/// other source keeps its own bytes.
namespace unravel::asset_compiler
{

/// The compression a clip compiles with on `platform`, never Project Default: the clip's own for the platform
/// (audio_importer_meta::get_compression), else `defaults` (the project's, or a profile's), else Normal Quality.
auto resolve_audio_compression(const audio_importer_meta& importer,
                               target_platform platform,
                               const settings::asset_settings::audio_importer_settings& defaults)
    -> audio_importer_meta::compression_quality;

/// The codec of an audio source, by its file name: its extension in lower case, without the dot ("wav").
auto get_audio_codec(const fs::path& source) -> std::string;

/// What a clip of `codec` compiled with `compression` (resolved) is stored as: `compression` for a lossless source,
/// None for any other, whose bytes are compressed already and ship as they are.
auto get_stored_compression(audio_importer_meta::compression_quality compression, const std::string& codec)
    -> audio_importer_meta::compression_quality;

/// Whether a clip of `codec` compiled with `compression` (resolved) is stored as Vorbis: a lossless source at a
/// quality.
auto is_compressed_to_vorbis(audio_importer_meta::compression_quality compression, const std::string& codec) -> bool;

/// What a clip's manifest records of how it compiled: what it is stored as, and for Vorbis the encoder's version, so
/// it compiles again when either changes and not when a setting changes that leaves its bytes as they are.
auto get_audio_compile_parameters(audio_importer_meta::compression_quality compression, const std::string& codec)
    -> std::string;

/// The project's audio defaults the editor compiles its clips with; any thread.
void set_editor_audio_defaults(const settings::asset_settings::audio_importer_settings& defaults);
auto get_editor_audio_defaults() -> settings::asset_settings::audio_importer_settings;

/// The audio defaults the editor compiles the clip at `key` with: the project's for its clips, a new project's for
/// the engine's and the editor's (import_defaults.h).
auto get_editor_audio_defaults_for(const fs::path& key) -> settings::asset_settings::audio_importer_settings;

/// The compression the editor compiles the clip at `key` with: the Desktop tab's, with get_editor_audio_defaults_for.
auto resolve_editor_audio_compression(const fs::path& key, const audio_importer_meta& importer)
    -> audio_importer_meta::compression_quality;

/// The import settings of the clip at `key`, read without writing: a clip without settings gets a new clip's.
auto read_audio_importer(const fs::path& key) -> audio_importer_meta;

/// The compile parameters the clip at `key` (its source or its .meta) compiles with in the editor now.
auto get_editor_audio_compile_parameters(const fs::path& key) -> std::string;

/**
 * @brief Compiles the clip at `key` with `compression` (resolved) into `output`, and its manifest: the source's bytes,
 * or for a lossless source at a quality its Vorbis encoding. A source the encoder refuses keeps its bytes, with a
 * warning.
 * @return False, with the reason logged, when the source does not decode or the output cannot be written.
 */
auto compile_audio_clip(const fs::path& key,
                        const fs::path& output,
                        const audio_importer_meta& importer,
                        audio_importer_meta::compression_quality compression) -> bool;

/// Whether `output` is the clip at `key` compiled with `compression` (resolved) from its current source.
auto is_audio_compiled_with(const fs::path& key, const fs::path& output, audio_importer_meta::compression_quality compression)
    -> bool;

} // namespace unravel::asset_compiler
