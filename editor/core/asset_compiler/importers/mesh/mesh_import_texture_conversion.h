#pragma once

#include <asset_compiler/importers/mesh_importer.h>

#include <filesystem/filesystem.h>

#include <assimp/types.h>
#include <bimg/bimg.h>

#include <cstddef>
#include <string>
#include <tuple>
#include <vector>

struct aiTexture;

namespace unravel
{
namespace importer
{
namespace mesh_import
{

/**
 * @brief Per-material multipliers from the KHR_materials_pbrSpecularGlossiness extension: the diffuse factor of the
 * diffuse map, the specular factor of the specular map's rgb, the glossiness factor of its alpha.
 */
struct spec_gloss_factors_t
{
    float diffuse_r{1.0f};
    float diffuse_g{1.0f};
    float diffuse_b{1.0f};
    float diffuse_a{1.0f};
    float specular_r{1.0f};
    float specular_g{1.0f};
    float specular_b{1.0f};
    float glossiness{1.0f};
};

/// Whether apply_texture_conversion rewrites the texels of a texture with @p semantic / @p inverse.
auto is_pixel_conversion(const std::string& semantic, bool inverse) -> bool;

/**
 * @brief Rewrite @p image in place for a conversion semantic: "ShininessToRoughness", "GlossinessToRoughness",
 * "ExtractMetallicChannel",
 * "ExtractRoughnessChannel", or a plain inversion when @p inverse is set. Any other semantic leaves
 * the image untouched.
 */
void apply_texture_conversion(bimg::ImageContainer* image, const std::string& semantic, bool inverse);

/**
 * @brief Extract embedded texture @p assimp_tex_idx into @p output_dir, applying the conversion of the
 * manifest entry that requested it (searched from the back of @p textures; extracted at most once).
 */
void process_embedded_texture(const aiTexture* assimp_tex,
                              size_t assimp_tex_idx,
                              const fs::path& filename,
                              const fs::path& output_dir,
                              std::vector<imported_texture>& textures);

} // namespace mesh_import
} // namespace importer
} // namespace unravel
