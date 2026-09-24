#include "format.h"

namespace gfx
{

auto get_best_float_format(std::uint16_t type_flags,
                           std::uint32_t search_flags,
                           bool requires_alpha,
                           bool accept_padding,
                           bool accept_half,
                           bool accept_full) -> bgfx::TextureFormat::Enum
{
    if(search_flags & format_search_flags::four_channels)
    {
        if(accept_full && is_format_supported(type_flags, bgfx::TextureFormat::RGBA32F))
            return bgfx::TextureFormat::RGBA32F;
        if(accept_half && is_format_supported(type_flags, bgfx::TextureFormat::RGBA16F))
            return bgfx::TextureFormat::RGBA16F;
    }
    else if(search_flags & format_search_flags::two_channels)
    {
        if(!requires_alpha)
        {
            if(accept_full && is_format_supported(type_flags, bgfx::TextureFormat::RG32F))
                return bgfx::TextureFormat::RG32F;
            if(accept_half && is_format_supported(type_flags, bgfx::TextureFormat::RG16F))
                return bgfx::TextureFormat::RG16F;
            if(accept_padding && accept_half && is_format_supported(type_flags, bgfx::TextureFormat::RGBA16F))
                return bgfx::TextureFormat::RGBA16F;
            if(accept_padding && accept_full && is_format_supported(type_flags, bgfx::TextureFormat::RGBA32F))
                return bgfx::TextureFormat::RGBA32F;
        }
        else
        {
            if(accept_padding && accept_half && is_format_supported(type_flags, bgfx::TextureFormat::RGBA16F))
                return bgfx::TextureFormat::RGBA16F;
            if(accept_padding && accept_full && is_format_supported(type_flags, bgfx::TextureFormat::RGBA32F))
                return bgfx::TextureFormat::RGBA32F;
        }
    }
    else if(search_flags & format_search_flags::one_channel)
    {
        if(!requires_alpha)
        {
            if(accept_full && is_format_supported(type_flags, bgfx::TextureFormat::R32F))
                return bgfx::TextureFormat::R32F;
            if(accept_half && is_format_supported(type_flags, bgfx::TextureFormat::R16F))
                return bgfx::TextureFormat::R16F;
            if(accept_padding && accept_half && is_format_supported(type_flags, bgfx::TextureFormat::RG16F))
                return bgfx::TextureFormat::RG16F;
            if(accept_padding && accept_full && is_format_supported(type_flags, bgfx::TextureFormat::RG32F))
                return bgfx::TextureFormat::RG32F;
            if(accept_padding && accept_half && is_format_supported(type_flags, bgfx::TextureFormat::RGBA16F))
                return bgfx::TextureFormat::RGBA16F;
            if(accept_padding && accept_full && is_format_supported(type_flags, bgfx::TextureFormat::RGBA32F))
                return bgfx::TextureFormat::RGBA32F;
        }
        else
        {
            if(accept_padding && accept_half && is_format_supported(type_flags, bgfx::TextureFormat::RGBA16F))
                return bgfx::TextureFormat::RGBA16F;
            if(accept_padding && accept_full && is_format_supported(type_flags, bgfx::TextureFormat::RGBA32F))
                return bgfx::TextureFormat::RGBA32F;
        }
    }

    return bgfx::TextureFormat::Unknown;
}

auto get_best_standard_format(std::uint16_t type_flags,
                              std::uint32_t search_flags,
                              bool requires_alpha,
                              bool accept_padding) -> bgfx::TextureFormat::Enum
{
    if(search_flags & format_search_flags::four_channels)
    {
        if(requires_alpha)
        {
            if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA8))
                return bgfx::TextureFormat::RGBA8;
            if(is_format_supported(type_flags, bgfx::TextureFormat::BGRA8))
                return bgfx::TextureFormat::BGRA8;
            if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA16))
                return bgfx::TextureFormat::RGBA16;
            if(is_format_supported(type_flags, bgfx::TextureFormat::RGB10A2))
                return bgfx::TextureFormat::RGB10A2;
            if(is_format_supported(type_flags, bgfx::TextureFormat::RGB5A1))
                return bgfx::TextureFormat::RGB5A1;
        }
        else
        {
            if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA8))
                return bgfx::TextureFormat::RGBA8;
            if(is_format_supported(type_flags, bgfx::TextureFormat::BGRA8))
                return bgfx::TextureFormat::BGRA8;
            if(is_format_supported(type_flags, bgfx::TextureFormat::RGB8))
                return bgfx::TextureFormat::RGB8;
            if(is_format_supported(type_flags, bgfx::TextureFormat::RGB10A2))
                return bgfx::TextureFormat::RGB10A2;
            if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA16))
                return bgfx::TextureFormat::RGBA16;
            if(is_format_supported(type_flags, bgfx::TextureFormat::R5G6B5))
                return bgfx::TextureFormat::R5G6B5;
            if(is_format_supported(type_flags, bgfx::TextureFormat::RGB5A1))
                return bgfx::TextureFormat::RGB5A1;
        }
    }
    else if(search_flags & format_search_flags::two_channels)
    {
        if(!requires_alpha)
        {
            if(is_format_supported(type_flags, bgfx::TextureFormat::RG16))
                return bgfx::TextureFormat::RG16;
            if(accept_padding)
            {
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGB8))
                    return bgfx::TextureFormat::RGB8;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA8))
                    return bgfx::TextureFormat::RGBA8;
                if(is_format_supported(type_flags, bgfx::TextureFormat::BGRA8))
                    return bgfx::TextureFormat::BGRA8;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGB10A2))
                    return bgfx::TextureFormat::RGB10A2;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA16))
                    return bgfx::TextureFormat::RGBA16;
                if(is_format_supported(type_flags, bgfx::TextureFormat::R5G6B5))
                    return bgfx::TextureFormat::R5G6B5;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGB5A1))
                    return bgfx::TextureFormat::RGB5A1;
            }
        }
        else
        {
            if(accept_padding)
            {
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA8))
                    return bgfx::TextureFormat::RGBA8;
                if(is_format_supported(type_flags, bgfx::TextureFormat::BGRA8))
                    return bgfx::TextureFormat::BGRA8;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA16))
                    return bgfx::TextureFormat::RGBA16;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGB10A2))
                    return bgfx::TextureFormat::RGB10A2;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGB5A1))
                    return bgfx::TextureFormat::RGB5A1;
            }
        }
    }
    else if(search_flags & format_search_flags::one_channel)
    {
        if(!requires_alpha)
        {
            if(is_format_supported(type_flags, bgfx::TextureFormat::R8))
                return bgfx::TextureFormat::R8;
            if(accept_padding)
            {
                if(is_format_supported(type_flags, bgfx::TextureFormat::RG16))
                    return bgfx::TextureFormat::RG16;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGB8))
                    return bgfx::TextureFormat::RGB8;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA8))
                    return bgfx::TextureFormat::RGBA8;
                if(is_format_supported(type_flags, bgfx::TextureFormat::BGRA8))
                    return bgfx::TextureFormat::BGRA8;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGB10A2))
                    return bgfx::TextureFormat::RGB10A2;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA16))
                    return bgfx::TextureFormat::RGBA16;
                if(is_format_supported(type_flags, bgfx::TextureFormat::R5G6B5))
                    return bgfx::TextureFormat::R5G6B5;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGB5A1))
                    return bgfx::TextureFormat::RGB5A1;
            }
        }
        else
        {
            if(is_format_supported(type_flags, bgfx::TextureFormat::A8))
                return bgfx::TextureFormat::A8;
            if(accept_padding)
            {
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA8))
                    return bgfx::TextureFormat::RGBA8;
                if(is_format_supported(type_flags, bgfx::TextureFormat::BGRA8))
                    return bgfx::TextureFormat::BGRA8;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGBA16))
                    return bgfx::TextureFormat::RGBA16;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGB10A2))
                    return bgfx::TextureFormat::RGB10A2;
                if(is_format_supported(type_flags, bgfx::TextureFormat::RGB5A1))
                    return bgfx::TextureFormat::RGB5A1;
            }
        }
    }

    return bgfx::TextureFormat::Unknown;
}

