#include "mesh_import_texture_conversion.h"

#include "mesh_import_image.h"
#include "mesh_import_paths.h"

#include "../../asset_writer.h"

#include <logging/logging.h>
#include <math/math.h>
#include <string_utils/utils.h>

#include <graphics/utils/bgfx_utils.h>

#include <assimp/texture.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace unravel
{
namespace importer
{
namespace mesh_import
{
namespace
{

/**
 * @brief Pixel transformation functions for different texture formats
 */
namespace pixel_transforms
{
    /**
     * @brief Quantize a normalized float [0,1] to uint8 with proper rounding.
     * Using truncation (static_cast<uint8_t>(x * 255.0f)) introduces a half-LSB
     * bias toward zero that compounds when a texture is re-converted.
     */
    inline auto to_uint8(float value) -> uint8_t
    {
        return static_cast<uint8_t>(std::lround(math::clamp(value, 0.0f, 1.0f) * 255.0f));
    }

    /**
     * @brief Transform a single pixel based on format and transformation function
     */
    template<typename TransformFunc>
    void transform_pixel(uint8_t* pixel_data, uint32_t bytes_per_pixel, TransformFunc transform_func)
    {
        if (bytes_per_pixel >= 4)
        {
            // RGBA format
            float r = pixel_data[0] / 255.0f;
            float g = pixel_data[1] / 255.0f;
            float b = pixel_data[2] / 255.0f;
            float a = pixel_data[3] / 255.0f;

            auto [new_r, new_g, new_b, new_a] = transform_func(r, g, b, a);

            pixel_data[0] = to_uint8(new_r);
            pixel_data[1] = to_uint8(new_g);
            pixel_data[2] = to_uint8(new_b);
            pixel_data[3] = to_uint8(new_a);
        }
        else if (bytes_per_pixel >= 3)
        {
            // RGB format
            float r = pixel_data[0] / 255.0f;
            float g = pixel_data[1] / 255.0f;
            float b = pixel_data[2] / 255.0f;
            float a = 1.0f; // Default alpha

            auto [new_r, new_g, new_b, new_a] = transform_func(r, g, b, a);

            pixel_data[0] = to_uint8(new_r);
            pixel_data[1] = to_uint8(new_g);
            pixel_data[2] = to_uint8(new_b);
        }
        else if (bytes_per_pixel == 2)
        {
            // Grayscale + Alpha format
            float luminance = pixel_data[0] / 255.0f;
            float a = pixel_data[1] / 255.0f;

            auto [new_r, new_g, new_b, new_a] = transform_func(luminance, luminance, luminance, a);

            pixel_data[0] = to_uint8(new_r); // Use red as luminance
            pixel_data[1] = to_uint8(new_a);
        }
        else if (bytes_per_pixel == 1)
        {
            // Grayscale format
            float luminance = pixel_data[0] / 255.0f;

            auto [new_r, new_g, new_b, new_a] = transform_func(luminance, luminance, luminance, 1.0f);

            pixel_data[0] = to_uint8(new_r);
        }
    }

    /**
     * @brief Convert Phong shininess exponent map to roughness (Beckmann mapping: sqrt(2 / (n + 2))).
     * Texture texels are treated as normalized exponent; scaled to a reference max of 128.
     */
    /**
     * @brief Convert a glossiness map (3ds Max useGlossiness) to roughness.
     */
    auto glossiness_to_roughness_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        const float roughness = 1.0f - std::max({r, g, b});
        return std::make_tuple(roughness, roughness, roughness, 1.0f);
    }

    auto shininess_to_roughness_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        constexpr float k_reference_max_shininess = 128.0f;
        const float shininess = std::max(std::max({r, g, b}) * k_reference_max_shininess, 1.0f);
        const float roughness = std::sqrt(2.0f / (shininess + 2.0f));
        return std::make_tuple(roughness, roughness, roughness, 1.0f);
    }

    /**
     * @brief Shared helper: approximate metallic from specular-only pixel.
     * Uses the quadratic solver with an assumed mid-grey diffuse.
     */
    auto compute_metallic_from_specular(float r, float g, float b) -> float
    {
        constexpr float assumed_diffuse = 0.5f;
        float max_specular = std::max({r, g, b});
        float one_minus_specular_strength = 1.0f - max_specular;
        float perc_diffuse = perceived_brightness(assumed_diffuse, assumed_diffuse, assumed_diffuse);
        float perc_specular = perceived_brightness(r, g, b);
        return solve_metallic(perc_diffuse, perc_specular, one_minus_specular_strength);
    }

    /**
     * @brief Convert specular to combined metallic/roughness using alpha as gloss.
     * glTF convention: R=Occlusion(unused), G=Roughness, B=Metallic, A=1.0
     */
    auto specular_to_metallic_roughness_alpha_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        float metallic = compute_metallic_from_specular(r, g, b);
        float roughness = 1.0f - a;
        return std::make_tuple(1.0f, roughness, metallic, 1.0f);
    }

    /**
     * @brief Convert specular to combined metallic/roughness using intensity for roughness.
     * glTF convention: R=Occlusion(unused), G=Roughness, B=Metallic, A=1.0
     */
    auto specular_to_metallic_roughness_intensity_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        float metallic = compute_metallic_from_specular(r, g, b);
        float avg_specular = (r + g + b) / 3.0f;
        float roughness = 1.0f - avg_specular;
        return std::make_tuple(1.0f, roughness, metallic, 1.0f);
    }

    /**
     * @brief Simple inversion transformation
     */
    auto simple_invert_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        return std::make_tuple(1.0f - r, 1.0f - g, 1.0f - b, 1.0f - a);
    }
}

