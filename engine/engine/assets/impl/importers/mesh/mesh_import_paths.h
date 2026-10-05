#pragma once

#include <filesystem/filesystem.h>

#include <cstddef>
#include <optional>
#include <string>

struct aiTexture;
struct aiScene;

namespace unravel
{
namespace importer
{
namespace mesh_import
{

/**
 * @brief Normalize a texture path reported by Assimp: backslashes become '/', then lexically normal.
 */
auto normalize_assimp_path(const std::string& path) -> fs::path;
auto normalize_assimp_path(const fs::path& path) -> fs::path;
auto normalize_assimp_path(const char* path) -> fs::path;

/**
 * @brief Path of @p relative_path under @p base_dir if it exists, else the same stem with another
 * supported texture extension that does (authored assets that never remapped .png -> .dds), else
 * @p relative_path unchanged.
 */
auto resolve_external_texture_path(const fs::path& base_dir, fs::path relative_path) -> fs::path;

/// Lower-case generic form, for comparing material texture paths.
auto normalize_material_texture_path(const fs::path& path) -> std::string;
auto material_texture_paths_equal(const fs::path& left, const fs::path& right) -> bool;

/// ".<format hint>" of a compressed embedded texture (".jpg", ".png" ...); empty for raw texel data.
auto get_compressed_texture_extension(const aiTexture* texture) -> std::string;

/**
 * @brief Extension an extracted embedded texture is written with. A compressed texture in a format the
 * texture compiler reads keeps its own (it is extracted byte for byte); converted texels, raw texel data
 * and formats the compiler cannot read are written as PNG.
 */
auto get_texture_extension(const aiTexture* texture, bool is_converted) -> std::string;

/// "[index] semantic filename.ext" - the file an embedded texture is extracted to.
auto get_embedded_texture_name(const aiTexture* texture,
                               size_t index,
                               const fs::path& filename,
                               const std::string& semantic,
                               bool is_converted) -> std::string;

/// "<dir>/<stem>_<semantic>.png" - the file a converted external texture is written to.
auto make_converted_texture_name(const std::string& original_name, const std::string& semantic) -> std::string;

/// Output name of a spec-gloss pair bake for an embedded or an external source texture.
auto build_converted_texture_name(const fs::path& filename,
                                  const aiScene* scene,
                                  int embedded_idx,
                                  const std::string& source_relative,
                                  const std::string& target_semantic) -> std::string;

/// Absolute path of an existing texture relative to @p output_dir (extension fallback applied).
auto resolve_texture_on_disk(const fs::path& output_dir, fs::path relative) -> std::optional<fs::path>;
auto texture_file_exists(const fs::path& output_dir, const std::string& relative) -> bool;

/// Protocol asset key of an existing texture, or nullopt when it is missing or outside every protocol.
auto try_make_texture_asset_key(const fs::path& output_dir, const std::string& relative) -> std::optional<std::string>;

/**
 * @brief Replace the characters a file name cannot hold on Windows (<>:"/\|?* and control
 * characters) with '_'. Imported material and clip names become file names: Blender clips are
 * "Armature|Action", Maya material names carry "namespace:" prefixes.
 */
auto replace_invalid_file_name_characters(std::string name) -> std::string;

} // namespace mesh_import
} // namespace importer
} // namespace unravel