auto get_best_depth_format(std::uint16_t type_flags, std::uint32_t search_flags) -> bgfx::TextureFormat::Enum
{
    bool requires_stencil = (search_flags & format_search_flags::requires_stencil) != 0;
    bool accept_full = (search_flags & format_search_flags::full_precision_float) != 0;

    if(search_flags & format_search_flags::floating_point)
    {
        if(!requires_stencil)
        {
            if(accept_full && is_format_supported(type_flags, bgfx::TextureFormat::D32F))
                return bgfx::TextureFormat::D32F;
            if(accept_full && is_format_supported(type_flags, bgfx::TextureFormat::D24F))
                return bgfx::TextureFormat::D24F;
        }
    }
    else
    {
        if(!requires_stencil)
        {
            if(is_format_supported(type_flags, bgfx::TextureFormat::D32))
                return bgfx::TextureFormat::D32;
            if(is_format_supported(type_flags, bgfx::TextureFormat::D24))
                return bgfx::TextureFormat::D24;
            if(is_format_supported(type_flags, bgfx::TextureFormat::D16))
                return bgfx::TextureFormat::D16;
        }
        else
        {
            if(is_format_supported(type_flags, bgfx::TextureFormat::D24S8))
                return bgfx::TextureFormat::D24S8;
        }
    }

    return bgfx::TextureFormat::Unknown;
}

