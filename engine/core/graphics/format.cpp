#include "format.h"

namespace gfx
{

auto get_best_float_format(std::uint16_t type_flags,
                           std::uint32_t search_flags,
                           bool requires_alpha,
                           bool accept_padding,
                           bool accept_half,
                           bool accept_full) -> texture_format
{
    if(search_flags & format_search_flags::four_channels)
    {
        if(accept_full && is_format_supported(type_flags, texture_format::RGBA32F))
            return texture_format::RGBA32F;
        if(accept_half && is_format_supported(type_flags, texture_format::RGBA16F))
            return texture_format::RGBA16F;
    }
    else if(search_flags & format_search_flags::two_channels)
    {
        if(!requires_alpha)
        {
            if(accept_full && is_format_supported(type_flags, texture_format::RG32F))
                return texture_format::RG32F;
            if(accept_half && is_format_supported(type_flags, texture_format::RG16F))
                return texture_format::RG16F;
            if(accept_padding && accept_half && is_format_supported(type_flags, texture_format::RGBA16F))
                return texture_format::RGBA16F;
            if(accept_padding && accept_full && is_format_supported(type_flags, texture_format::RGBA32F))
                return texture_format::RGBA32F;
        }
        else
        {
            if(accept_padding && accept_half && is_format_supported(type_flags, texture_format::RGBA16F))
                return texture_format::RGBA16F;
            if(accept_padding && accept_full && is_format_supported(type_flags, texture_format::RGBA32F))
                return texture_format::RGBA32F;
        }
    }
    else if(search_flags & format_search_flags::one_channel)
    {
        if(!requires_alpha)
        {
            if(accept_full && is_format_supported(type_flags, texture_format::R32F))
                return texture_format::R32F;
            if(accept_half && is_format_supported(type_flags, texture_format::R16F))
                return texture_format::R16F;
            if(accept_padding && accept_half && is_format_supported(type_flags, texture_format::RG16F))
                return texture_format::RG16F;
            if(accept_padding && accept_full && is_format_supported(type_flags, texture_format::RG32F))
                return texture_format::RG32F;
            if(accept_padding && accept_half && is_format_supported(type_flags, texture_format::RGBA16F))
                return texture_format::RGBA16F;
            if(accept_padding && accept_full && is_format_supported(type_flags, texture_format::RGBA32F))
                return texture_format::RGBA32F;
        }
        else
        {
            if(accept_padding && accept_half && is_format_supported(type_flags, texture_format::RGBA16F))
                return texture_format::RGBA16F;
            if(accept_padding && accept_full && is_format_supported(type_flags, texture_format::RGBA32F))
                return texture_format::RGBA32F;
        }
    }

    return texture_format::Unknown;
}

auto get_best_standard_format(std::uint16_t type_flags,
                              std::uint32_t search_flags,
                              bool requires_alpha,
                              bool accept_padding) -> texture_format
{
    if(search_flags & format_search_flags::four_channels)
    {
        if(requires_alpha)
        {
            if(is_format_supported(type_flags, texture_format::RGBA8))
                return texture_format::RGBA8;
            if(is_format_supported(type_flags, texture_format::BGRA8))
                return texture_format::BGRA8;
            if(is_format_supported(type_flags, texture_format::RGBA16))
                return texture_format::RGBA16;
            if(is_format_supported(type_flags, texture_format::RGB10A2))
                return texture_format::RGB10A2;
            if(is_format_supported(type_flags, texture_format::RGB5A1))
                return texture_format::RGB5A1;
        }
        else
        {
            if(is_format_supported(type_flags, texture_format::RGBA8))
                return texture_format::RGBA8;
            if(is_format_supported(type_flags, texture_format::BGRA8))
                return texture_format::BGRA8;
            if(is_format_supported(type_flags, texture_format::RGB8))
                return texture_format::RGB8;
            if(is_format_supported(type_flags, texture_format::RGB10A2))
                return texture_format::RGB10A2;
            if(is_format_supported(type_flags, texture_format::RGBA16))
                return texture_format::RGBA16;
            if(is_format_supported(type_flags, texture_format::R5G6B5))
                return texture_format::R5G6B5;
            if(is_format_supported(type_flags, texture_format::RGB5A1))
                return texture_format::RGB5A1;
        }
    }
    else if(search_flags & format_search_flags::two_channels)
    {
        if(!requires_alpha)
        {
            if(is_format_supported(type_flags, texture_format::RG16))
                return texture_format::RG16;
            if(accept_padding)
            {
                if(is_format_supported(type_flags, texture_format::RGB8))
                    return texture_format::RGB8;
                if(is_format_supported(type_flags, texture_format::RGBA8))
                    return texture_format::RGBA8;
                if(is_format_supported(type_flags, texture_format::BGRA8))
                    return texture_format::BGRA8;
                if(is_format_supported(type_flags, texture_format::RGB10A2))
                    return texture_format::RGB10A2;
                if(is_format_supported(type_flags, texture_format::RGBA16))
                    return texture_format::RGBA16;
                if(is_format_supported(type_flags, texture_format::R5G6B5))
                    return texture_format::R5G6B5;
                if(is_format_supported(type_flags, texture_format::RGB5A1))
                    return texture_format::RGB5A1;
            }
        }
        else
        {
            if(accept_padding)
            {
                if(is_format_supported(type_flags, texture_format::RGBA8))
                    return texture_format::RGBA8;
                if(is_format_supported(type_flags, texture_format::BGRA8))
                    return texture_format::BGRA8;
                if(is_format_supported(type_flags, texture_format::RGBA16))
                    return texture_format::RGBA16;
                if(is_format_supported(type_flags, texture_format::RGB10A2))
                    return texture_format::RGB10A2;
                if(is_format_supported(type_flags, texture_format::RGB5A1))
                    return texture_format::RGB5A1;
            }
        }
    }
    else if(search_flags & format_search_flags::one_channel)
    {
        if(!requires_alpha)
        {
            if(is_format_supported(type_flags, texture_format::R8))
                return texture_format::R8;
            if(accept_padding)
            {
                if(is_format_supported(type_flags, texture_format::RG16))
                    return texture_format::RG16;
                if(is_format_supported(type_flags, texture_format::RGB8))
                    return texture_format::RGB8;
                if(is_format_supported(type_flags, texture_format::RGBA8))
                    return texture_format::RGBA8;
                if(is_format_supported(type_flags, texture_format::BGRA8))
                    return texture_format::BGRA8;
                if(is_format_supported(type_flags, texture_format::RGB10A2))
                    return texture_format::RGB10A2;
                if(is_format_supported(type_flags, texture_format::RGBA16))
                    return texture_format::RGBA16;
                if(is_format_supported(type_flags, texture_format::R5G6B5))
                    return texture_format::R5G6B5;
                if(is_format_supported(type_flags, texture_format::RGB5A1))
                    return texture_format::RGB5A1;
            }
        }
        else
        {
            if(is_format_supported(type_flags, texture_format::A8))
                return texture_format::A8;
            if(accept_padding)
            {
                if(is_format_supported(type_flags, texture_format::RGBA8))
                    return texture_format::RGBA8;
                if(is_format_supported(type_flags, texture_format::BGRA8))
                    return texture_format::BGRA8;
                if(is_format_supported(type_flags, texture_format::RGBA16))
                    return texture_format::RGBA16;
                if(is_format_supported(type_flags, texture_format::RGB10A2))
                    return texture_format::RGB10A2;
                if(is_format_supported(type_flags, texture_format::RGB5A1))
                    return texture_format::RGB5A1;
            }
        }
    }

    return texture_format::Unknown;
}

auto get_best_depth_format(std::uint16_t type_flags, std::uint32_t search_flags) -> texture_format
{
    bool requires_stencil = (search_flags & format_search_flags::requires_stencil) != 0;
    bool accept_full = (search_flags & format_search_flags::full_precision_float) != 0;

    if(search_flags & format_search_flags::floating_point)
    {
        if(!requires_stencil)
        {
            if(accept_full && is_format_supported(type_flags, texture_format::D32F))
                return texture_format::D32F;
            if(accept_full && is_format_supported(type_flags, texture_format::D24F))
                return texture_format::D24F;
        }
    }
    else
    {
        if(!requires_stencil)
        {
            if(is_format_supported(type_flags, texture_format::D32))
                return texture_format::D32;
            if(is_format_supported(type_flags, texture_format::D24))
                return texture_format::D24;
            if(is_format_supported(type_flags, texture_format::D16))
                return texture_format::D16;
        }
        else
        {
            if(is_format_supported(type_flags, texture_format::D24S8))
                return texture_format::D24S8;
        }
    }

    return texture_format::Unknown;
}

auto get_best_format(std::uint16_t type_flags, std::uint32_t search_flags) -> texture_format
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
                if(is_format_supported(type_flags, texture_format::BC2))
                    return texture_format::BC2;
                if(is_format_supported(type_flags, texture_format::BC3))
                    return texture_format::BC3;
            }
            else
            {
                if(is_format_supported(type_flags, texture_format::BC1))
                    return texture_format::BC1;
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

    return texture_format::Unknown;
}

