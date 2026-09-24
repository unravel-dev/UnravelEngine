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

auto get_format_info(bgfx::TextureFormat::Enum fmt) -> format_details;
auto is_compressed_format(bgfx::TextureFormat::Enum fmt) -> bool;
auto normal_map_needs_z_reconstruction(bgfx::TextureFormat::Enum fmt) -> bool;
auto to_string(bgfx::TextureFormat::Enum fmt) -> std::string;

} // namespace gfx