/**
 * @brief Apply combined specular to metallic+roughness conversion (glTF style).
 * Output layout matches the glTF Metallic-Roughness texture convention:
 *   R = (occlusion placeholder, set to 1.0)
 *   G = roughness
 *   B = metallic
 *   A = 1.0
 * The deferred geometry shader samples G for roughness and B for metallic when the
 * same texture is bound to both the metalness and roughness slots.
 *
 * Prefer the diffuse + specular pair path (`apply_diffuse_to_base_color_conversion`) when
 * a matching albedo texture is available - it uses per-pixel diffuse for the metallic solve.
 */
void apply_specular_to_metallic_roughness_conversion(bimg::ImageContainer* image)
{
    if(!image || !image->m_data)
    {
        return;
    }
    if(!is_supported_ldr_format(image->m_format))
    {
        APPLOG_WARNING("Mesh Importer: Skipping SpecularToMetallicRoughness conversion on unsupported texture format");
        return;
    }

    uint8_t* image_data = static_cast<uint8_t*>(image->m_data);
    uint32_t pixel_count = image->m_width * image->m_height;
    uint32_t bpp = bimg::getBitsPerPixel(image->m_format);
    uint32_t bytes_per_pixel = bpp / 8;

    // The MR pack uses three distinct channels (R/G/B), so a single-channel or
    // luminance+alpha source cannot represent the result. Bail loudly rather than
    // silently dropping the metallic / roughness data via transform_pixel's
    // channel-reduction fallback.
    if(bytes_per_pixel < 3)
    {
        APPLOG_WARNING("Mesh Importer: Skipping SpecularToMetallicRoughness conversion on <3-channel source (cannot pack R/G/B)");
        return;
    }

    // KHR spec/gloss combined maps: alpha is glossiness when RGBA (not cut-out opacity).
    const bool alpha_has_gloss = (bytes_per_pixel >= 4);

    if(alpha_has_gloss)
    {
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index],
                                                bytes_per_pixel,
                                                pixel_transforms::specular_to_metallic_roughness_alpha_pixel);
        }
        APPLOG_TRACE("Mesh Importer: Applied SpecularToMetallicRoughness conversion (alpha=gloss) to texture");
    }
    else
    {
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index],
                                                bytes_per_pixel,
                                                pixel_transforms::specular_to_metallic_roughness_intensity_pixel);
        }
        APPLOG_TRACE("Mesh Importer: Applied SpecularToMetallicRoughness conversion (intensity) to texture");
    }
}