auto get_default_rt_sampler_flags() -> uint64_t
{
    static std::uint64_t sampler_flags = 0 | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    return sampler_flags;
}

auto is_format_supported(uint16_t flags, texture_format format) -> bool
{
    const std::uint32_t formatCaps = bgfx::getCaps()->formats[format];
    return 0 != (formatCaps & flags);
}

auto get_format_info(texture_format fmt) -> format_details
{
    switch(fmt)
    {
        // --- Common BC formats ---
        case texture_format::BC1: // DXT1
            // Typically 3 channels (RGB), can do 1-bit alpha, but we ignore that here.
            return {false, false, 3};
        case texture_format::BC2: // DXT3
            // Explicit alpha
            return {true, false, 4};
        case texture_format::BC3: // DXT5
            // Interpolated alpha
            return {true, false, 4};
        case texture_format::BC4: // LATC1 / ATI1
            // Single channel
            return {false, false, 1};
        case texture_format::BC5: // LATC2 / ATI2
            // Two-channel, often used for XY in normal maps
            return {false, false, 2};
        case texture_format::BC6H:
            // HDR (RGB only), no alpha
            return {false, true, 3};
        case texture_format::BC7:
            // More advanced block compression for RGBA
            return {true, false, 4};

            // --- Common uncompressed “RGBA” forms you might care about ---
        case texture_format::RGBA8:
        case texture_format::BGRA8:
        case texture_format::RGBA8I:
        case texture_format::RGBA8U:
        case texture_format::RGBA8S:
        case texture_format::RGBA16:
        case texture_format::RGBA16I:
        case texture_format::RGBA16U:
            // 4 channels, 8-16 bits. Not HDR by default unless floating.
            return {true, false, 4};

            // Float-based RGBA => can be HDR
        case texture_format::RGBA16F:
        case texture_format::RGBA32F:
            // 4 channels, floating point
            return {true, true, 4};

            // Similarly, if you commonly use some other uncompressed formats:
        case texture_format::RGB8:
            return {false, false, 3};
        case texture_format::R8:
        case texture_format::R16:
        case texture_format::R16F:
        case texture_format::R32F:
            // Single channel, might or might not be HDR:
            // R16F / R32F is single-channel float => isHdr = true
            switch(fmt)
            {
                case texture_format::R16F:
                case texture_format::R32F:
                    return {false, true, 1};
                default:
                    return {false, false, 1};
            }

        case texture_format::RG8:
        case texture_format::RG16:
            // 2 channels, integer, not HDR
            return {false, false, 2};
        case texture_format::RG16F:
        case texture_format::RG32F:
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

auto to_string(texture_format fmt) -> std::string
{
    switch (fmt)
    {
        case texture_format::BC1:       return "BC1";
        case texture_format::BC2:       return "BC2";
        case texture_format::BC3:       return "BC3";
        case texture_format::BC4:       return "BC4";
        case texture_format::BC5:       return "BC5";
        case texture_format::BC6H:      return "BC6H";
        case texture_format::BC7:       return "BC7";
        case texture_format::ETC1:      return "ETC1";
        case texture_format::ETC2:      return "ETC2";
        case texture_format::ETC2A:     return "ETC2A";
        case texture_format::ETC2A1:    return "ETC2A1";
        case texture_format::PTC12:     return "PTC12";
        case texture_format::PTC14:     return "PTC14";
        case texture_format::PTC12A:    return "PTC12A";
        case texture_format::PTC14A:    return "PTC14A";
        case texture_format::PTC22:     return "PTC22";
        case texture_format::PTC24:     return "PTC24";
        case texture_format::ATC:       return "ATC";
        case texture_format::ATCE:      return "ATCE";
        case texture_format::ATCI:      return "ATCI";
        case texture_format::ASTC4x4:   return "ASTC4x4";
        case texture_format::ASTC5x4:   return "ASTC5x4";
        case texture_format::ASTC5x5:   return "ASTC5x5";
        case texture_format::ASTC6x5:   return "ASTC6x5";
        case texture_format::ASTC6x6:   return "ASTC6x6";
        case texture_format::ASTC8x5:   return "ASTC8x5";
        case texture_format::ASTC8x6:   return "ASTC8x6";
        case texture_format::ASTC8x8:   return "ASTC8x8";
        case texture_format::ASTC10x5:  return "ASTC10x5";
        case texture_format::ASTC10x6:  return "ASTC10x6";
        case texture_format::ASTC10x8:  return "ASTC10x8";
        case texture_format::ASTC10x10: return "ASTC10x10";
        case texture_format::ASTC12x10: return "ASTC12x10";
        case texture_format::ASTC12x12: return "ASTC12x12";

        case texture_format::Unknown:    return "Unknown";

        case texture_format::R1:        return "R1";
        case texture_format::A8:        return "A8";
        case texture_format::R8:        return "R8";
        case texture_format::R8I:       return "R8I";
        case texture_format::R8U:       return "R8U";
        case texture_format::R8S:       return "R8S";
        case texture_format::R16:       return "R16";
        case texture_format::R16I:      return "R16I";
        case texture_format::R16U:      return "R16U";
        case texture_format::R16F:      return "R16F";
        case texture_format::R16S:      return "R16S";
        case texture_format::R32I:      return "R32I";
        case texture_format::R32U:      return "R32U";
        case texture_format::R32F:      return "R32F";
        case texture_format::RG8:       return "RG8";
        case texture_format::RG8I:      return "RG8I";
        case texture_format::RG8U:      return "RG8U";
        case texture_format::RG8S:      return "RG8S";
        case texture_format::RG16:      return "RG16";
        case texture_format::RG16I:     return "RG16I";
        case texture_format::RG16U:     return "RG16U";
        case texture_format::RG16F:     return "RG16F";
        case texture_format::RG16S:     return "RG16S";
        case texture_format::RG32I:     return "RG32I";
        case texture_format::RG32U:     return "RG32U";
        case texture_format::RG32F:     return "RG32F";
        case texture_format::RGB8:      return "RGB8";
        case texture_format::RGB8I:     return "RGB8I";
        case texture_format::RGB8U:     return "RGB8U";
        case texture_format::RGB8S:     return "RGB8S";
        case texture_format::RGB9E5F:   return "RGB9E5F";
        case texture_format::BGRA8:     return "BGRA8";
        case texture_format::RGBA8:     return "RGBA8";
        case texture_format::RGBA8I:    return "RGBA8I";
        case texture_format::RGBA8U:    return "RGBA8U";
        case texture_format::RGBA8S:    return "RGBA8S";
        case texture_format::RGBA16:    return "RGBA16";
        case texture_format::RGBA16I:   return "RGBA16I";
        case texture_format::RGBA16U:   return "RGBA16U";
        case texture_format::RGBA16F:   return "RGBA16F";
        case texture_format::RGBA16S:   return "RGBA16S";
        case texture_format::RGBA32I:   return "RGBA32I";
        case texture_format::RGBA32U:   return "RGBA32U";
        case texture_format::RGBA32F:   return "RGBA32F";
        case texture_format::B5G6R5:    return "B5G6R5";
        case texture_format::R5G6B5:    return "R5G6B5";
        case texture_format::BGRA4:     return "BGRA4";
        case texture_format::RGBA4:     return "RGBA4";
        case texture_format::BGR5A1:    return "BGR5A1";
        case texture_format::RGB5A1:    return "RGB5A1";
        case texture_format::RGB10A2:   return "RGB10A2";
        case texture_format::RG11B10F:  return "RG11B10F";

        case texture_format::UnknownDepth: return "UnknownDepth";

        case texture_format::D16:    return "D16";
        case texture_format::D24:    return "D24";
        case texture_format::D24S8:  return "D24S8";
        case texture_format::D32:    return "D32";
        case texture_format::D16F:   return "D16F";
        case texture_format::D24F:   return "D24F";
        case texture_format::D32F:   return "D32F";
        case texture_format::D0S8:   return "D0S8";

        case texture_format::Count:  return "Count";

        default:
            // If somehow it doesn't match any known enumerator
            return "Unknown";
    }
}

} // namespace gfx
