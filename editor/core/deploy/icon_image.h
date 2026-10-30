#pragma once

#include <filesystem/filesystem.h>

#include <cstdint>
#include <vector>

/// The image an application icon is made from, and the sizes the platforms want of it.
namespace unravel::deployment
{

/// RGBA texels with premultiplied alpha, so filtering never bleeds the color of transparent texels.
struct icon_image
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<float> texels;

    auto get(std::uint32_t x, std::uint32_t y, std::uint32_t channel) const -> float;
    auto is_empty() const -> bool;
};

/// The image in `file`; empty when it is no image bimg reads.
auto load_icon_image(const fs::path& file) -> icon_image;

/// `image` at `width` x `height`. Each target texel averages samples spread over the source texels under
/// it, so a large reduction stays smooth; an enlargement is filtered between texels.
auto resize_icon_image(const icon_image& image, std::uint32_t width, std::uint32_t height) -> icon_image;

/// A clear `side` x `side` square with `image` scaled to fit the `content` x `content` square in its middle,
/// keeping its proportions.
auto fit_icon_image(const icon_image& image, std::uint32_t side, std::uint32_t content) -> icon_image;

/// A color channel from 0 to 1 as a byte.
auto to_color_byte(float value) -> std::uint8_t;

/// The image as a PNG file's bytes; empty when encoding failed.
auto encode_png(const icon_image& image) -> std::vector<std::uint8_t>;

/// Writes the image as a PNG file.
auto write_png(const fs::path& file, const icon_image& image) -> bool;

/// The side of the window icon a game shows where its executable carries no icon (Linux).
constexpr std::uint32_t WINDOW_ICON_SIZE = 256;

/// Writes `image` as the window icon file: a WINDOW_ICON_SIZE square PNG with the image fitted inside.
auto write_window_icon(const fs::path& file, const icon_image& image) -> bool;

} // namespace unravel::deployment