/**
 * @brief Convert a diffuse/specular pair into PBR metallic-roughness textures.
 *
 * The spec-gloss -> metal-rough math is performed in sRGB-encoded float space
 * (i.e. 8-bit channels divided by 255). The perceptual luminance weighting in
 * solve_metallic only holds when inputs stay in sRGB; do NOT degamma here.
 *
 * Per pixel we solve metallic with the proper diffuse luminance (much better than the
 * mid-gray fallback used by `compute_metallic_from_specular`), then reconstruct base
 * color with the identity:
 *   baseColor = mix(diffuse * (1 - F0) / (1 - metallic * F0),
 *                   specular - F0 * (1 - metallic),
 *                   metallic^2)
 *
 * The diffuse_image is rewritten in-memory with the base color result. specular_image
 * is read-only.
 *
 * If `out_mr_rgba8` is non-null, it is filled with `width*height*4` bytes of RGBA8
 * metallic-roughness data matching the glTF MR convention (R=1, G=roughness,
 * B=metallic_per_pixel, A=1).
 *
 * Both images must have the same dimensions and be byte-aligned LDR formats with at
 * least 3 channels.
 */
void apply_diffuse_to_base_color_conversion(bimg::ImageContainer* diffuse_image,
                                            const bimg::ImageContainer* specular_image,
                                            const spec_gloss_factors_t& factors,
                                            std::vector<uint8_t>* out_mr_rgba8,
                                            bool rewrite_diffuse_to_base_color)
{
    if(!diffuse_image || !diffuse_image->m_data || !specular_image || !specular_image->m_data)
    {
        return;
    }
    if(diffuse_image->m_width != specular_image->m_width || diffuse_image->m_height != specular_image->m_height)
    {
        APPLOG_WARNING("Mesh Importer: Diffuse/specular texture size mismatch for base color conversion");
        return;
    }
    if(!is_supported_ldr_format(diffuse_image->m_format) || !is_supported_ldr_format(specular_image->m_format))
    {
        APPLOG_WARNING("Mesh Importer: Diffuse-to-base-color conversion requires uncompressed LDR textures; skipping");
        return;
    }

    uint32_t d_bpp = bimg::getBitsPerPixel(diffuse_image->m_format) / 8;
    uint32_t s_bpp = bimg::getBitsPerPixel(specular_image->m_format) / 8;
    if(d_bpp < 3 || s_bpp < 3)
    {
        // We need actual RGB channels in both inputs; grayscale/L+A sources do not
        // carry the chromatic information the spec-gloss identity needs.
        APPLOG_WARNING("Mesh Importer: Diffuse-to-base-color conversion requires RGB inputs (diffuse={} bpp, specular={} bpp); skipping",
                       d_bpp * 8, s_bpp * 8);
        return;
    }

    constexpr float dielectric_f0 = 0.04f;
    constexpr float epsilon = 1e-6f;

    uint32_t width = diffuse_image->m_width;
    uint32_t height = diffuse_image->m_height;
    uint32_t pixel_count = width * height;
    auto* d_data = static_cast<uint8_t*>(diffuse_image->m_data);
    const auto* s_data = static_cast<const uint8_t*>(specular_image->m_data);

    // KHR_materials_pbrSpecularGlossiness: RGB = specular, A = glossiness.
    // When the specular map has alpha, always use it for per-pixel gloss (not cut-out opacity).
    // Fall back to specular RGB intensity only for RGB-only spec sources.
    bool spec_alpha_has_gloss = (s_bpp >= 4);

    if(out_mr_rgba8 != nullptr)
    {
        out_mr_rgba8->assign(static_cast<size_t>(pixel_count) * 4, 0);
    }

    for(uint32_t i = 0; i < pixel_count; ++i)
    {
        // Apply per-material multipliers from KHR_materials_pbrSpecularGlossiness:
        //   final_diffuse_color    = diffuseTexture.rgb * diffuseFactor.rgb
        //   final_specular_color   = specularTexture.rgb * specularFactor
        //   final_glossiness       = specularTexture.a * glossinessFactor
        // Baking the factors in here lets the caller set the material's base-color /
        // metallic / roughness factor uniforms to identity and avoid double-application
        // (the deferred shader does `albedo *= u_base_color` and `roughness *= tex.g`).
        float dr = static_cast<float>(d_data[i * d_bpp + 0]) / 255.0f * factors.diffuse_r;
        float dg = static_cast<float>(d_data[i * d_bpp + 1]) / 255.0f * factors.diffuse_g;
        float db = static_cast<float>(d_data[i * d_bpp + 2]) / 255.0f * factors.diffuse_b;

        float sr = static_cast<float>(s_data[i * s_bpp + 0]) / 255.0f * factors.specular_r;
        float sg = static_cast<float>(s_data[i * s_bpp + 1]) / 255.0f * factors.specular_g;
        float sb = static_cast<float>(s_data[i * s_bpp + 2]) / 255.0f * factors.specular_b;
        float sa = (s_bpp >= 4) ? static_cast<float>(s_data[i * s_bpp + 3]) / 255.0f * factors.glossiness
                                : factors.glossiness;

        float max_specular = std::max({sr, sg, sb});
        float one_minus_spec_str = 1.0f - max_specular;
        float perc_d = perceived_brightness(dr, dg, db);
        float perc_s = perceived_brightness(sr, sg, sb);
        float metallic = solve_metallic(perc_d, perc_s, one_minus_spec_str);

        // Base color reconstruction:
        //   baseColorFromDiffuse  = diffuse * (1 - F0) / (1 - metallic * F0)
        //   baseColorFromSpecular = specular - F0 * (1 - metallic)
        //   baseColor = mix(baseColorFromDiffuse, baseColorFromSpecular, metallic^2)
        // A `one_minus_spec_str / (1 - metallic)` factor in the diffuse term would
        // over-weight the diffuse for high-metallic pixels and let things like the rust
        // tones on a metal helm bleed into the final base color.
        float denom = std::max(1.0f - metallic * dielectric_f0, epsilon);
        float spec_offset = dielectric_f0 * (1.0f - metallic);
        float t = metallic * metallic;

        auto base_d = [&](float d) -> float { return d * (1.0f - dielectric_f0) / denom; };
        auto base_s = [&](float s) -> float { return s - spec_offset; };

        if(rewrite_diffuse_to_base_color)
        {
            float br = math::mix(base_d(dr), base_s(sr), t);
            float bg = math::mix(base_d(dg), base_s(sg), t);
            float bb = math::mix(base_d(db), base_s(sb), t);

            d_data[i * d_bpp + 0] = pixel_transforms::to_uint8(br);
            d_data[i * d_bpp + 1] = pixel_transforms::to_uint8(bg);
            d_data[i * d_bpp + 2] = pixel_transforms::to_uint8(bb);

            // Bake diffuseFactor.a into the base color alpha so material transparency
            // doesn't get lost. Skip when no alpha channel is present.
            if(d_bpp >= 4)
            {
                float da = static_cast<float>(d_data[i * d_bpp + 3]) / 255.0f * factors.diffuse_a;
                d_data[i * d_bpp + 3] = pixel_transforms::to_uint8(da);
            }
        }

        if(out_mr_rgba8 != nullptr)
        {
            float roughness = spec_alpha_has_gloss
                                  ? (1.0f - sa)
                                  : (1.0f - (sr + sg + sb) / 3.0f);

            uint8_t* mr = out_mr_rgba8->data() + static_cast<size_t>(i) * 4;
            mr[0] = 255; // R = occlusion placeholder
            mr[1] = pixel_transforms::to_uint8(roughness);
            mr[2] = pixel_transforms::to_uint8(metallic);
            mr[3] = 255;
        }
    }

    APPLOG_TRACE("Mesh Importer: Applied spec-gloss conversion{} (rewrite base color: {})",
                 out_mr_rgba8 ? " with sibling metallic-roughness map" : "",
                 rewrite_diffuse_to_base_color);
}