auto get_best_format(std::uint16_t type_flags, std::uint32_t search_flags) -> bgfx::TextureFormat::Enum
{
    bool is_depth = (search_flags & format_search_flags::requires_depth) != 0;
    bool requires_alpha = (search_flags & format_search_flags::requires_alpha) != 0;
    bool accept_padding = (search_flags & format_search_flags::allow_padding_channels) != 0;
    bool accept_half = (search_flags & format_search_flags::half_precision_float) != 0;
    bool accept_full = (search_flags & format_search_flags::full_precision_float) != 0;

    if(!is_depth)
    {
        if((search_flags & format_search_flags::prefer_compressed) &&
           (search_flags & format_search_flags::four_channels) && !(search_flags & format_search_flags::floating_point))
        {
            if(requires_alpha)
            {
                if(is_format_supported(type_flags, bgfx::TextureFormat::BC2))
                    return bgfx::TextureFormat::BC2;
                if(is_format_supported(type_flags, bgfx::TextureFormat::BC3))
                    return bgfx::TextureFormat::BC3;
            }
            else
            {
                if(is_format_supported(type_flags, bgfx::TextureFormat::BC1))
                    return bgfx::TextureFormat::BC1;
            }
        }

        if(search_flags & format_search_flags::floating_point)
        {
            return get_best_float_format(type_flags,
                                         search_flags,
                                         requires_alpha,
                                         accept_padding,
                                         accept_half,
                                         accept_full);
        }
        else
        {
            return get_best_standard_format(type_flags, search_flags, requires_alpha, accept_padding);
        }
    }
    else
    {
        return get_best_depth_format(type_flags, search_flags);
    }

    return bgfx::TextureFormat::Unknown;
}

auto get_default_rt_sampler_flags() -> uint64_t
{
    static std::uint64_t sampler_flags = 0 | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    return sampler_flags;
}

auto is_format_supported(uint16_t flags, bgfx::TextureFormat::Enum format) -> bool
{
    const std::uint32_t formatCaps = bgfx::getCaps()->formats[format];
    return 0 != (formatCaps & flags);
}

