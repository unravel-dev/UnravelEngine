#pragma once

#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>

namespace gfx
{

using texture_format = bgfx::TextureFormat::Enum;

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

auto is_format_supported(std::uint16_t flags, texture_format format) -> bool;

auto get_best_format(std::uint16_t type, std::uint32_t search_flags) -> texture_format;

auto get_default_rt_sampler_flags() -> std::uint64_t;


// A small struct to hold your derived info.
struct format_details
{
    bool has_alpha_channel{};
    bool is_hdr{};
    int  num_hannels{};
};

auto get_format_info(texture_format fmt) -> format_details;
auto to_string(texture_format fmt) -> std::string;

} // namespace gfx