/**
 * @brief Process raw texture data with conversions
 */
/**
 * @brief Whether an embedded compressed texture is extracted byte for byte: its texels need no
 * conversion and @p output_file carries the extension of the format it is stored in.
 */
auto is_original_file_extraction(const aiTexture* assimp_tex, const fs::path& output_file, const imported_texture& texture)
    -> bool
{
    const std::string stored_extension = get_compressed_texture_extension(assimp_tex);
    return !is_pixel_conversion(texture.semantic, texture.inverse) && !stored_extension.empty()
           && string_utils::to_lower(output_file.extension().string()) == stored_extension;
}

/// Write an embedded compressed texture's original bytes: no decode, no re-encode, no quality loss.
void write_embedded_file(const aiTexture* assimp_tex, const fs::path& output_file)
{
    fs::error_code ec;
    asset_writer::atomic_write_file(output_file,
                                    [&](const fs::path& temp)
                                    {
                                        std::ofstream stream(temp, std::ios::binary);
                                        stream.write(reinterpret_cast<const char*>(assimp_tex->pcData),
                                                     static_cast<std::streamsize>(assimp_tex->mWidth));
                                    },
                                    ec);
    if(ec)
    {
        APPLOG_WARNING("Mesh Importer: Failed to extract embedded texture '{}': {}", output_file.generic_string(), ec.message());
    }
}

