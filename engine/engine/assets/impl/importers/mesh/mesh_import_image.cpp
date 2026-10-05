#include "mesh_import_image.h"

#include "../../asset_writer.h"

#include <graphics/utils/bgfx_utils.h>
#include <logging/logging.h>

#include <bimg/decode.h>
#include <bimg/encode.h>
#include <bx/file.h>

#include <array>
#include <cstring>
#include <fstream>
#include <vector>

namespace unravel
{
namespace importer
{
namespace mesh_import
{
namespace
{

auto compressed_texture_format_has_alpha(bimg::TextureFormat::Enum format) -> bool
{
    switch(format)
    {
    case bimg::TextureFormat::BC2:     // DXT3 - explicit alpha
    case bimg::TextureFormat::BC3:     // DXT5 - interpolated alpha
    case bimg::TextureFormat::BC7:
    case bimg::TextureFormat::ETC2A:
    case bimg::TextureFormat::ETC2A1:
    case bimg::TextureFormat::PTC12A:
    case bimg::TextureFormat::PTC14A:
    case bimg::TextureFormat::ATCE:
    case bimg::TextureFormat::ATCI:
        return true;
    default:
        break;
    }

    if(format >= bimg::TextureFormat::ASTC4x4 && format <= bimg::TextureFormat::ASTC12x12)
    {
        return true;
    }

    return false;
}

auto image_mip_border_has_transparency(const bimg::ImageMip& mip,
                                       bimg::TextureFormat::Enum format,
                                       float opaque_threshold) -> bool
{
    if(mip.m_width == 0 || mip.m_height == 0 || !mip.m_data)
    {
        return false;
    }

    const bimg::UnpackFn unpack = bimg::getUnpack(format);
    if(!unpack)
    {
        return false;
    }

    const uint32_t bpp = bimg::getBitsPerPixel(format);
    if(bpp == 0 || (bpp % 8) != 0)
    {
        return false;
    }

    const uint32_t bytes_per_pixel = bpp / 8;
    const uint32_t width = mip.m_width;
    const uint32_t height = mip.m_height;
    const uint32_t row_stride = width * bytes_per_pixel;

    auto alpha_below_threshold = [&](uint32_t x, uint32_t y) -> bool
    {
        const uint8_t* pixel = mip.m_data + (static_cast<size_t>(y) * row_stride + x * bytes_per_pixel);
        float rgba[4];
        unpack(rgba, pixel);
        return rgba[3] < opaque_threshold;
    };

    for(uint32_t x = 0; x < width; ++x)
    {
        if(alpha_below_threshold(x, 0) || alpha_below_threshold(x, height - 1))
        {
            return true;
        }
    }

    for(uint32_t y = 1; y + 1 < height; ++y)
    {
        if(alpha_below_threshold(0, y) || alpha_below_threshold(width - 1, y))
        {
            return true;
        }
    }

    return false;
}

} // namespace

/**
 * @brief Ensure an image container is RGBA8 layout. If the source is anything else
 * (RGB8, R8, RG8 ...) the result is a freshly allocated RGBA8 container - the caller
 * is responsible for freeing it. Returns nullptr on failure. Pass-through (no copy)
 * when the input is already RGBA8.
 *
 * Critical because the downstream PNG writer expects pitch = width*4 / RGBA8.
 * Feeding it a 3-bpp RGB8 buffer produces garbage in the saved file.
 */
auto ensure_rgba8(bimg::ImageContainer* image, bool& owns_result) -> bimg::ImageContainer*
{
    owns_result = false;
    if(!image)
    {
        return nullptr;
    }
    if(image->m_format == bimg::TextureFormat::RGBA8)
    {
        return image;
    }
    auto* converted = bimg::imageConvert(get_bimg_allocator(), bimg::TextureFormat::RGBA8, *image);
    if(!converted)
    {
        APPLOG_WARNING("Mesh Importer: Failed to convert image to RGBA8 (source format = {})",
                       bimg::getName(image->m_format));
        return nullptr;
    }
    owns_result = true;
    return converted;
}

/**
 * @brief Bilinear upscale/downscale an RGBA8 image to the target dimensions.
 * Returns a newly allocated container the caller must free, or nullptr on failure.
 */
auto resize_rgba8_image_to(const bimg::ImageContainer* src, uint32_t target_w, uint32_t target_h) -> bimg::ImageContainer*
{
    if(!src || !src->m_data || target_w == 0 || target_h == 0)
    {
        return nullptr;
    }
    if(src->m_width == target_w && src->m_height == target_h)
    {
        return nullptr;
    }

    bimg::ImageContainer* src32f =
        bimg::imageConvert(get_bimg_allocator(), bimg::TextureFormat::RGBA32F, *src, false);
    if(!src32f)
    {
        return nullptr;
    }

    // Depth 0: bimg reserves a non-zero depth for volume textures.
    bimg::ImageContainer* dst32f = bimg::imageAlloc(get_bimg_allocator(),
                                                    bimg::TextureFormat::RGBA32F,
                                                    static_cast<uint16_t>(target_w),
                                                    static_cast<uint16_t>(target_h),
                                                    0,
                                                    1,
                                                    false,
                                                    false);
    if(!dst32f || !bimg::imageResizeRgba32fLinear(dst32f, src32f))
    {
        bimg::imageFree(src32f);
        if(dst32f)
        {
            bimg::imageFree(dst32f);
        }
        return nullptr;
    }

    bimg::imageFree(src32f);

    auto* dst8 = bimg::imageConvert(get_bimg_allocator(), bimg::TextureFormat::RGBA8, *dst32f, false);
    bimg::imageFree(dst32f);
    return dst8;
}

/**
 * @brief Atomically save an ImageContainer via imageSave.
 * Callback only writes the temp path; atomic_write_file owns rename/cleanup.
 */
auto atomic_image_save(const fs::path& output_file, bimg::ImageContainer* image) -> bool
{
    fs::error_code ec;
    bool wrote = false;
    const std::string format_hint = output_file.string();
    asset_writer::atomic_write_file(
        output_file,
        [&](const fs::path& temp)
        {
            // Temp is `.<uuid>.temp` (watcher-safe). Pass the final destination as
            // format_hint so imageSave can still pick dds/png/etc.
            wrote = imageSave(temp.string().c_str(), image, format_hint.c_str());
            if(!wrote)
            {
                fs::error_code remove_ec;
                fs::remove(temp, remove_ec);
            }
        },
        ec);
    return wrote && !ec;
}

/**
 * @brief Write a raw RGBA8 buffer to a PNG file. Used for sibling outputs that
 * we synthesize directly without going through bimg::ImageContainer.
 *
 * We deliberately avoid TGA here: bimg::imageWriteTga dumps the source buffer
 * raw under a Type-2 header, but the TGA spec mandates BGRA byte order on disk.
 * Feeding it RGBA bytes (as bimg::ImageContainer stores them) produces a file
 * that stb_image - and any other compliant TGA reader - re-interprets as BGRA,
 * yielding an R<->B swap at load time. PNG carries explicit format metadata and
 * imageWritePng honors the RGBA8 parameter, so this round-trips correctly.
 *
 * Writes through asset_writer::atomic_write_file so watchers never observe a
 * partial PNG.
 */
auto write_rgba8_png(const fs::path& output_file,
                     uint32_t width,
                     uint32_t height,
                     const uint8_t* rgba8_data) -> bool
{
    fs::error_code ec;
    asset_writer::atomic_write_file(
        output_file,
        [&](const fs::path& temp)
        {
            bx::FileWriter writer;
            bx::Error err;
            if(!bx::open(&writer, temp.string().c_str(), false, &err))
            {
                return;
            }
            bimg::imageWritePng(&writer,
                                width,
                                height,
                                width * 4,
                                rgba8_data,
                                bimg::TextureFormat::RGBA8,
                                false,
                                &err);
            bx::close(&writer);
        },
        ec);
    return !ec;
}

auto texture_format_has_alpha(bimg::TextureFormat::Enum format, bool parser_reported_alpha) -> bool
{
    if(parser_reported_alpha)
    {
        return true;
    }

    if(!bimg::isValid(format))
    {
        return false;
    }

    if(bimg::getBlockInfo(format).aBits > 0)
    {
        return true;
    }

    // bimg block info has aBits=0 for block-compressed formats; DDS DXT5/BC3 also omits
    // DDPF_ALPHAPIXELS so m_hasAlpha stays false even though the block encoding has alpha.
    if(bimg::isCompressed(format) && compressed_texture_format_has_alpha(format))
    {
        return true;
    }

    return false;
}

auto image_border_has_transparency(const bimg::ImageContainer& image, float opaque_threshold) -> bool
{
    if(image.m_width == 0 || image.m_height == 0 || !image.m_data)
    {
        return false;
    }

    if(!texture_format_has_alpha(image.m_format, image.m_hasAlpha))
    {
        return false;
    }

    bimg::ImageMip mip{};
    if(!bimg::imageGetRawData(image, 0, 0, image.m_data, image.m_size, mip))
    {
        return false;
    }

    if(!bimg::isCompressed(image.m_format) && bimg::getUnpack(image.m_format) != nullptr)
    {
        return image_mip_border_has_transparency(mip, image.m_format, opaque_threshold);
    }

    if(bimg::isCompressed(image.m_format))
    {
        const uint32_t width = mip.m_width;
        const uint32_t height = mip.m_height;
        if(width == 0 || height == 0)
        {
            return false;
        }

        std::vector<uint8_t> decoded(static_cast<size_t>(width) * height * 4);
        bimg::imageDecodeToRgba8(get_bimg_allocator(),
                                 decoded.data(),
                                 mip.m_data,
                                 width,
                                 height,
                                 width * 4,
                                 image.m_format);

        bimg::ImageMip decoded_mip{};
        decoded_mip.m_format = bimg::TextureFormat::RGBA8;
        decoded_mip.m_width = width;
        decoded_mip.m_height = height;
        decoded_mip.m_depth = 1;
        decoded_mip.m_bpp = 32;
        decoded_mip.m_hasAlpha = true;
        decoded_mip.m_data = decoded.data();

        return image_mip_border_has_transparency(decoded_mip, bimg::TextureFormat::RGBA8, opaque_threshold);
    }

    bimg::ImageContainer* converted =
        bimg::imageConvert(get_bimg_allocator(), bimg::TextureFormat::RGBA8, image, false);
    if(!converted)
    {
        return false;
    }

    const bool suggests_cutout = image_border_has_transparency(*converted, opaque_threshold);
    bimg::imageFree(converted);
    return suggests_cutout;
}

auto dds_header_declares_alpha(const fs::path& path) -> bool
{
    constexpr uint32_t dds_magic = 0x20534444;   // "DDS "
    constexpr uint32_t dx10_fourcc = 0x30315844; // "DX10"
    constexpr size_t fourcc_offset = 84;         // magic + DDS_HEADER up to ddspf.dwFourCC
    constexpr size_t misc_flags2_offset = 144;   // magic + DDS_HEADER + DDS_HEADER_DXT10 up to miscFlags2
    constexpr uint32_t alpha_mode_mask = 0x7;
    constexpr uint32_t alpha_mode_straight = 1;
    constexpr uint32_t alpha_mode_premultiplied = 2;
    std::array<char, misc_flags2_offset + sizeof(uint32_t)> header{};
    std::ifstream stream(path, std::ios::binary);
    if(!stream.read(header.data(), header.size()))
    {
        return false;
    }
    const auto read_u32 = [&](size_t offset)
    {
        uint32_t value = 0;
        std::memcpy(&value, header.data() + offset, sizeof(value));
        return value;
    };
    if(read_u32(0) != dds_magic || read_u32(fourcc_offset) != dx10_fourcc)
    {
        return false;
    }
    const uint32_t alpha_mode = read_u32(misc_flags2_offset) & alpha_mode_mask;
    return alpha_mode == alpha_mode_straight || alpha_mode == alpha_mode_premultiplied;
}

} // namespace mesh_import
} // namespace importer
} // namespace unravel
