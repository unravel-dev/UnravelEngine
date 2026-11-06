#include "audio_compression.h"

#include <mutex>

namespace unravel::asset_compiler
{
namespace
{
/// The compression of a clip on Project Default when the project's default is Project Default too.
constexpr auto FALLBACK_COMPRESSION = audio_importer_meta::compression_quality::normal_quality;

std::mutex editor_defaults_mutex;
settings::asset_settings::audio_importer_settings editor_defaults;

auto is_lossless_codec(const std::string& codec) -> bool
{
    return codec == "wav" || codec == "wave" || codec == "flac";
}

auto get_compression_name(audio_importer_meta::compression_quality compression) -> const char*
{
    switch(compression)
    {
        case audio_importer_meta::compression_quality::none:
            return "none";
        case audio_importer_meta::compression_quality::low_quality:
            return "vorbis_low";
        case audio_importer_meta::compression_quality::high_quality:
            return "vorbis_high";
        case audio_importer_meta::compression_quality::normal_quality:
        case audio_importer_meta::compression_quality::project_default:
        default:
            return "vorbis_normal";
    }
}
} // namespace

auto resolve_audio_compression(const audio_importer_meta& importer,
                               const settings::asset_settings::audio_importer_settings& defaults)
    -> audio_importer_meta::compression_quality
{
    if(importer.compression != audio_importer_meta::compression_quality::project_default)
    {
        return importer.compression;
    }
    if(defaults.default_compression != audio_importer_meta::compression_quality::project_default)
    {
        return defaults.default_compression;
    }
    return FALLBACK_COMPRESSION;
}

auto is_compressed_to_vorbis(audio_importer_meta::compression_quality compression, const std::string& codec) -> bool
{
    return compression != audio_importer_meta::compression_quality::none && is_lossless_codec(codec);
}

auto get_audio_compile_parameters(audio_importer_meta::compression_quality compression) -> std::string
{
    return std::string("compression_") + get_compression_name(compression);
}

void set_editor_audio_defaults(const settings::asset_settings::audio_importer_settings& defaults)
{
    const std::lock_guard<std::mutex> lock(editor_defaults_mutex);
    editor_defaults = defaults;
}

auto get_editor_audio_defaults() -> settings::asset_settings::audio_importer_settings
{
    const std::lock_guard<std::mutex> lock(editor_defaults_mutex);
    return editor_defaults;
}

auto get_editor_audio_compile_parameters(const fs::path& key) -> std::string
{
    return get_audio_compile_parameters(resolve_audio_compression(read_audio_importer(key), get_editor_audio_defaults()));
}

} // namespace unravel::asset_compiler