void process_raw_texture_data(const aiTexture* assimp_tex, const fs::path& output_file, 
                             const std::string& semantic, bool inverse)
{
    // For raw textures, we need to create a temporary image container to apply conversions
    uint32_t width = assimp_tex->mWidth;
    uint32_t height = assimp_tex->mHeight;

    // aiTexel is laid out b, g, r, a; everything below works on RGBA8.
    std::vector<uint8_t> data(static_cast<size_t>(width) * height * 4);
    for(size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
    {
        const aiTexel& texel = assimp_tex->pcData[i];
        data[i * 4 + 0] = texel.r;
        data[i * 4 + 1] = texel.g;
        data[i * 4 + 2] = texel.b;
        data[i * 4 + 3] = texel.a;
    }

    // Apply conversions to the copied data
    if(is_pixel_conversion(semantic, false))
    {
        // Create a temporary image container for conversion
        bimg::ImageContainer image;
        image.m_data = data.data();
        image.m_width = width;
        image.m_height = height;
        image.m_depth = 0; // Not a volume: bimg reserves a non-zero depth for 3D textures.
        image.m_format = bimg::TextureFormat::RGBA8;
        image.m_numMips = 1;
        image.m_hasAlpha = true;
        
        apply_texture_conversion(&image, semantic, inverse);
    }
    else if(inverse)
    {
        // Simple inversion
        for(size_t i = 0; i < data.size(); ++i)
        {
            data[i] = 255 - data[i];
        }
    }
    
    // Write the processed data as PNG. Avoid TGA here for the same reason as
    // write_rgba8_png: bimg::imageWriteTga writes the buffer raw under a Type-2
    // header but TGA's wire format is BGRA, so RGBA bytes load back R<->B-swapped.
    write_rgba8_png(output_file, width, height, data.data());
}

} // namespace

/**
 * @brief Perceived brightness using ITU BT.601 luminance coefficients.
 */
auto perceived_brightness(float r, float g, float b) -> float
{
    return std::sqrt(0.299f * r * r + 0.587f * g * g + 0.114f * b * b);
}

/**
 * @brief Solve the quadratic for metallic.
 *
 * The PBR identity for specular is: specular = lerp(dielectricF0, baseColor, metallic)
 * Combined with the diffuse identity, this yields a quadratic in metallic that we solve here.
 *
 * IMPORTANT: this routine is intentionally evaluated in sRGB-ENCODED float space (i.e. 8-bit
 * channels divided by 255). The BT.601 perceptual luminance in `perceived_brightness` and
 * the dielectric F0=0.04 are both calibrated for that color space. Do NOT degamma to linear
 * before calling - it will skew the metallic estimate.
 */
