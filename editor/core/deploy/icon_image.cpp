#include "icon_image.h"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/readerwriter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>

namespace unravel::deployment
{
namespace
{
constexpr std::uint32_t CHANNELS = 4;
constexpr std::uint32_t ALPHA = 3;
constexpr float BYTE_MAX = 255.0f;
/// The samples a target texel takes along each axis at most, when the image shrinks a lot.
constexpr std::uint32_t MAX_SAMPLES_PER_AXIS = 16;

auto read_bytes(const fs::path& file) -> std::vector<char>
{
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void premultiply(icon_image& image)
{
    for(std::size_t i = 0; i < image.texels.size(); i += CHANNELS)
    {
        for(std::uint32_t channel = 0; channel < ALPHA; ++channel)
        {
            image.texels[i + channel] *= image.texels[i + ALPHA];
        }
    }
}

/// The image at (x, y), in texels from its top left corner, between the four nearest texels; the edges
/// repeat outward.
auto sample(const icon_image& image, float x, float y) -> std::array<float, CHANNELS>
{
    const float column = std::clamp(x - 0.5f, 0.0f, float(image.width - 1));
    const float row = std::clamp(y - 0.5f, 0.0f, float(image.height - 1));
    const auto left = static_cast<std::uint32_t>(column);
    const auto top = static_cast<std::uint32_t>(row);
    const std::uint32_t right = std::min(left + 1, image.width - 1);
    const std::uint32_t bottom = std::min(top + 1, image.height - 1);
    const float across = column - float(left);
    const float down = row - float(top);
    std::array<float, CHANNELS> value{};
    for(std::uint32_t channel = 0; channel < CHANNELS; ++channel)
    {
        const float upper = image.get(left, top, channel) * (1.0f - across) + image.get(right, top, channel) * across;
        const float lower = image.get(left, bottom, channel) * (1.0f - across) + image.get(right, bottom, channel) * across;
        value[channel] = upper * (1.0f - down) + lower * down;
    }
    return value;
}

/// RGBA8 texels with straight alpha, as PNG stores them.
auto to_rgba8(const icon_image& image) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> bytes(image.texels.size());
    for(std::size_t i = 0; i < image.texels.size(); i += CHANNELS)
    {
        const float alpha = image.texels[i + ALPHA];
        for(std::uint32_t channel = 0; channel < ALPHA; ++channel)
        {
            bytes[i + channel] = alpha > 0.0f ? to_color_byte(image.texels[i + channel] / alpha) : 0;
        }
        bytes[i + ALPHA] = to_color_byte(alpha);
    }
    return bytes;
}
} // namespace

auto icon_image::get(std::uint32_t x, std::uint32_t y, std::uint32_t channel) const -> float
{
    return texels[(std::size_t(y) * width + x) * CHANNELS + channel];
}

auto icon_image::is_empty() const -> bool
{
    return texels.empty();
}

auto load_icon_image(const fs::path& file) -> icon_image
{
    const auto bytes = read_bytes(file);
    if(bytes.empty())
    {
        return {};
    }
    bx::DefaultAllocator allocator;
    bimg::ImageContainer* container =
        bimg::imageParse(&allocator, bytes.data(), static_cast<std::uint32_t>(bytes.size()), bimg::TextureFormat::RGBA32F);
    if(container == nullptr)
    {
        return {};
    }
    icon_image image;
    bimg::ImageMip mip;
    if(bimg::imageGetRawData(*container, 0, 0, container->m_data, container->m_size, mip))
    {
        const auto* first = reinterpret_cast<const float*>(mip.m_data);
        image.width = mip.m_width;
        image.height = mip.m_height;
        image.texels.assign(first, first + std::size_t(mip.m_width) * mip.m_height * CHANNELS);
    }
    bimg::imageFree(container);
    premultiply(image);
    return image;
}

auto resize_icon_image(const icon_image& image, std::uint32_t width, std::uint32_t height) -> icon_image
{
    icon_image result{width, height, std::vector<float>(std::size_t(width) * height * CHANNELS)};
    const float step_x = float(image.width) / float(width);
    const float step_y = float(image.height) / float(height);
    const auto samples_x = std::clamp(static_cast<std::uint32_t>(std::ceil(step_x)), 1u, MAX_SAMPLES_PER_AXIS);
    const auto samples_y = std::clamp(static_cast<std::uint32_t>(std::ceil(step_y)), 1u, MAX_SAMPLES_PER_AXIS);
    const float weight = 1.0f / float(samples_x * samples_y);
    for(std::uint32_t y = 0; y < height; ++y)
    {
        for(std::uint32_t x = 0; x < width; ++x)
        {
            float* target = result.texels.data() + (std::size_t(y) * width + x) * CHANNELS;
            for(std::uint32_t sy = 0; sy < samples_y; ++sy)
            {
                const float source_y = (float(y) + (float(sy) + 0.5f) / float(samples_y)) * step_y;
                for(std::uint32_t sx = 0; sx < samples_x; ++sx)
                {
                    const float source_x = (float(x) + (float(sx) + 0.5f) / float(samples_x)) * step_x;
                    const auto value = sample(image, source_x, source_y);
                    for(std::uint32_t channel = 0; channel < CHANNELS; ++channel)
                    {
                        target[channel] += value[channel] * weight;
                    }
                }
            }
        }
    }
    return result;
}

auto fit_icon_image(const icon_image& image, std::uint32_t side, std::uint32_t content) -> icon_image
{
    const float fit = std::min(float(content) / float(image.width), float(content) / float(image.height));
    const auto width = std::max(1u, static_cast<std::uint32_t>(std::lround(float(image.width) * fit)));
    const auto height = std::max(1u, static_cast<std::uint32_t>(std::lround(float(image.height) * fit)));
    const auto scaled = resize_icon_image(image, width, height);
    icon_image square{side, side, std::vector<float>(std::size_t(side) * side * CHANNELS, 0.0f)};
    const std::uint32_t left = (side - width) / 2;
    const std::uint32_t top = (side - height) / 2;
    for(std::uint32_t row = 0; row < height; ++row)
    {
        const auto source = scaled.texels.begin() + std::ptrdiff_t(std::size_t(row) * width * CHANNELS);
        const auto target = square.texels.begin() + std::ptrdiff_t((std::size_t(top + row) * side + left) * CHANNELS);
        std::copy(source, source + std::ptrdiff_t(width * CHANNELS), target);
    }
    return square;
}

auto to_color_byte(float value) -> std::uint8_t
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * BYTE_MAX));
}

auto encode_png(const icon_image& image) -> std::vector<std::uint8_t>
{
    const auto bytes = to_rgba8(image);
    bx::DefaultAllocator allocator;
    bx::MemoryBlock block(&allocator);
    bx::MemoryWriter writer(&block);
    bx::Error error;
    bimg::imageWritePng(&writer, image.width, image.height, image.width * CHANNELS, bytes.data(), bimg::TextureFormat::RGBA8, false, &error);
    if(!error.isOk())
    {
        return {};
    }
    const auto size = static_cast<std::size_t>(bx::seek(&writer, 0, bx::Whence::Current));
    const auto* data = static_cast<const std::uint8_t*>(block.more(0));
    return {data, data + size};
}

auto write_png(const fs::path& file, const icon_image& image) -> bool
{
    const auto bytes = encode_png(image);
    if(bytes.empty())
    {
        return false;
    }
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return stream.good();
}

auto write_window_icon(const fs::path& file, const icon_image& image) -> bool
{
    return write_png(file, fit_icon_image(image, WINDOW_ICON_SIZE, WINDOW_ICON_SIZE));
}

} // namespace unravel::deployment
