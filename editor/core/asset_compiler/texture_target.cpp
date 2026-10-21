#include "texture_target.h"

#include <bimg/bimg.h>
#include <logging/logging.h>

#include <algorithm>
#include <vector>

#include <mutex>

namespace unravel::asset_compiler
{
namespace
{
/// Raised when a change to the texture compiler (texturec, the format choice) changes what a texture
/// compiles to with the same parameters, so that every compiled texture is compiled again.
constexpr int TEXTURE_COMPILER_VERSION = 2;
/// The protocol of the project's own files.
constexpr const char* PROJECT_PROTOCOL = "app";
/// What a texture compiles with when neither it nor the target sets a value.
constexpr auto FALLBACK_MAX_SIZE = texture_importer_meta::texture_size::size_2048;
constexpr auto FALLBACK_COMPRESSION = texture_importer_meta::compression_quality::normal_quality;

std::mutex editor_target_mutex;
texture_target editor_target;

struct texel_size
{
    std::uint32_t width{};
    std::uint32_t height{};

    friend auto operator==(const texel_size& lhs, const texel_size& rhs) -> bool = default;
};

/// The size texturec compiles a texture to with blocks `block` texels wide: it rounds the size up to whole
/// blocks, before and after fitting it into `max_side` (0 for none), and resamples the texture to it.
auto get_texturec_output_size(texel_size source, std::uint32_t max_side, std::uint32_t block) -> texel_size
{
    const auto round_up = [block](std::uint32_t side)
    {
        return std::max(block, (side + block - 1) / block * block);
    };
    texel_size size{round_up(source.width), round_up(source.height)};
    const std::uint32_t largest = std::max(size.width, size.height);
    if(max_side > 0 && largest > max_side)
    {
        size = {round_up(size.width * max_side / largest), round_up(size.height * max_side / largest)};
    }
    return size;
}

/// The ASTC formats a quality may take, the most compressed first. 4x4, the block BC uses, closes each list.
auto get_astc_formats(texture_importer_meta::compression_quality quality) -> std::vector<bgfx::TextureFormat::Enum>
{
    switch(quality)
    {
        case texture_importer_meta::compression_quality::low_quality:
            return {bgfx::TextureFormat::ASTC8x8,
                    bgfx::TextureFormat::ASTC6x6,
                    bgfx::TextureFormat::ASTC5x5,
                    bgfx::TextureFormat::ASTC4x4};
        case texture_importer_meta::compression_quality::high_quality:
            return {bgfx::TextureFormat::ASTC4x4};
        default:
            return {bgfx::TextureFormat::ASTC6x6, bgfx::TextureFormat::ASTC5x5, bgfx::TextureFormat::ASTC4x4};
    }
}

auto get_family_name(texture_compression_family family) -> const char*
{
    switch(family)
    {
        case texture_compression_family::astc:
            return "astc";
        case texture_compression_family::etc2:
            return "etc2";
        case texture_compression_family::bc:
        default:
            return "bc";
    }
}
} // namespace

auto resolve_texture_parameters(const texture_importer_meta& importer, const texture_target& target) -> texture_compile_parameters
{
    const auto& quality = importer.get_quality(target.platform);
    texture_compile_parameters parameters;
    parameters.family = target.family;
    parameters.max_size = quality.max_size;
    parameters.compression = quality.compression;
    if(parameters.max_size == texture_importer_meta::texture_size::project_default)
    {
        parameters.max_size = target.defaults.default_max_size;
    }
    if(parameters.max_size == texture_importer_meta::texture_size::project_default)
    {
        parameters.max_size = FALLBACK_MAX_SIZE;
    }
    if(parameters.compression == texture_importer_meta::compression_quality::project_default)
    {
        parameters.compression = target.defaults.default_compression;
    }
    if(parameters.compression == texture_importer_meta::compression_quality::project_default)
    {
        parameters.compression = FALLBACK_COMPRESSION;
    }
    if(parameters.compression == texture_importer_meta::compression_quality::none)
    {
        parameters.family = texture_compression_family::bc;
    }
    return parameters;
}

auto select_astc_ldr_format(std::uint32_t width,
                            std::uint32_t height,
                            std::uint32_t max_side,
                            texture_importer_meta::compression_quality quality) -> bgfx::TextureFormat::Enum
{
    const texel_size source{width, height};
    const auto own_size = get_texturec_output_size(source, max_side, 1);
    const auto formats = get_astc_formats(quality);
    const auto keeps_size = [&](bgfx::TextureFormat::Enum format)
    {
        const auto block = bimg::getBlockInfo(static_cast<bimg::TextureFormat::Enum>(format)).blockWidth;
        return get_texturec_output_size(source, max_side, block) == own_size;
    };
    const auto found = std::find_if(formats.begin(), formats.end(), keeps_size);
    return found != formats.end() ? *found : bgfx::TextureFormat::ASTC4x4;
}

auto to_string(const texture_compile_parameters& parameters) -> std::string
{
    return fmt::format("v{}/{}/compression_{}/size_{}",
                       TEXTURE_COMPILER_VERSION,
                       get_family_name(parameters.family),
                       static_cast<int>(parameters.compression),
                       static_cast<int>(parameters.max_size));
}

void set_editor_texture_target(const texture_target& target)
{
    const std::lock_guard<std::mutex> lock(editor_target_mutex);
    editor_target = target;
}

auto get_editor_texture_target() -> texture_target
{
    const std::lock_guard<std::mutex> lock(editor_target_mutex);
    return editor_target;
}

auto get_editor_texture_target_for(const fs::path& key) -> texture_target
{
    const auto protocol = fs::extract_protocol(fs::convert_to_protocol(key)).generic_string();
    return protocol == PROJECT_PROTOCOL ? get_editor_texture_target() : texture_target{};
}

} // namespace unravel::asset_compiler