auto solve_metallic(float perceived_diffuse, float perceived_specular, float one_minus_specular_strength) -> float
{
    constexpr float dielectric_f0 = 0.04f;

    if(perceived_specular < dielectric_f0)
    {
        return 0.0f;
    }

    float a = dielectric_f0;
    float b = perceived_diffuse * one_minus_specular_strength / (1.0f - dielectric_f0) + perceived_specular - 2.0f * dielectric_f0;
    float c = dielectric_f0 - perceived_specular;
    float discriminant = std::max(b * b - 4.0f * a * c, 0.0f);

    return math::clamp((-b + std::sqrt(discriminant)) / (2.0f * a), 0.0f, 1.0f);
}

/**
 * @brief Convert specular/gloss factors to metallic/roughness: {base color, metallic, roughness}.
 */
auto convert_specular_gloss_to_metallic_roughness(const aiColor3D& diffuse_color,
                                                 const aiColor3D& specular_color,
                                                 float glossiness_factor) -> std::tuple<aiColor3D, float, float>
{
    constexpr float dielectric_f0 = 0.04f;
    constexpr float epsilon = 1e-6f;

    float max_specular = std::max({specular_color.r, specular_color.g, specular_color.b});
    float one_minus_specular_strength = 1.0f - max_specular;

    float perceived_diffuse = perceived_brightness(diffuse_color.r, diffuse_color.g, diffuse_color.b);
    float perceived_specular = perceived_brightness(specular_color.r, specular_color.g, specular_color.b);

    float metallic = solve_metallic(perceived_diffuse, perceived_specular, one_minus_specular_strength);

    // Base color reconstruction:
    //   baseColorFromDiffuse  = diffuse * (1 - F0) / (1 - metallic * F0)
    //   baseColorFromSpecular = specular - F0 * (1 - metallic)
    //   baseColor = mix(baseColorFromDiffuse, baseColorFromSpecular, metallic^2)
    float denom = std::max(1.0f - metallic * dielectric_f0, epsilon);
    float spec_offset = dielectric_f0 * (1.0f - metallic);

    auto base_from_diffuse = [&](float d) -> float { return d * (1.0f - dielectric_f0) / denom; };
    auto base_from_specular = [&](float s) -> float { return s - spec_offset; };

    float t = metallic * metallic;
    aiColor3D base_color;
    base_color.r = math::mix(base_from_diffuse(diffuse_color.r), base_from_specular(specular_color.r), t);
    base_color.g = math::mix(base_from_diffuse(diffuse_color.g), base_from_specular(specular_color.g), t);
    base_color.b = math::mix(base_from_diffuse(diffuse_color.b), base_from_specular(specular_color.b), t);

    float roughness = 1.0f - glossiness_factor;

    base_color.r = math::clamp(base_color.r, 0.0f, 1.0f);
    base_color.g = math::clamp(base_color.g, 0.0f, 1.0f);
    base_color.b = math::clamp(base_color.b, 0.0f, 1.0f);
    metallic = math::clamp(metallic, 0.0f, 1.0f);
    roughness = math::clamp(roughness, 0.0f, 1.0f);

    return std::make_tuple(base_color, metallic, roughness);
}

/**
 * @brief Apply texture conversion using modular pixel transformations
 */
auto is_pixel_conversion(const std::string& semantic, bool inverse) -> bool
{
    static constexpr std::array<const char*, 5> converting_semantics = {
        "SpecularToMetallicRoughness",
        "ShininessToRoughness",
        "GlossinessToRoughness",
        "ExtractMetallicChannel",
        "ExtractRoughnessChannel",
    };
    return inverse
           || std::find(converting_semantics.begin(), converting_semantics.end(), semantic) != converting_semantics.end();
}

