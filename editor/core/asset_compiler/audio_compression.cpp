#include "audio_compression.h"
#include "import_defaults.h"
#include "vorbis_encoder.h"

#include <string_utils/utils.h>

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

/// The name of a stored compression (get_stored_compression) in the compile parameters.
auto get_stored_compression_name(audio_importer_meta::compression_quality stored) -> const char*
{
    switch(stored)
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
                               target_platform platform,
                               const settings::asset_settings::audio_importer_settings& defaults)
    -> audio_importer_meta::compression_quality
{
    const auto own = importer.get_compression(platform);
    if(own != audio_importer_meta::compression_quality::project_default)
    {
        return own;
    }
    if(defaults.default_compression != audio_importer_meta::compression_quality::project_default)
    {
        return defaults.default_compression;
    }
    return FALLBACK_COMPRESSION;
}

auto get_audio_codec(const fs::path& source) -> std::string
{
    auto codec = string_utils::to_lower(source.extension().generic_string());
    if(!codec.empty() && codec.front() == '.')
    {
        codec.erase(0, 1);
    }
    return codec;
}

auto get_stored_compression(audio_importer_meta::compression_quality compression, const std::string& codec)
    -> audio_importer_meta::compression_quality
{
    return is_lossless_codec(codec) ? compression : audio_importer_meta::compression_quality::none;
}

auto is_compressed_to_vorbis(audio_importer_meta::compression_quality compression, const std::string& codec) -> bool
{
    return get_stored_compression(compression, codec) != audio_importer_meta::compression_quality::none;
}

auto get_audio_compile_parameters(audio_importer_meta::compression_quality compression, const std::string& codec)
    -> std::string
{
    const auto stored = get_stored_compression(compression, codec);
    std::string parameters = std::string("compression_") + get_stored_compression_name(stored);
    if(stored != audio_importer_meta::compression_quality::none)
    {
        parameters += " encoder_" + get_vorbis_encoder_version();
    }
    return parameters;
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

auto get_editor_audio_defaults_for(const fs::path& key) -> settings::asset_settings::audio_importer_settings
{
    return follows_project_defaults(key) ? get_editor_audio_defaults() : settings::asset_settings::audio_importer_settings{};
}

auto resolve_editor_audio_compression(const fs::path& key, const audio_importer_meta& importer)
    -> audio_importer_meta::compression_quality
{
    return resolve_audio_compression(importer, target_platform::desktop, get_editor_audio_defaults_for(key));
}

} // namespace unravel::asset_compiler
