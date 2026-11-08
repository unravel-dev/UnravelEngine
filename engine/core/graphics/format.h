#pragma once

#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>

namespace gfx
{

namespace format_search_flags
{
enum e
{
    one_channel = 0x1,
    two_channels = 0x2,
    four_channels = 0x8,
    requires_alpha = 0x10,
    requires_stencil = 0x20,
    prefer_compressed = 0x40,

    allow_padding_channels = 0x100,
    requires_depth = 0x200,

    half_precision_float = 0x1000,
    full_precision_float = 0x2000,
    floating_point = 0xF000,
};
} // namespace format_search_flags

auto is_format_supported(std::uint16_t flags, bgfx::TextureFormat::Enum format) -> bool;

auto get_best_format(std::uint16_t type, std::uint32_t search_flags) -> bgfx::TextureFormat::Enum;

auto get_default_rt_sampler_flags() -> std::uint64_t;


// A small struct to hold your derived info.
struct format_details
{
    bool has_alpha_channel{};
    bool is_hdr{};
    int  num_channels{};
};

/// Where a normal map's channels keep its vector; the shaders that decode it take the value as a number.
enum class normal_map_channels : std::uint8_t
{
    /// X, Y and Z in red, green and blue.
    xyz = 0,
    /// X and Y in red and green (BC5, EAC RG11); Z is rebuilt from them.
    xy = 1,
    /// X in red and Y in alpha (an ASTC normal map, which ASTC keeps sharpest that way); Z is rebuilt from them.
    x_red_y_alpha = 2,
};

auto get_format_info(bgfx::TextureFormat::Enum fmt) -> format_details;
auto is_compressed_format(bgfx::TextureFormat::Enum fmt) -> bool;
auto normal_map_needs_z_reconstruction(bgfx::TextureFormat::Enum fmt) -> bool;
/**
 * @brief Where a normal map in `fmt` keeps its vector. `is_normal_map`: its import settings call it a normal map, which
 * the asset compiler gives two channels: BC5 and EAC RG11 hold X and Y, and an ASTC block, which has no two-channel
 * format, holds X in red, green and blue and Y in alpha (texturec's normal map mode). Any other texture read as a
 * normal map holds X, Y and Z, or X and Y in a two-channel format.
 */
auto get_normal_map_channels(bgfx::TextureFormat::Enum fmt, bool is_normal_map) -> normal_map_channels;
auto to_string(bgfx::TextureFormat::Enum fmt) -> std::string;

} // namespace gfx
