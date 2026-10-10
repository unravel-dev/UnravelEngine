#pragma once

#include <engine/assets/impl/importers/mesh_importer.h>

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
 * @brief Per-material scalar/vector multipliers from the KHR_materials_pbrSpecularGlossiness
 * extension. The conversion bakes these into the produced base-color and MR textures so the
 * caller can set the material's base_color / metallic / roughness factors to identity and
 * avoid double-application at sample time.
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

/**
 * @brief Result of a spec-gloss -> PBR texture conversion.
 */
struct spec_gloss_pbr_result
{
    bool diffuse_converted{false};   ///< Diffuse mutated to base color and saved to base_color_relative.
    std::string base_color_relative; ///< Relative path of the converted base color file (empty if not produced).
    std::string mr_relative;         ///< Relative path of the sibling metallic-roughness file (empty if not produced).
};

/// Perceived brightness using ITU BT.601 luminance coefficients.
auto perceived_brightness(float r, float g, float b) -> float;

/// Solves the quadratic for metallic from sRGB-encoded diffuse/specular brightness.
auto solve_metallic(float perceived_diffuse, float perceived_specular, float one_minus_specular_strength) -> float;

/// Spec/gloss -> metal/rough for scalar factors: {base color, metallic, roughness}.
auto convert_specular_gloss_to_metallic_roughness(const aiColor3D& diffuse_color,
                                                  const aiColor3D& specular_color,
                                                  float glossiness_factor) -> std::tuple<aiColor3D, float, float>;

/// Whether apply_texture_conversion rewrites the texels of a texture with @p semantic / @p inverse.
auto is_pixel_conversion(const std::string& semantic, bool inverse) -> bool;

/**
 * @brief Rewrite @p image in place for a conversion semantic: "SpecularToMetallicRoughness",
 * "ShininessToRoughness", "GlossinessToRoughness", "ExtractMetallicChannel",
 * "ExtractRoughnessChannel", or a plain inversion when @p inverse is set. Any other semantic leaves
 * the image untouched.
 */
void apply_texture_conversion(bimg::ImageContainer* image, const std::string& semantic, bool inverse);

/**
 * @brief Bake a diffuse + specular(+gloss) pair into a metallic-roughness PNG at @p mr_relative and,
 * when @p bake_base_color is set, the reconstructed base color PNG at @p base_color_relative.
 * Both relative to @p output_dir. The specular map is resized to the diffuse size when they differ.
 */
auto convert_spec_gloss_to_pbr_textures(const fs::path& output_dir,
                                        const std::string& base_color_relative,
                                        const std::string& mr_relative,
                                        bimg::ImageContainer* diffuse_img,
                                        const bimg::ImageContainer* specular_img,
                                        const spec_gloss_factors_t& factors,
                                        bool bake_base_color) -> spec_gloss_pbr_result;

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
