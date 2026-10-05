#include "mesh_import_paths.h"

#include "../../asset_extensions.h"

#include <logging/logging.h>
#include <string_utils/utils.h>

#include <assimp/scene.h>

#include <algorithm>
#include <string_view>

namespace unravel
{
namespace importer
{
namespace mesh_import
{

/**
 * @brief Normalize file paths from Assimp (often Windows-style backslashes on every host).
 * Uses forward slashes so relative paths resolve consistently across platforms.
 *
 * fs::path::generic_string() only changes how an already-parsed path is printed; on POSIX
 * backslash is not a separator, so "Textures\\file.dds" stays one filename unless we fix
 * the string before constructing fs::path. Stored relative paths use generic_string() (/).
 */
auto normalize_assimp_path(const std::string& path) -> fs::path
{
    if(path.empty())
    {
        return {};
    }

    auto normalized_path = string_utils::replace(path, "\\", "/");
    return fs::path(normalized_path).lexically_normal();
}

auto normalize_assimp_path(const fs::path& path) -> fs::path
{
    if(path.empty())
    {
        return {};
    }
    return normalize_assimp_path(path.generic_string());
}

auto normalize_assimp_path(const char* path) -> fs::path
{
    if(path == nullptr || path[0] == '\0')
    {
        return {};
    }
    return normalize_assimp_path(std::string(path));
}

/**
 * @brief When a material references a texture path that does not exist on disk, try the
 * same basename with other image extensions (e.g. material says .png but only .dds exists).
 */
auto resolve_external_texture_path(const fs::path& base_dir, fs::path relative_path) -> fs::path
{
    relative_path = normalize_assimp_path(relative_path);
    fs::error_code ec;
    if(fs::exists(base_dir / relative_path, ec))
    {
        return relative_path;
    }

    const auto& extensions = ex::get_suported_formats<gfx::texture>();
    const auto parent = relative_path.parent_path();
    const auto stem = relative_path.stem().string();
    const auto requested_ext = string_utils::to_lower(relative_path.extension().string());

    for(const auto& ext : extensions)
    {
        if(ext == requested_ext)
        {
            continue;
        }
        fs::path alternate = parent / (stem + ext);
        if(fs::exists(base_dir / alternate, ec))
        {
            APPLOG_WARNING("Mesh Importer: Texture '{}' not found, using '{}' instead",
                         relative_path.generic_string(),
                         alternate.generic_string());
            return alternate;
        }
    }

    return relative_path;
}

auto get_compressed_texture_extension(const aiTexture* texture) -> std::string
{
    if(texture->mHeight != 0 || texture->achFormatHint[0] == '\0')
    {
        return {};
    }
    return "." + string_utils::to_lower(texture->achFormatHint);
}

auto get_texture_extension(const aiTexture* texture, bool is_converted) -> std::string
{
    const std::string extension = get_compressed_texture_extension(texture);
    const auto& formats = ex::get_suported_formats<gfx::texture>();
    if(!is_converted && !extension.empty() && std::find(formats.begin(), formats.end(), extension) != formats.end())
    {
        return extension;
    }
    return ".png";
}

auto get_embedded_texture_name(const aiTexture* texture,
                               size_t index,
                               const fs::path& filename,
                               const std::string& semantic,
                               bool is_converted) -> std::string
{
    return fmt::format("[{}] {} {}{}", index, semantic, filename.string(), get_texture_extension(texture, is_converted));
}

auto normalize_material_texture_path(const fs::path& path) -> std::string
{
    return string_utils::to_lower(normalize_assimp_path(path).generic_string());
}

auto material_texture_paths_equal(const fs::path& left, const fs::path& right) -> bool
{
    if(left.empty() || right.empty())
    {
        return false;
    }
    return normalize_material_texture_path(left) == normalize_material_texture_path(right);
}

auto make_converted_texture_name(const std::string& original_name, const std::string& semantic) -> std::string
{
    fs::path p(original_name);
    const std::string suffix = semantic.empty() ? "converted" : semantic;
    return (p.parent_path() / (p.stem().string() + "_" + suffix + ".png")).generic_string();
}

auto build_converted_texture_name(const fs::path& filename,
                                  const aiScene* scene,
                                  int embedded_idx,
                                  const std::string& source_relative,
                                  const std::string& target_semantic) -> std::string
{
    if(embedded_idx >= 0 && embedded_idx < static_cast<int>(scene->mNumTextures))
    {
        return fmt::format("[{}] {} {}.png", embedded_idx, target_semantic, filename.string());
    }
    fs::path src(source_relative);
    return (src.parent_path() / (src.stem().string() + "_" + target_semantic + ".png")).generic_string();
}

auto resolve_texture_on_disk(const fs::path& output_dir, fs::path relative) -> std::optional<fs::path>
{
    if(relative.empty())
    {
        return std::nullopt;
    }

    relative = resolve_external_texture_path(output_dir, relative);

    fs::error_code err;
    fs::path absolute = relative.is_absolute() ? relative : (output_dir / relative);
    absolute = fs::weakly_canonical(absolute, err);
    if(err || !fs::exists(absolute, err))
    {
        return std::nullopt;
    }

    return absolute;
}

auto texture_file_exists(const fs::path& output_dir, const std::string& relative) -> bool
{
    return resolve_texture_on_disk(output_dir, normalize_assimp_path(relative)).has_value();
}

auto try_make_texture_asset_key(const fs::path& output_dir, const std::string& relative) -> std::optional<std::string>
{
    const auto absolute = resolve_texture_on_disk(output_dir, normalize_assimp_path(relative));
    if(!absolute)
    {
        return std::nullopt;
    }

    const fs::path key = fs::convert_to_protocol(*absolute);
    if(!fs::has_known_protocol(key))
    {
        return std::nullopt;
    }

    return key.generic_string();
}

auto replace_invalid_file_name_characters(std::string name) -> std::string
{
    constexpr std::string_view invalid_characters = "<>:\"/\\|?*";
    constexpr unsigned char first_printable_character = 0x20;
    std::replace_if(
        name.begin(),
        name.end(),
        [&](char c)
        {
            return static_cast<unsigned char>(c) < first_printable_character
                   || invalid_characters.find(c) != std::string_view::npos;
        },
        '_');
    return name;
}

} // namespace mesh_import
} // namespace importer
} // namespace unravel
