#include "texture.hpp"

namespace gfx
{

REFLECT(texture_info)
{
    rttr::registration::enumeration<texture_format>("texture_format")(
        // Compressed formats (with comments in parentheses):
        rttr::value("BC1 (DXT1 R5G6B5A1)", texture_format::BC1),
        rttr::value("BC2 (DXT3 R5G6B5A4)", texture_format::BC2),
        rttr::value("BC3 (DXT5 R5G6B5A8)", texture_format::BC3),
        rttr::value("BC4 (LATC1/ATI1 R8)", texture_format::BC4),
        rttr::value("BC5 (LATC2/ATI2 RG8)", texture_format::BC5),
        rttr::value("BC6H (BC6H RGB16F)", texture_format::BC6H),
        rttr::value("BC7 (BC7 RGB 4-7 bits per color channel, 0-8 bits alpha)", texture_format::BC7),
        rttr::value("ETC1 (ETC1 RGB8)", texture_format::ETC1),
        rttr::value("ETC2 (ETC2 RGB8)", texture_format::ETC2),
        rttr::value("ETC2A (ETC2 RGBA8)", texture_format::ETC2A),
        rttr::value("ETC2A1 (ETC2 RGB8A1)", texture_format::ETC2A1),
        rttr::value("PTC12 (PVRTC1 RGB 2BPP)", texture_format::PTC12),
        rttr::value("PTC14 (PVRTC1 RGB 4BPP)", texture_format::PTC14),
        rttr::value("PTC12A (PVRTC1 RGBA 2BPP)", texture_format::PTC12A),
        rttr::value("PTC14A (PVRTC1 RGBA 4BPP)", texture_format::PTC14A),
        rttr::value("PTC22 (PVRTC2 RGBA 2BPP)", texture_format::PTC22),
        rttr::value("PTC24 (PVRTC2 RGBA 4BPP)", texture_format::PTC24),
        rttr::value("ATC (ATC RGB 4BPP)", texture_format::ATC),
        rttr::value("ATCE (ATCE RGBA 8 BPP explicit alpha)", texture_format::ATCE),
        rttr::value("ATCI (ATCI RGBA 8 BPP interpolated alpha)", texture_format::ATCI),
        rttr::value("ASTC4x4 (ASTC 4x4 8.0 BPP)", texture_format::ASTC4x4),
        rttr::value("ASTC5x4 (ASTC 5x4 6.40 BPP)", texture_format::ASTC5x4),
        rttr::value("ASTC5x5 (ASTC 5x5 5.12 BPP)", texture_format::ASTC5x5),
        rttr::value("ASTC6x5 (ASTC 6x5 4.27 BPP)", texture_format::ASTC6x5),
        rttr::value("ASTC6x6 (ASTC 6x6 3.56 BPP)", texture_format::ASTC6x6),
        rttr::value("ASTC8x5 (ASTC 8x5 3.20 BPP)", texture_format::ASTC8x5),
        rttr::value("ASTC8x6 (ASTC 8x6 2.67 BPP)", texture_format::ASTC8x6),
        rttr::value("ASTC8x8 (ASTC 8x8 2.00 BPP)", texture_format::ASTC8x8),
        rttr::value("ASTC10x5 (ASTC 10x5 2.56 BPP)", texture_format::ASTC10x5),
        rttr::value("ASTC10x6 (ASTC 10x6 2.13 BPP)", texture_format::ASTC10x6),
        rttr::value("ASTC10x8 (ASTC 10x8 1.60 BPP)", texture_format::ASTC10x8),
        rttr::value("ASTC10x10 (ASTC 10x10 1.28 BPP)", texture_format::ASTC10x10),
        rttr::value("ASTC12x10 (ASTC 12x10 1.07 BPP)", texture_format::ASTC12x10),
        rttr::value("ASTC12x12 (ASTC 12x12 0.89 BPP)", texture_format::ASTC12x12),

        rttr::value("Unknown", texture_format::Unknown),

        // Uncompressed formats (no original comments, so just names):
        rttr::value("R1", texture_format::R1),
        rttr::value("A8", texture_format::A8),
        rttr::value("R8", texture_format::R8),
        rttr::value("R8I", texture_format::R8I),
        rttr::value("R8U", texture_format::R8U),
        rttr::value("R8S", texture_format::R8S),
        rttr::value("R16", texture_format::R16),
        rttr::value("R16I", texture_format::R16I),
        rttr::value("R16U", texture_format::R16U),
        rttr::value("R16F", texture_format::R16F),
        rttr::value("R16S", texture_format::R16S),
        rttr::value("R32I", texture_format::R32I),
        rttr::value("R32U", texture_format::R32U),
        rttr::value("R32F", texture_format::R32F),
        rttr::value("RG8", texture_format::RG8),
        rttr::value("RG8I", texture_format::RG8I),
        rttr::value("RG8U", texture_format::RG8U),
        rttr::value("RG8S", texture_format::RG8S),
        rttr::value("RG16", texture_format::RG16),
        rttr::value("RG16I", texture_format::RG16I),
        rttr::value("RG16U", texture_format::RG16U),
        rttr::value("RG16F", texture_format::RG16F),
        rttr::value("RG16S", texture_format::RG16S),
        rttr::value("RG32I", texture_format::RG32I),
        rttr::value("RG32U", texture_format::RG32U),
        rttr::value("RG32F", texture_format::RG32F),
        rttr::value("RGB8", texture_format::RGB8),
        rttr::value("RGB8I", texture_format::RGB8I),
        rttr::value("RGB8U", texture_format::RGB8U),
        rttr::value("RGB8S", texture_format::RGB8S),
        rttr::value("RGB9E5F", texture_format::RGB9E5F),
        rttr::value("BGRA8", texture_format::BGRA8),
        rttr::value("RGBA8", texture_format::RGBA8),
        rttr::value("RGBA8I", texture_format::RGBA8I),
        rttr::value("RGBA8U", texture_format::RGBA8U),
        rttr::value("RGBA8S", texture_format::RGBA8S),
        rttr::value("RGBA16", texture_format::RGBA16),
        rttr::value("RGBA16I", texture_format::RGBA16I),
        rttr::value("RGBA16U", texture_format::RGBA16U),
        rttr::value("RGBA16F", texture_format::RGBA16F),
        rttr::value("RGBA16S", texture_format::RGBA16S),
        rttr::value("RGBA32I", texture_format::RGBA32I),
        rttr::value("RGBA32U", texture_format::RGBA32U),
        rttr::value("RGBA32F", texture_format::RGBA32F),
        rttr::value("B5G6R5", texture_format::B5G6R5),
        rttr::value("R5G6B5", texture_format::R5G6B5),
        rttr::value("BGRA4", texture_format::BGRA4),
        rttr::value("RGBA4", texture_format::RGBA4),
        rttr::value("BGR5A1", texture_format::BGR5A1),
        rttr::value("RGB5A1", texture_format::RGB5A1),
        rttr::value("RGB10A2", texture_format::RGB10A2),
        rttr::value("RG11B10F", texture_format::RG11B10F),

        // Depth formats (no explicit comments provided, so just names):
        rttr::value("UnknownDepth", texture_format::UnknownDepth),
        rttr::value("D16", texture_format::D16),
        rttr::value("D24", texture_format::D24),
        rttr::value("D24S8", texture_format::D24S8),
        rttr::value("D32", texture_format::D32),
        rttr::value("D16F", texture_format::D16F),
        rttr::value("D24F", texture_format::D24F),
        rttr::value("D32F", texture_format::D32F),
        rttr::value("D0S8", texture_format::D0S8),

        rttr::value("Count", texture_format::Count));

    rttr::registration::class_<texture_info>("texture_info")
        .property_readonly("format", &texture_info::format)(rttr::metadata("pretty_name", "Format"))
        .property_readonly("storageSize", &texture_info::storageSize)(rttr::metadata("pretty_name", "Storage Size"),
                                                                      rttr::metadata("format", "size"),
                                                                      rttr::metadata("data_format", "B"))
        .property_readonly("width", &texture_info::width)(rttr::metadata("pretty_name", "Width"))
        .property_readonly("height", &texture_info::height)(rttr::metadata("pretty_name", "Height"))
        .property_readonly("depth", &texture_info::depth)(rttr::metadata("pretty_name", "Depth"))
        .property_readonly("numMips", &texture_info::numMips)(rttr::metadata("pretty_name", "Mips"))
        .property_readonly("bitsPerPixel", &texture_info::bitsPerPixel)(rttr::metadata("pretty_name", "Bits Per Pixel"))
        .property_readonly("cubeMap", &texture_info::cubeMap)(rttr::metadata("pretty_name", "Cubemap"));

    // EnTT meta registration mirroring RTTR
    entt::meta_factory<texture_format>{}
        .type("texture_format"_hs)
        .data<texture_format::BC1>("BC1"_hs)
        .data<texture_format::BC2>("BC2"_hs)
        .data<texture_format::BC3>("BC3"_hs)
        .data<texture_format::BC4>("BC4"_hs)
        .data<texture_format::BC5>("BC5"_hs)
        .data<texture_format::BC6H>("BC6H"_hs)
        .data<texture_format::BC7>("BC7"_hs)
        .data<texture_format::ETC1>("ETC1"_hs)
        .data<texture_format::ETC2>("ETC2"_hs)
        .data<texture_format::ETC2A>("ETC2A"_hs)
        .data<texture_format::ETC2A1>("ETC2A1"_hs)
        .data<texture_format::PTC12>("PTC12"_hs)
        .data<texture_format::PTC14>("PTC14"_hs)
        .data<texture_format::PTC12A>("PTC12A"_hs)
        .data<texture_format::PTC14A>("PTC14A"_hs)
        .data<texture_format::PTC22>("PTC22"_hs)
        .data<texture_format::PTC24>("PTC24"_hs)
        .data<texture_format::ATC>("ATC"_hs)
        .data<texture_format::ATCE>("ATCE"_hs)
        .data<texture_format::ATCI>("ATCI"_hs)
        .data<texture_format::ASTC4x4>("ASTC4x4"_hs)
        .data<texture_format::ASTC5x4>("ASTC5x4"_hs)
        .data<texture_format::ASTC5x5>("ASTC5x5"_hs)
        .data<texture_format::ASTC6x5>("ASTC6x5"_hs)
        .data<texture_format::ASTC6x6>("ASTC6x6"_hs)
        .data<texture_format::ASTC8x5>("ASTC8x5"_hs)
        .data<texture_format::ASTC8x6>("ASTC8x6"_hs)
        .data<texture_format::ASTC8x8>("ASTC8x8"_hs)
        .data<texture_format::ASTC10x5>("ASTC10x5"_hs)
        .data<texture_format::ASTC10x6>("ASTC10x6"_hs)
        .data<texture_format::ASTC10x8>("ASTC10x8"_hs)
        .data<texture_format::ASTC10x10>("ASTC10x10"_hs)
        .data<texture_format::ASTC12x10>("ASTC12x10"_hs)
        .data<texture_format::ASTC12x12>("ASTC12x12"_hs)
        .data<texture_format::Unknown>("Unknown"_hs)
        .data<texture_format::R1>("R1"_hs)
        .data<texture_format::A8>("A8"_hs)
        .data<texture_format::R8>("R8"_hs)
        .data<texture_format::R8I>("R8I"_hs)
        .data<texture_format::R8U>("R8U"_hs)
        .data<texture_format::R8S>("R8S"_hs)
        .data<texture_format::R16>("R16"_hs)
        .data<texture_format::R16I>("R16I"_hs)
        .data<texture_format::R16U>("R16U"_hs)
        .data<texture_format::R16F>("R16F"_hs)
        .data<texture_format::R16S>("R16S"_hs)
        .data<texture_format::R32I>("R32I"_hs)
        .data<texture_format::R32U>("R32U"_hs)
        .data<texture_format::R32F>("R32F"_hs)
        .data<texture_format::RG8>("RG8"_hs)
        .data<texture_format::RG8I>("RG8I"_hs)
        .data<texture_format::RG8U>("RG8U"_hs)
        .data<texture_format::RG8S>("RG8S"_hs)
        .data<texture_format::RG16>("RG16"_hs)
        .data<texture_format::RG16I>("RG16I"_hs)
        .data<texture_format::RG16U>("RG16U"_hs)
        .data<texture_format::RG16F>("RG16F"_hs)
        .data<texture_format::RG16S>("RG16S"_hs)
        .data<texture_format::RG32I>("RG32I"_hs)
        .data<texture_format::RG32U>("RG32U"_hs)
        .data<texture_format::RG32F>("RG32F"_hs)
        .data<texture_format::RGB8>("RGB8"_hs)
        .data<texture_format::RGB8I>("RGB8I"_hs)
        .data<texture_format::RGB8U>("RGB8U"_hs)
        .data<texture_format::RGB8S>("RGB8S"_hs)
        .data<texture_format::RGB9E5F>("RGB9E5F"_hs)
        .data<texture_format::BGRA8>("BGRA8"_hs)
        .data<texture_format::RGBA8>("RGBA8"_hs)
        .data<texture_format::RGBA8I>("RGBA8I"_hs)
        .data<texture_format::RGBA8U>("RGBA8U"_hs)
        .data<texture_format::RGBA8S>("RGBA8S"_hs)
        .data<texture_format::RGBA16>("RGBA16"_hs)
        .data<texture_format::RGBA16I>("RGBA16I"_hs)
        .data<texture_format::RGBA16U>("RGBA16U"_hs)
        .data<texture_format::RGBA16F>("RGBA16F"_hs)
        .data<texture_format::RGBA16S>("RGBA16S"_hs)
        .data<texture_format::RGBA32I>("RGBA32I"_hs)
        .data<texture_format::RGBA32U>("RGBA32U"_hs)
        .data<texture_format::RGBA32F>("RGBA32F"_hs)
        .data<texture_format::B5G6R5>("B5G6R5"_hs)
        .data<texture_format::R5G6B5>("R5G6B5"_hs)
        .data<texture_format::BGRA4>("BGRA4"_hs)
        .data<texture_format::RGBA4>("RGBA4"_hs)
        .data<texture_format::BGR5A1>("BGR5A1"_hs)
        .data<texture_format::RGB5A1>("RGB5A1"_hs)
        .data<texture_format::RGB10A2>("RGB10A2"_hs)
        .data<texture_format::RG11B10F>("RG11B10F"_hs)
        .data<texture_format::UnknownDepth>("UnknownDepth"_hs)
        .data<texture_format::D16>("D16"_hs)
        .data<texture_format::D24>("D24"_hs)
        .data<texture_format::D24S8>("D24S8"_hs)
        .data<texture_format::D32>("D32"_hs)
        .data<texture_format::D16F>("D16F"_hs)
        .data<texture_format::D24F>("D24F"_hs)
        .data<texture_format::D32F>("D32F"_hs)
        .data<texture_format::D0S8>("D0S8"_hs)
        .data<texture_format::Count>("Count"_hs);

    entt::meta_factory<texture_info>{}
        .type("texture_info"_hs)
        .data<nullptr, &texture_info::format>("format"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Format"} })
        .data<nullptr, &texture_info::storageSize>("storageSize"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Storage Size"},
            entt::attribute{"format", "size"},
            entt::attribute{"data_format", "B"},
        })
        .data<nullptr, &texture_info::width>("width"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Width"} })
        .data<nullptr, &texture_info::height>("height"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Height"} })
        .data<nullptr, &texture_info::depth>("depth"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Depth"} })
        .data<nullptr, &texture_info::numMips>("numMips"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Mips"} })
        .data<nullptr, &texture_info::bitsPerPixel>("bitsPerPixel"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Bits Per Pixel"} })
        .data<nullptr, &texture_info::cubeMap>("cubeMap"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Cubemap"} });
}
} // namespace gfx