auto get_format_info(bgfx::TextureFormat::Enum fmt) -> format_details
{
    switch(fmt)
    {
        // --- Common BC formats ---
        case bgfx::TextureFormat::BC1: // DXT1
            // Typically 3 channels (RGB), can do 1-bit alpha, but we ignore that here.
            return {false, false, 3};
        case bgfx::TextureFormat::BC2: // DXT3
            // Explicit alpha
            return {true, false, 4};
        case bgfx::TextureFormat::BC3: // DXT5
            // Interpolated alpha
            return {true, false, 4};
        case bgfx::TextureFormat::BC4: // LATC1 / ATI1
            // Single channel
            return {false, false, 1};
        case bgfx::TextureFormat::BC5: // LATC2 / ATI2
            // Two-channel, often used for XY in normal maps
            return {false, false, 2};
        case bgfx::TextureFormat::BC6H:
            // HDR (RGB only), no alpha
            return {false, true, 3};
        case bgfx::TextureFormat::BC7:
            // More advanced block compression for RGBA
            return {true, false, 4};

            // --- Common uncompressed “RGBA” forms you might care about ---
        case bgfx::TextureFormat::RGBA8:
        case bgfx::TextureFormat::BGRA8:
        case bgfx::TextureFormat::RGBA8I:
        case bgfx::TextureFormat::RGBA8U:
        case bgfx::TextureFormat::RGBA8S:
        case bgfx::TextureFormat::RGBA16:
        case bgfx::TextureFormat::RGBA16I:
        case bgfx::TextureFormat::RGBA16U:
            // 4 channels, 8-16 bits. Not HDR by default unless floating.
            return {true, false, 4};

            // Float-based RGBA => can be HDR
        case bgfx::TextureFormat::RGBA16F:
        case bgfx::TextureFormat::RGBA32F:
            // 4 channels, floating point
            return {true, true, 4};

            // Similarly, if you commonly use some other uncompressed formats:
        case bgfx::TextureFormat::RGB8:
            return {false, false, 3};
        case bgfx::TextureFormat::R8:
        case bgfx::TextureFormat::R8I:
        case bgfx::TextureFormat::R8U:
        case bgfx::TextureFormat::R8S:
        case bgfx::TextureFormat::R16:
        case bgfx::TextureFormat::R16I:
        case bgfx::TextureFormat::R16U:
        case bgfx::TextureFormat::R16S:
        case bgfx::TextureFormat::R16F:
        case bgfx::TextureFormat::R32I:
        case bgfx::TextureFormat::R32U:
        case bgfx::TextureFormat::R32F:
            // Single channel, might or might not be HDR:
            // R16F / R32F is single-channel float => isHdr = true
            switch(fmt)
            {
                case bgfx::TextureFormat::R16F:
                case bgfx::TextureFormat::R32F:
                    return {false, true, 1};
                default:
                    return {false, false, 1};
            }

        case bgfx::TextureFormat::RG8:
        case bgfx::TextureFormat::RG8I:
        case bgfx::TextureFormat::RG8U:
        case bgfx::TextureFormat::RG8S:
        case bgfx::TextureFormat::RG16:
        case bgfx::TextureFormat::RG16I:
        case bgfx::TextureFormat::RG16U:
        case bgfx::TextureFormat::RG16S:
        case bgfx::TextureFormat::RG32I:
        case bgfx::TextureFormat::RG32U:
            // 2 channels, integer, not HDR
            return {false, false, 2};
        case bgfx::TextureFormat::RG16F:
        case bgfx::TextureFormat::RG32F:
            // 2 channels, float => HDR
            return {false, true, 2};

            // ... etc. Add more if needed for your typical usage.

        default:
            // For depth formats, rarely used/unknown formats, fallback.
            // Depth is definitely not alpha or typical color data:
            // Or if you're ignoring them, just do:
            return {false, false, 3}; // e.g., default to “no alpha, no HDR, 3 channels”
    }
}