void apply_texture_conversion(bimg::ImageContainer* image, const std::string& semantic, bool inverse)
{
    if(!image || !image->m_data)
    {
        return;
    }
    if(!is_supported_ldr_format(image->m_format))
    {
        APPLOG_WARNING("Mesh Importer: Skipping {} conversion on unsupported texture format (compressed/float/non-byte-aligned)", semantic);
        return;
    }

    uint8_t* image_data = static_cast<uint8_t*>(image->m_data);
    uint32_t pixel_count = image->m_width * image->m_height;
    uint32_t bpp = bimg::getBitsPerPixel(image->m_format);
    uint32_t bytes_per_pixel = bpp / 8;
    
    if(semantic == "SpecularToMetallicRoughness")
    {
        apply_specular_to_metallic_roughness_conversion(image);
        return;
    }
    else if(semantic == "GlossinessToRoughness")
    {
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel,
                                            pixel_transforms::glossiness_to_roughness_pixel);
        }
        APPLOG_TRACE("Mesh Importer: Applied GlossinessToRoughness conversion to texture");
    }
    else if(semantic == "ShininessToRoughness")
    {
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel,
                                            pixel_transforms::shininess_to_roughness_pixel);
        }
        APPLOG_TRACE("Mesh Importer: Applied ShininessToRoughness conversion to texture");
    }
    else if(semantic == "ExtractMetallicChannel")
    {
        // Extract metallic channel from combined texture (Blue channel in glTF standard)
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel, 
                                            [](float r, float g, float b, float a) {
                                                // Extract metallic from blue channel and make it grayscale
                                                return std::make_tuple(b, b, b, 1.0f);
                                            });
        }
        APPLOG_TRACE("Mesh Importer: Extracted metallic channel for debugging");
    }
    else if(semantic == "ExtractRoughnessChannel")
    {
        // Extract roughness channel from combined texture (Green channel in glTF standard)
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel, 
                                            [](float r, float g, float b, float a) {
                                                // Extract roughness from green channel and make it grayscale
                                                return std::make_tuple(g, g, g, 1.0f);
                                            });
        }
        APPLOG_TRACE("Mesh Importer: Extracted roughness channel for debugging");
    }
    else if(inverse)
    {
        // Simple inversion for other cases where inverse flag is set
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel, 
                                            pixel_transforms::simple_invert_pixel);
        }
        APPLOG_TRACE("Mesh Importer: Applied simple inversion to texture");
    }
}

auto convert_spec_gloss_to_pbr_textures(const fs::path& output_dir,
                                        const std::string& base_color_relative,
                                        const std::string& mr_relative,
                                        bimg::ImageContainer* diffuse_img,
                                        const bimg::ImageContainer* specular_img,
                                        const spec_gloss_factors_t& factors,
                                        bool bake_base_color) -> spec_gloss_pbr_result
{
    spec_gloss_pbr_result result{};

    if(!diffuse_img || !specular_img)
    {
        return result;
    }

    // Normalize both inputs to RGBA8 so byte-offset reads and the PNG writer see
    // a consistent layout. imageConvert may return the same pointer if the source
    // is already RGBA8.
    bool diffuse_was_converted = false;
    bool specular_was_converted = false;
    bimg::ImageContainer* diffuse_rgba8 = ensure_rgba8(diffuse_img, diffuse_was_converted);
    bimg::ImageContainer* specular_rgba8 = ensure_rgba8(const_cast<bimg::ImageContainer*>(specular_img), specular_was_converted);

    bool specular_resized = false;
    bimg::ImageContainer* specular_work = specular_rgba8;

    auto free_intermediates = [&]()
    {
        if(specular_resized && specular_work)
        {
            bimg::imageFree(specular_work);
        }
        if(diffuse_was_converted && diffuse_rgba8)
        {
            bimg::imageFree(diffuse_rgba8);
        }
        if(specular_was_converted && specular_rgba8)
        {
            bimg::imageFree(specular_rgba8);
        }
    };

    if(!diffuse_rgba8 || !specular_rgba8)
    {
        APPLOG_WARNING("Mesh Importer: Spec-gloss conversion skipped - could not normalize inputs to RGBA8");
        free_intermediates();
        return result;
    }

    if(diffuse_rgba8->m_width != specular_rgba8->m_width || diffuse_rgba8->m_height != specular_rgba8->m_height)
    {
        bimg::ImageContainer* resized =
            resize_rgba8_image_to(specular_rgba8, diffuse_rgba8->m_width, diffuse_rgba8->m_height);
        if(!resized)
        {
            APPLOG_WARNING("Mesh Importer: Failed to resize specular {}x{} to match diffuse {}x{} for spec-gloss conversion",
                           specular_rgba8->m_width,
                           specular_rgba8->m_height,
                           diffuse_rgba8->m_width,
                           diffuse_rgba8->m_height);
            free_intermediates();
            return result;
        }
        specular_work = resized;
        specular_resized = true;
        APPLOG_TRACE("Mesh Importer: Upscaled specular {}x{} -> {}x{} for spec-gloss pair conversion",
                     specular_rgba8->m_width,
                     specular_rgba8->m_height,
                     diffuse_rgba8->m_width,
                     diffuse_rgba8->m_height);
    }

    std::vector<uint8_t> mr_buffer;
    apply_diffuse_to_base_color_conversion(diffuse_rgba8,
                                           specular_work,
                                           factors,
                                           &mr_buffer,
                                           bake_base_color);

    // The conversion bails (logged) if formats are incompatible or sizes mismatch.
    if(mr_buffer.empty())
    {
        APPLOG_WARNING("Mesh Importer: Spec-gloss conversion produced no output (size mismatch or unsupported format)");
        free_intermediates();
        return result;
    }

    if(bake_base_color)
    {
        if(!write_rgba8_png(output_dir / base_color_relative,
                            diffuse_rgba8->m_width,
                            diffuse_rgba8->m_height,
                            static_cast<const uint8_t*>(diffuse_rgba8->m_data)))
        {
            APPLOG_WARNING("Mesh Importer: Failed to save converted base color texture: {}", base_color_relative);
            free_intermediates();
            return result;
        }
        result.diffuse_converted = true;
        result.base_color_relative = base_color_relative;
    }

    if(write_rgba8_png(output_dir / mr_relative, diffuse_rgba8->m_width, diffuse_rgba8->m_height, mr_buffer.data()))
    {
        result.mr_relative = mr_relative;
    }
    else
    {
        APPLOG_WARNING("Mesh Importer: Failed to save sibling metallic-roughness texture for spec-gloss conversion");
    }

    free_intermediates();
    return result;
}

