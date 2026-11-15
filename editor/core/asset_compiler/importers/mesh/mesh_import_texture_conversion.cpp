#include "mesh_import_texture_conversion.h"

#include "mesh_import_image.h"
#include "mesh_import_paths.h"

#include <engine/assets/impl/asset_writer.h>

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
     * @brief Simple inversion transformation
     */
    auto simple_invert_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        return std::make_tuple(1.0f - r, 1.0f - g, 1.0f - b, 1.0f - a);
    }
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
 * @brief Apply texture conversion using modular pixel transformations
 */
auto is_pixel_conversion(const std::string& semantic, bool inverse) -> bool
{
    static constexpr std::array<const char*, 4> converting_semantics = {
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
    
    if(semantic == "GlossinessToRoughness")
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