auto is_compressed_format(bgfx::TextureFormat::Enum fmt) -> bool
{
    return fmt < bgfx::TextureFormat::Unknown;
}

auto normal_map_needs_z_reconstruction(bgfx::TextureFormat::Enum fmt) -> bool
{
    return get_format_info(fmt).num_channels == 2;
}

auto to_string(bgfx::TextureFormat::Enum fmt) -> std::string
{
    switch (fmt)
    {
        case bgfx::TextureFormat::BC1:       return "BC1";
        case bgfx::TextureFormat::BC2:       return "BC2";
        case bgfx::TextureFormat::BC3:       return "BC3";
        case bgfx::TextureFormat::BC4:       return "BC4";
        case bgfx::TextureFormat::BC5:       return "BC5";
        case bgfx::TextureFormat::BC6H:      return "BC6H";
        case bgfx::TextureFormat::BC7:       return "BC7";
        case bgfx::TextureFormat::ETC1:      return "ETC1";
        case bgfx::TextureFormat::ETC2:      return "ETC2";
        case bgfx::TextureFormat::ETC2A:     return "ETC2A";
        case bgfx::TextureFormat::ETC2A1:    return "ETC2A1";
        case bgfx::TextureFormat::PTC12:     return "PTC12";
        case bgfx::TextureFormat::PTC14:     return "PTC14";
        case bgfx::TextureFormat::PTC12A:    return "PTC12A";
        case bgfx::TextureFormat::PTC14A:    return "PTC14A";
        case bgfx::TextureFormat::PTC22:     return "PTC22";
        case bgfx::TextureFormat::PTC24:     return "PTC24";
        case bgfx::TextureFormat::ATC:       return "ATC";
        case bgfx::TextureFormat::ATCE:      return "ATCE";
        case bgfx::TextureFormat::ATCI:      return "ATCI";
        case bgfx::TextureFormat::ASTC4x4:   return "ASTC4x4";
        case bgfx::TextureFormat::ASTC5x4:   return "ASTC5x4";
        case bgfx::TextureFormat::ASTC5x5:   return "ASTC5x5";
        case bgfx::TextureFormat::ASTC6x5:   return "ASTC6x5";
        case bgfx::TextureFormat::ASTC6x6:   return "ASTC6x6";
        case bgfx::TextureFormat::ASTC8x5:   return "ASTC8x5";
        case bgfx::TextureFormat::ASTC8x6:   return "ASTC8x6";
        case bgfx::TextureFormat::ASTC8x8:   return "ASTC8x8";
        case bgfx::TextureFormat::ASTC10x5:  return "ASTC10x5";
        case bgfx::TextureFormat::ASTC10x6:  return "ASTC10x6";
        case bgfx::TextureFormat::ASTC10x8:  return "ASTC10x8";
        case bgfx::TextureFormat::ASTC10x10: return "ASTC10x10";
        case bgfx::TextureFormat::ASTC12x10: return "ASTC12x10";
        case bgfx::TextureFormat::ASTC12x12: return "ASTC12x12";

        case bgfx::TextureFormat::Unknown:    return "Unknown";

        case bgfx::TextureFormat::R1:        return "R1";
        case bgfx::TextureFormat::A8:        return "A8";
        case bgfx::TextureFormat::R8:        return "R8";
        case bgfx::TextureFormat::R8I:       return "R8I";
        case bgfx::TextureFormat::R8U:       return "R8U";
        case bgfx::TextureFormat::R8S:       return "R8S";
        case bgfx::TextureFormat::R16:       return "R16";
        case bgfx::TextureFormat::R16I:      return "R16I";
        case bgfx::TextureFormat::R16U:      return "R16U";
        case bgfx::TextureFormat::R16F:      return "R16F";
        case bgfx::TextureFormat::R16S:      return "R16S";
        case bgfx::TextureFormat::R32I:      return "R32I";
        case bgfx::TextureFormat::R32U:      return "R32U";
        case bgfx::TextureFormat::R32F:      return "R32F";
        case bgfx::TextureFormat::RG8:       return "RG8";
        case bgfx::TextureFormat::RG8I:      return "RG8I";
        case bgfx::TextureFormat::RG8U:      return "RG8U";
        case bgfx::TextureFormat::RG8S:      return "RG8S";
        case bgfx::TextureFormat::RG16:      return "RG16";
        case bgfx::TextureFormat::RG16I:     return "RG16I";
        case bgfx::TextureFormat::RG16U:     return "RG16U";
        case bgfx::TextureFormat::RG16F:     return "RG16F";
        case bgfx::TextureFormat::RG16S:     return "RG16S";
        case bgfx::TextureFormat::RG32I:     return "RG32I";
        case bgfx::TextureFormat::RG32U:     return "RG32U";
        case bgfx::TextureFormat::RG32F:     return "RG32F";
        case bgfx::TextureFormat::RGB8:      return "RGB8";
        case bgfx::TextureFormat::RGB8I:     return "RGB8I";
        case bgfx::TextureFormat::RGB8U:     return "RGB8U";
        case bgfx::TextureFormat::RGB8S:     return "RGB8S";
        case bgfx::TextureFormat::RGB9E5F:   return "RGB9E5F";
        case bgfx::TextureFormat::BGRA8:     return "BGRA8";
        case bgfx::TextureFormat::RGBA8:     return "RGBA8";
        case bgfx::TextureFormat::RGBA8I:    return "RGBA8I";
        case bgfx::TextureFormat::RGBA8U:    return "RGBA8U";
        case bgfx::TextureFormat::RGBA8S:    return "RGBA8S";
        case bgfx::TextureFormat::RGBA16:    return "RGBA16";
        case bgfx::TextureFormat::RGBA16I:   return "RGBA16I";
        case bgfx::TextureFormat::RGBA16U:   return "RGBA16U";
        case bgfx::TextureFormat::RGBA16F:   return "RGBA16F";
        case bgfx::TextureFormat::RGBA16S:   return "RGBA16S";
        case bgfx::TextureFormat::RGBA32I:   return "RGBA32I";
        case bgfx::TextureFormat::RGBA32U:   return "RGBA32U";
        case bgfx::TextureFormat::RGBA32F:   return "RGBA32F";
        case bgfx::TextureFormat::B5G6R5:    return "B5G6R5";
        case bgfx::TextureFormat::R5G6B5:    return "R5G6B5";
        case bgfx::TextureFormat::BGRA4:     return "BGRA4";
        case bgfx::TextureFormat::RGBA4:     return "RGBA4";
        case bgfx::TextureFormat::BGR5A1:    return "BGR5A1";
        case bgfx::TextureFormat::RGB5A1:    return "RGB5A1";
        case bgfx::TextureFormat::RGB10A2:   return "RGB10A2";
        case bgfx::TextureFormat::RG11B10F:  return "RG11B10F";

        case bgfx::TextureFormat::UnknownDepth: return "UnknownDepth";

        case bgfx::TextureFormat::D16:    return "D16";
        case bgfx::TextureFormat::D24:    return "D24";
        case bgfx::TextureFormat::D24S8:  return "D24S8";
        case bgfx::TextureFormat::D32:    return "D32";
        case bgfx::TextureFormat::D16F:   return "D16F";
        case bgfx::TextureFormat::D24F:   return "D24F";
        case bgfx::TextureFormat::D32F:   return "D32F";
        case bgfx::TextureFormat::D0S8:   return "D0S8";

        case bgfx::TextureFormat::Count:  return "Count";

        default:
            // If somehow it doesn't match any known enumerator
            return "Unknown";
    }
}

} // namespace gfx