void process_embedded_texture(const aiTexture* assimp_tex,
                              size_t assimp_tex_idx,
                              const fs::path& filename,
                              const fs::path& output_dir,
                              std::vector<imported_texture>& textures)
{
    imported_texture texture{};
    // Search backwards: the caller just pushed the target entry at the back.
    auto rit = std::find_if(textures.rbegin(),
                            textures.rend(),
                            [&](const imported_texture& texture)
                            {
                                return texture.embedded_index == static_cast<int>(assimp_tex_idx);
                            });
    if(rit != textures.rend())
    {
        if(rit->process_count > 0)
        {
            return;
        }

        rit->process_count++;
        texture = *rit;
    }
    else if(assimp_tex->mFilename.length > 0)
    {
        texture.name = normalize_assimp_path(assimp_tex->mFilename.C_Str()).filename().string();
    }
    else
    {
        texture.name = get_embedded_texture_name(assimp_tex, assimp_tex_idx, filename, "Texture", false);
    }

    fs::path output_file = output_dir / texture.name;

    if(assimp_tex->pcData)
    {
        bool compressed = assimp_tex->mHeight == 0;
        bool raw = assimp_tex->mHeight > 0;

        if(compressed && is_original_file_extraction(assimp_tex, output_file, texture))
        {
            write_embedded_file(assimp_tex, output_file);
        }
        else if(compressed)
        {
            // Compressed texture (e.g., PNG, JPEG)
            size_t texture_size = assimp_tex->mWidth;

            // Parse the image using bimg
            bimg::ImageContainer* image = imageLoad(assimp_tex->pcData, static_cast<uint32_t>(texture_size));
            if(image)
            {
                // Apply workflow-specific texture conversions
                apply_texture_conversion(image, texture.semantic, texture.inverse);

                atomic_image_save(output_file, image);

                bimg::imageFree(image);
            }
        }
        else if(raw)
        {
            // Uncompressed texture (e.g., raw RGBA)
            // For raw data, we need to process it differently
            process_raw_texture_data(assimp_tex, output_file, texture.semantic, texture.inverse);
        }
    }
}

} // namespace mesh_import
} // namespace importer
} // namespace unravel
