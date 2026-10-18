#pragma once

#include <filesystem/filesystem.h>

#include <bimg/bimg.h>
#include <bx/allocator.h>

#include <cstdint>

namespace unravel
{
namespace importer
{
namespace mesh_import
{

/// Alpha at or above this counts as opaque when probing a color map border for cut-outs.
inline constexpr float k_import_border_alpha_opaque_threshold = 0.95f;

/**
 * @brief Check that an image format is an uncompressed, non-float LDR layout we can
 * safely byte-walk in our per-pixel conversion routines. Floating-point/HDR and
 * block-compressed formats would silently corrupt if treated as 8-bit channels.
 */
inline auto is_supported_ldr_format(bimg::TextureFormat::Enum format) -> bool
{
    if(bimg::isCompressed(format) || bimg::isFloat(format))
    {
        return false;
    }
    uint32_t bpp = bimg::getBitsPerPixel(format);
    return bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32;
}

/**
 * @brief Process-wide allocator used by bimg's conversion / allocation entry points.
 * bgfx's entry::getAllocator() is in an anonymous namespace inside bgfx_utils.cpp so
 * we keep our own bx::DefaultAllocator instance here.
 */
inline auto get_bimg_allocator() -> bx::AllocatorI*
{
    static thread_local bx::DefaultAllocator allocator;
    return &allocator;
}

/**
 * @brief Ensure an image container is RGBA8 layout. If the source is anything else
 * (RGB8, R8, RG8 ...) the result is a freshly allocated RGBA8 container - the caller
 * is responsible for freeing it (@p owns_result is set). Returns nullptr on failure.
 * Pass-through (no copy) when the input is already RGBA8.
 */
auto ensure_rgba8(bimg::ImageContainer* image, bool& owns_result) -> bimg::ImageContainer*;

/**
 * @brief Bilinear upscale/downscale an RGBA8 image to the target dimensions.
 * Returns a newly allocated container the caller must free, or nullptr on failure
 * (including when the size already matches).
 */
auto resize_rgba8_image_to(const bimg::ImageContainer* src, uint32_t target_w, uint32_t target_h)
    -> bimg::ImageContainer*;

/// Save @p image through a watcher-safe temp file; the format follows @p output_file's extension.
auto atomic_image_save(const fs::path& output_file, bimg::ImageContainer* image) -> bool;

/// Write a raw RGBA8 buffer as PNG through a watcher-safe temp file.
auto write_rgba8_png(const fs::path& output_file, uint32_t width, uint32_t height, const uint8_t* rgba8_data)
    -> bool;

/// Whether texels of @p format can carry alpha (block-compressed alpha formats included).
auto texture_format_has_alpha(bimg::TextureFormat::Enum format, bool parser_reported_alpha) -> bool;

/**
 * @brief Whether a DX10 DDS header declares straight or premultiplied alpha. bimg ignores the field,
 * and for BC1 it is the only thing telling punch-through alpha from opaque data.
 */
auto dds_header_declares_alpha(const fs::path& path) -> bool;

/// Whether any texel on the outer border of mip 0 has alpha below @p opaque_threshold.
auto image_border_has_transparency(const bimg::ImageContainer& image, float opaque_threshold) -> bool;

} // namespace mesh_import
} // namespace importer
} // namespace unravel
