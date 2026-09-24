#include "texture.hpp"
#include "entt/core/fwd.hpp"
#include "graphics/texture.h"
#include "reflection/reflection.h"

namespace gfx
{

REFLECT(bgfx::TextureInfo)
{

    entt::meta_factory<bgfx::TextureFormat::Enum>{}
        .type("texture_format"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_format"},
            entt::attribute{"pretty_name", "Texture Format"},
        })
        .data<bgfx::TextureFormat::BC1>("BC1"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "BC1"},
            entt::attribute{"pretty_name", "BC1 (DXT1 R5G6B5A1)"},
        })
        .data<bgfx::TextureFormat::BC2>("BC2"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "BC2"},
            entt::attribute{"pretty_name", "BC2 (DXT3 R5G6B5A4)"},
        })
        .data<bgfx::TextureFormat::BC3>("BC3"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "BC3"},
            entt::attribute{"pretty_name", "BC3 (DXT5 R5G6B5A8)"},
        })
        .data<bgfx::TextureFormat::BC4>("BC4"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "BC4"},
            entt::attribute{"pretty_name", "BC4 (LATC1/ATI1 R8)"},
        })
        .data<bgfx::TextureFormat::BC5>("BC5"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "BC5"},
            entt::attribute{"pretty_name", "BC5 (LATC2/ATI2 RG8)"},
        })
        .data<bgfx::TextureFormat::BC6H>("BC6H"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "BC6H"},
            entt::attribute{"pretty_name", "BC6H (BC6H RGB16F)"},
        })
        .data<bgfx::TextureFormat::BC7>("BC7"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "BC7"},
            entt::attribute{"pretty_name", "BC7 (BC7 RGB 4-7 bits per color channel, 0-8 bits alpha)"},
        })
        .data<bgfx::TextureFormat::ETC1>("ETC1"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ETC1"},
            entt::attribute{"pretty_name", "ETC1 (ETC1 RGB8)"},
        })
        .data<bgfx::TextureFormat::ETC2>("ETC2"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ETC2"},
            entt::attribute{"pretty_name", "ETC2 (ETC2 RGB8)"},
        })
        .data<bgfx::TextureFormat::ETC2A>("ETC2A"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ETC2A"},
            entt::attribute{"pretty_name", "ETC2A (ETC2 RGBA8)"},
        })
        .data<bgfx::TextureFormat::ETC2A1>("ETC2A1"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ETC2A1"},
            entt::attribute{"pretty_name", "ETC2A1 (ETC2 RGB8A1)"},
        })
        .data<bgfx::TextureFormat::EACR11>("EACR11"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "EACR11"},
            entt::attribute{"pretty_name", "EACR11 (EAC R11 UNORM)"},
        })
        .data<bgfx::TextureFormat::EACR11S>("EACR11S"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "EACR11S"},
            entt::attribute{"pretty_name", "EACR11S (EAC R11 SNORM)"},
        })
        .data<bgfx::TextureFormat::EACRG11>("EACRG11"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "EACRG11"},
            entt::attribute{"pretty_name", "EACRG11 (EAC RG11 UNORM)"},
        })
        .data<bgfx::TextureFormat::EACRG11S>("EACRG11S"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "EACRG11S"},
            entt::attribute{"pretty_name", "EACRG11S (EAC RG11 SNORM)"},
        })
        .data<bgfx::TextureFormat::PTC12>("PTC12"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "PTC12"},
            entt::attribute{"pretty_name", "PTC12 (PVRTC1 RGB 2BPP)"},
        })
        .data<bgfx::TextureFormat::PTC14>("PTC14"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "PTC14"},
            entt::attribute{"pretty_name", "PTC14 (PVRTC1 RGB 4BPP)"},
        })
        .data<bgfx::TextureFormat::PTC12A>("PTC12A"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "PTC12A"},
            entt::attribute{"pretty_name", "PTC12A (PVRTC1 RGBA 2BPP)"},
        })
        .data<bgfx::TextureFormat::PTC14A>("PTC14A"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "PTC14A"},
            entt::attribute{"pretty_name", "PTC14A (PVRTC1 RGBA 4BPP)"},
        })
        .data<bgfx::TextureFormat::PTC22>("PTC22"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "PTC22"},
            entt::attribute{"pretty_name", "PTC22 (PVRTC2 RGBA 2BPP)"},
        })
        .data<bgfx::TextureFormat::PTC24>("PTC24"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "PTC24"},
            entt::attribute{"pretty_name", "PTC24 (PVRTC2 RGBA 4BPP)"},
        })
        .data<bgfx::TextureFormat::ATC>("ATC"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ATC"},
            entt::attribute{"pretty_name", "ATC (ATC RGB 4BPP)"},
        })
        .data<bgfx::TextureFormat::ATCE>("ATCE"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ATCE"},
            entt::attribute{"pretty_name", "ATCE (ATCE RGBA 8 BPP explicit alpha)"},
        })
        .data<bgfx::TextureFormat::ATCI>("ATCI"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ATCI"},
            entt::attribute{"pretty_name", "ATCI (ATCI RGBA 8 BPP interpolated alpha)"},
        })
        .data<bgfx::TextureFormat::ASTC4x4>("ASTC4x4"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC4x4"},
            entt::attribute{"pretty_name", "ASTC4x4 (ASTC 4x4 8.0 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC5x4>("ASTC5x4"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC5x4"},
            entt::attribute{"pretty_name", "ASTC5x4 (ASTC 5x4 6.40 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC5x5>("ASTC5x5"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC5x5"},
            entt::attribute{"pretty_name", "ASTC5x5 (ASTC 5x5 5.12 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC6x5>("ASTC6x5"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC6x5"},
            entt::attribute{"pretty_name", "ASTC6x5 (ASTC 6x5 4.27 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC6x6>("ASTC6x6"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC6x6"},
            entt::attribute{"pretty_name", "ASTC6x6 (ASTC 6x6 3.56 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC8x5>("ASTC8x5"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC8x5"},
            entt::attribute{"pretty_name", "ASTC8x5 (ASTC 8x5 3.20 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC8x6>("ASTC8x6"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC8x6"},
            entt::attribute{"pretty_name", "ASTC8x6 (ASTC 8x6 2.67 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC8x8>("ASTC8x8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC8x8"},
            entt::attribute{"pretty_name", "ASTC8x8 (ASTC 8x8 2.00 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC10x5>("ASTC10x5"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC10x5"},
            entt::attribute{"pretty_name", "ASTC10x5 (ASTC 10x5 2.56 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC10x6>("ASTC10x6"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC10x6"},
            entt::attribute{"pretty_name", "ASTC10x6 (ASTC 10x6 2.13 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC10x8>("ASTC10x8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC10x8"},
            entt::attribute{"pretty_name", "ASTC10x8 (ASTC 10x8 1.60 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC10x10>("ASTC10x10"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC10x10"},
            entt::attribute{"pretty_name", "ASTC10x10 (ASTC 10x10 1.28 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC12x10>("ASTC12x10"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC12x10"},
            entt::attribute{"pretty_name", "ASTC12x10 (ASTC 12x10 1.07 BPP)"},
        })
        .data<bgfx::TextureFormat::ASTC12x12>("ASTC12x12"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ASTC12x12"},
            entt::attribute{"pretty_name", "ASTC12x12 (ASTC 12x12 0.89 BPP)"},
        })
        .data<bgfx::TextureFormat::Unknown>("Unknown"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Unknown"},
            entt::attribute{"pretty_name", "Unknown"},
        })
        .data<bgfx::TextureFormat::R1>("R1"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R1"},
            entt::attribute{"pretty_name", "R1"},
        })
        .data<bgfx::TextureFormat::A8>("A8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "A8"},
            entt::attribute{"pretty_name", "A8"},
        })
        .data<bgfx::TextureFormat::R8>("R8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R8"},
            entt::attribute{"pretty_name", "R8"},
        })
        .data<bgfx::TextureFormat::R8I>("R8I"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R8I"},
            entt::attribute{"pretty_name", "R8I"},
        })
        .data<bgfx::TextureFormat::R8U>("R8U"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R8U"},
            entt::attribute{"pretty_name", "R8U"},
        })
        .data<bgfx::TextureFormat::R8S>("R8S"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R8S"},
            entt::attribute{"pretty_name", "R8S"},
        })
        .data<bgfx::TextureFormat::R16>("R16"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R16"},
            entt::attribute{"pretty_name", "R16"},
        })
        .data<bgfx::TextureFormat::R16I>("R16I"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R16I"},
            entt::attribute{"pretty_name", "R16I"},
        })
        .data<bgfx::TextureFormat::R16U>("R16U"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R16U"},
            entt::attribute{"pretty_name", "R16U"},
        })
        .data<bgfx::TextureFormat::R16F>("R16F"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R16F"},
            entt::attribute{"pretty_name", "R16F"},
        })
        .data<bgfx::TextureFormat::R16S>("R16S"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R16S"},
            entt::attribute{"pretty_name", "R16S"},
        })
        .data<bgfx::TextureFormat::R32I>("R32I"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R32I"},
            entt::attribute{"pretty_name", "R32I"},
        })
        .data<bgfx::TextureFormat::R32U>("R32U"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R32U"},
            entt::attribute{"pretty_name", "R32U"},
        })
        .data<bgfx::TextureFormat::R32F>("R32F"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R32F"},
            entt::attribute{"pretty_name", "R32F"},
        })
        .data<bgfx::TextureFormat::RG8>("RG8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG8"},
            entt::attribute{"pretty_name", "RG8"},
        })
        .data<bgfx::TextureFormat::RG8I>("RG8I"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG8I"},
            entt::attribute{"pretty_name", "RG8I"},
        })
        .data<bgfx::TextureFormat::RG8U>("RG8U"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG8U"},
            entt::attribute{"pretty_name", "RG8U"},
        })
        .data<bgfx::TextureFormat::RG8S>("RG8S"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG8S"},
            entt::attribute{"pretty_name", "RG8S"},
        })
        .data<bgfx::TextureFormat::RG16>("RG16"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG16"},
            entt::attribute{"pretty_name", "RG16"},
        })
        .data<bgfx::TextureFormat::RG16I>("RG16I"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG16I"},
            entt::attribute{"pretty_name", "RG16I"},
        })
        .data<bgfx::TextureFormat::RG16U>("RG16U"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG16U"},
            entt::attribute{"pretty_name", "RG16U"},
        })
        .data<bgfx::TextureFormat::RG16F>("RG16F"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG16F"},
            entt::attribute{"pretty_name", "RG16F"},
        })
        .data<bgfx::TextureFormat::RG16S>("RG16S"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG16S"},
            entt::attribute{"pretty_name", "RG16S"},
        })
        .data<bgfx::TextureFormat::RG32I>("RG32I"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG32I"},
            entt::attribute{"pretty_name", "RG32I"},
        })
        .data<bgfx::TextureFormat::RG32U>("RG32U"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG32U"},
            entt::attribute{"pretty_name", "RG32U"},
        })
        .data<bgfx::TextureFormat::RG32F>("RG32F"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG32F"},
            entt::attribute{"pretty_name", "RG32F"},
        })
        .data<bgfx::TextureFormat::RGB8>("RGB8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGB8"},
            entt::attribute{"pretty_name", "RGB8"},
        })
        .data<bgfx::TextureFormat::RGB8I>("RGB8I"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGB8I"},
            entt::attribute{"pretty_name", "RGB8I"},
        })
        .data<bgfx::TextureFormat::RGB8U>("RGB8U"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGB8U"},
            entt::attribute{"pretty_name", "RGB8U"},
        })
        .data<bgfx::TextureFormat::RGB8S>("RGB8S"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGB8S"},
            entt::attribute{"pretty_name", "RGB8S"},
        })
        .data<bgfx::TextureFormat::RGB9E5F>("RGB9E5F"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGB9E5F"},
            entt::attribute{"pretty_name", "RGB9E5F"},
        })
        .data<bgfx::TextureFormat::BGRA8>("BGRA8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "BGRA8"},
            entt::attribute{"pretty_name", "BGRA8"},
        })
        .data<bgfx::TextureFormat::RGBA8>("RGBA8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA8"},
            entt::attribute{"pretty_name", "RGBA8"},
        })
        .data<bgfx::TextureFormat::RGBA8I>("RGBA8I"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA8I"},
            entt::attribute{"pretty_name", "RGBA8I"},
        })
        .data<bgfx::TextureFormat::RGBA8U>("RGBA8U"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA8U"},
            entt::attribute{"pretty_name", "RGBA8U"},
        })
        .data<bgfx::TextureFormat::RGBA8S>("RGBA8S"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA8S"},
            entt::attribute{"pretty_name", "RGBA8S"},
        })
        .data<bgfx::TextureFormat::RGBA16>("RGBA16"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA16"},
            entt::attribute{"pretty_name", "RGBA16"},
        })
        .data<bgfx::TextureFormat::RGBA16I>("RGBA16I"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA16I"},
            entt::attribute{"pretty_name", "RGBA16I"},
        })
        .data<bgfx::TextureFormat::RGBA16U>("RGBA16U"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA16U"},
            entt::attribute{"pretty_name", "RGBA16U"},
        })
        .data<bgfx::TextureFormat::RGBA16F>("RGBA16F"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA16F"},
            entt::attribute{"pretty_name", "RGBA16F"},
        })
        .data<bgfx::TextureFormat::RGBA16S>("RGBA16S"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA16S"},
            entt::attribute{"pretty_name", "RGBA16S"},
        })
        .data<bgfx::TextureFormat::RGBA32I>("RGBA32I"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA32I"},
            entt::attribute{"pretty_name", "RGBA32I"},
        })
        .data<bgfx::TextureFormat::RGBA32U>("RGBA32U"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA32U"},
            entt::attribute{"pretty_name", "RGBA32U"},
        })
        .data<bgfx::TextureFormat::RGBA32F>("RGBA32F"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA32F"},
            entt::attribute{"pretty_name", "RGBA32F"},
        })
        .data<bgfx::TextureFormat::B5G6R5>("B5G6R5"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "B5G6R5"},
            entt::attribute{"pretty_name", "B5G6R5"},
        })
        .data<bgfx::TextureFormat::R5G6B5>("R5G6B5"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "R5G6B5"},
            entt::attribute{"pretty_name", "R5G6B5"},
        })
        .data<bgfx::TextureFormat::BGRA4>("BGRA4"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "BGRA4"},
            entt::attribute{"pretty_name", "BGRA4"},
        })
        .data<bgfx::TextureFormat::RGBA4>("RGBA4"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGBA4"},
            entt::attribute{"pretty_name", "RGBA4"},
        })
        .data<bgfx::TextureFormat::BGR5A1>("BGR5A1"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "BGR5A1"},
            entt::attribute{"pretty_name", "BGR5A1"},
        })
        .data<bgfx::TextureFormat::RGB5A1>("RGB5A1"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGB5A1"},
            entt::attribute{"pretty_name", "RGB5A1"},
        })
        .data<bgfx::TextureFormat::RGB10A2>("RGB10A2"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RGB10A2"},
            entt::attribute{"pretty_name", "RGB10A2"},
        })
        .data<bgfx::TextureFormat::RG11B10F>("RG11B10F"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RG11B10F"},
            entt::attribute{"pretty_name", "RG11B10F"},
        })
        .data<bgfx::TextureFormat::UnknownDepth>("UnknownDepth"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "UnknownDepth"},
            entt::attribute{"pretty_name", "UnknownDepth"},
        })
        .data<bgfx::TextureFormat::D16>("D16"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "D16"},
            entt::attribute{"pretty_name", "D16"},
        })
        .data<bgfx::TextureFormat::D24>("D24"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "D24"},
            entt::attribute{"pretty_name", "D24"},
        })
        .data<bgfx::TextureFormat::D24S8>("D24S8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "D24S8"},
            entt::attribute{"pretty_name", "D24S8"},
        })
        .data<bgfx::TextureFormat::D32>("D32"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "D32"},
            entt::attribute{"pretty_name", "D32"},
        })
        .data<bgfx::TextureFormat::D16F>("D16F"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "D16F"},
            entt::attribute{"pretty_name", "D16F"},
        })
        .data<bgfx::TextureFormat::D24F>("D24F"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "D24F"},
            entt::attribute{"pretty_name", "D24F"},
        })
        .data<bgfx::TextureFormat::D32F>("D32F"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "D32F"},
            entt::attribute{"pretty_name", "D32F"},
        })
        .data<bgfx::TextureFormat::D0S8>("D0S8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "D0S8"},
            entt::attribute{"pretty_name", "D0S8"},
        })
        .data<bgfx::TextureFormat::Count>("Count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Count"},
            entt::attribute{"pretty_name", "Count"},
        });

    entt::meta_factory<bgfx::TextureInfo>{}
        .type("texture_info"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_info"},
            entt::attribute{"pretty_name", "Texture Info"},
        })
        .data<nullptr, &bgfx::TextureInfo::format>("format"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "format"},
            entt::attribute{"pretty_name", "Format"} 
        })
        .data<nullptr, &bgfx::TextureInfo::storageSize>("storageSize"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "storageSize"},
            entt::attribute{"pretty_name", "Storage Size"},
            entt::attribute{"format", "size"},
            entt::attribute{"data_format", "B"},
        })
        .data<nullptr, &bgfx::TextureInfo::width>("width"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "width"},
            entt::attribute{"pretty_name", "Width"} 
        })
        .data<nullptr, &bgfx::TextureInfo::height>("height"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "height"},
            entt::attribute{"pretty_name", "Height"} 
        })
        .data<nullptr, &bgfx::TextureInfo::depth>("depth"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "depth"},
            entt::attribute{"pretty_name", "Depth"} 
        })
        .data<nullptr, &bgfx::TextureInfo::numMips>("numMips"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "numMips"},
            entt::attribute{"pretty_name", "Mips"} 
        })
        .data<nullptr, &bgfx::TextureInfo::bitsPerPixel>("bitsPerPixel"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "bitsPerPixel"},
            entt::attribute{"pretty_name", "Bits Per Pixel"} 
        })
        .data<nullptr, &bgfx::TextureInfo::cubeMap>("cubeMap"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "cubeMap"},
            entt::attribute{"pretty_name", "Cubemap"} 
        });


        entt::meta_factory<texture>{}
        .type("texture"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture"},
            entt::attribute{"pretty_name", "Texture"},
        })
        .data<nullptr, &texture::info>("info"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "info"},
            entt::attribute{"pretty_name", "Info"},
            entt::attribute{"flattable", true}
        })
        .data<nullptr, &texture::get_estimated_gpu_size>("estimated_gpu_size"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "estimated_gpu_size"},
            entt::attribute{"pretty_name", "Estimated GPU Size"},
            entt::attribute{"format", "size"},
            entt::attribute{"data_format", "B"},
            entt::attribute{"flattable", true}
        });
}
} // namespace gfx
