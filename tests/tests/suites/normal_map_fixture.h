#pragma once

#include <bimg/bimg.h>
#include <bx/file.h>
#include <filesystem/filesystem.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

/// Normal maps made from a known surface, for the suites that read a map's green direction.
namespace unravel::test
{

struct surface_map_options
{
    std::uint32_t width{512};
    std::uint32_t height{512};
    /// Whether green points up the image (the OpenGL convention) or down it (the DirectX one).
    bool is_green_up{true};
    /// Whether the surface changes along both axes; false makes ridges that run down the image.
    bool has_two_axes{true};
};

/**
 * @brief The RGBA8 texels of the tangent-space normal map of a surface of smooth waves: X in red, Y in green, Z in
 * blue, alpha opaque. The same options give the same texels.
 */
inline auto make_surface_normal_map(const surface_map_options& options) -> std::vector<std::uint8_t>
{
    struct wave
    {
        float across;
        float down;
        float phase;
        float height;
    };
    constexpr std::uint32_t WAVE_COUNT = 12;
    constexpr float SHORTEST_WAVE = 24.0f;
    constexpr float LONGEST_WAVE = 160.0f;
    constexpr float WAVE_SLOPE = 0.12f;
    constexpr float FULL_TURN = 6.2831853f;

    std::uint32_t state = 0x9E3779B9u;
    const auto next_unit = [&state]
    {
        state = state * 1664525u + 1013904223u;
        return float(state >> 8) / float(1u << 24);
    };
    std::vector<wave> waves(WAVE_COUNT);
    for(auto& item : waves)
    {
        const float length = SHORTEST_WAVE + (LONGEST_WAVE - SHORTEST_WAVE) * next_unit();
        const float angle = options.has_two_axes ? FULL_TURN * next_unit() : 0.0f;
        const float frequency = FULL_TURN / length;
        item.across = frequency * std::cos(angle);
        item.down = frequency * std::sin(angle);
        item.phase = FULL_TURN * next_unit();
        item.height = WAVE_SLOPE / frequency;
    }

    const auto to_byte = [](float value)
    {
        return static_cast<std::uint8_t>(std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    std::vector<std::uint8_t> texels(std::size_t(options.width) * options.height * 4);
    for(std::uint32_t row = 0; row < options.height; ++row)
    {
        for(std::uint32_t column = 0; column < options.width; ++column)
        {
            float slope_across = 0.0f;
            float slope_down = 0.0f;
            for(const auto& item : waves)
            {
                const float change = item.height * std::cos(item.across * float(column) + item.down * float(row) + item.phase);
                slope_across += change * item.across;
                slope_down += change * item.down;
            }
            // The normal leans against the slope. Up the image is the opposite of down it.
            const float x = -slope_across;
            const float y_up = slope_down;
            const float length = std::sqrt(x * x + y_up * y_up + 1.0f);
            std::uint8_t* texel = texels.data() + (std::size_t(row) * options.width + column) * 4;
            texel[0] = to_byte(x / length);
            texel[1] = to_byte((options.is_green_up ? y_up : -y_up) / length);
            texel[2] = to_byte(1.0f / length);
            texel[3] = 255;
        }
    }
    return texels;
}

/// The part of a padded fixture map that holds the surface: its left columns and top rows, as shares of its side.
/// Both end on a cell edge of the coverage grid, so a grid marked over the part selects no padding at all.
constexpr float PADDED_MAP_USED_WIDTH = 0.50f;
constexpr float PADDED_MAP_USED_HEIGHT = 0.75f;

/**
 * @brief A square normal map as a baker writes one for a model: the surface of make_surface_normal_map in its top
 * left part (PADDED_MAP_USED_WIDTH x PADDED_MAP_USED_HEIGHT of it) and padding everywhere else, here streaks of one
 * vector each that run diagonally. There is more padding than surface, and its vectors are chosen so that it reads
 * as the direction the surface does not have: the most that padding can mislead.
 */
inline auto make_padded_normal_map(std::uint32_t side, bool is_green_up) -> std::vector<std::uint8_t>
{
    auto texels = make_surface_normal_map({.width = side, .height = side, .is_green_up = is_green_up});
    // The X of the one vector each diagonal of the image holds.
    std::vector<std::uint8_t> streaks(std::size_t(side) * 2);
    std::uint32_t state = 0x2545F491u;
    for(auto& value : streaks)
    {
        state = state * 1664525u + 1013904223u;
        value = static_cast<std::uint8_t>(64 + ((state >> 12) & 127));
    }
    const auto used_width = static_cast<std::uint32_t>(float(side) * PADDED_MAP_USED_WIDTH);
    const auto used_height = static_cast<std::uint32_t>(float(side) * PADDED_MAP_USED_HEIGHT);
    for(std::uint32_t row = 0; row < side; ++row)
    {
        for(std::uint32_t column = 0; column < side; ++column)
        {
            if(column < used_width && row < used_height)
            {
                continue;
            }
            std::uint8_t* texel = texels.data() + (std::size_t(row) * side + column) * 4;
            // Ridges along a diagonal slope equally across and down the image. Stored with Y equal to X they are
            // a green-down map, with Y opposite to X a green-up one.
            const std::uint8_t x = streaks[std::size_t(row) + column];
            texel[0] = x;
            texel[1] = is_green_up ? x : static_cast<std::uint8_t>(255 - x);
            texel[2] = 255;
        }
    }
    return texels;
}

/// Writes RGBA8 texels as a PNG file, making its folder. Whether it was written.
inline auto write_rgba_png(const fs::path& file, const std::vector<std::uint8_t>& texels, std::uint32_t width, std::uint32_t height)
    -> bool
{
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    bx::FileWriter writer;
    bx::Error error;
    if(!bx::open(&writer, bx::FilePath(file.string().c_str()), false, &error))
    {
        return false;
    }
    bimg::imageWritePng(&writer, width, height, width * 4, texels.data(), bimg::TextureFormat::RGBA8, false, &error);
    bx::close(&writer);
    return error.isOk();
}

} // namespace unravel::test
