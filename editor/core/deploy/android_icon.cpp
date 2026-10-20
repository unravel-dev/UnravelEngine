#include "android_icon.h"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/file.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace unravel::deployment
{
namespace
{
/// A launcher density: the resource folder's suffix and the pixels one dp takes, in percent.
struct density
{
    const char* name;
    std::uint32_t scale_percent;
};

constexpr std::array<density, 5> DENSITIES = {{
    {"mdpi", 100},
    {"hdpi", 150},
    {"xhdpi", 200},
    {"xxhdpi", 300},
    {"xxxhdpi", 400},
}};
constexpr std::uint32_t PERCENT = 100;
constexpr std::uint32_t CHANNELS = 4;
constexpr std::uint32_t ALPHA = 3;
constexpr float BYTE_MAX = 255.0f;
/// The samples a target texel takes along each axis at most, when the image shrinks a lot.
constexpr std::uint32_t MAX_SAMPLES_PER_AXIS = 16;
constexpr const char* ADAPTIVE_ICON_FOLDER = "mipmap-anydpi-v26";
constexpr const char* VALUES_FOLDER = "values";

/// RGBA texels with premultiplied alpha, so filtering never bleeds the color of transparent texels.
struct premultiplied_image
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<float> texels;

    auto get(std::uint32_t x, std::uint32_t y, std::uint32_t channel) const -> float
    {
        return texels[(std::size_t(y) * width + x) * CHANNELS + channel];
    }
};

auto read_bytes(const fs::path& file) -> std::vector<char>
{
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

/// The image in `file`; empty when it is no image bimg reads.
auto load_image(const fs::path& file) -> premultiplied_image
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
    premultiplied_image image;
    bimg::ImageMip mip;
    if(bimg::imageGetRawData(*container, 0, 0, container->m_data, container->m_size, mip))
    {
        const auto* first = reinterpret_cast<const float*>(mip.m_data);
        image.width = mip.m_width;
        image.height = mip.m_height;
        image.texels.assign(first, first + std::size_t(mip.m_width) * mip.m_height * CHANNELS);
    }
    bimg::imageFree(container);
    for(std::size_t i = 0; i < image.texels.size(); i += CHANNELS)
    {
        for(std::uint32_t channel = 0; channel < ALPHA; ++channel)
        {
            image.texels[i + channel] *= image.texels[i + ALPHA];
        }
    }
    return image;
}

/// The image at (x, y), in texels from its top left corner, between the four nearest texels; the edges
/// repeat outward.
auto sample(const premultiplied_image& image, float x, float y) -> std::array<float, CHANNELS>
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

/// `image` at `width` x `height`. Each target texel averages samples spread over the source texels under
/// it, so a large reduction stays smooth; an enlargement is filtered between texels.
auto resize(const premultiplied_image& image, std::uint32_t width, std::uint32_t height) -> premultiplied_image
{
    premultiplied_image result{width, height, std::vector<float>(std::size_t(width) * height * CHANNELS)};
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

/// The foreground layer at `scale_percent`: the image scaled to fit the visible square, centered on a
/// clear layer.
auto make_foreground(const premultiplied_image& image, std::uint32_t scale_percent) -> premultiplied_image
{
    const std::uint32_t layer = ANDROID_ICON_LAYER_DP * scale_percent / PERCENT;
    const std::uint32_t visible = ANDROID_ICON_VISIBLE_DP * scale_percent / PERCENT;
    const float fit = std::min(float(visible) / float(image.width), float(visible) / float(image.height));
    const auto width = std::max(1u, static_cast<std::uint32_t>(std::lround(float(image.width) * fit)));
    const auto height = std::max(1u, static_cast<std::uint32_t>(std::lround(float(image.height) * fit)));
    const auto scaled = resize(image, width, height);
    premultiplied_image foreground{layer, layer, std::vector<float>(std::size_t(layer) * layer * CHANNELS, 0.0f)};
    const std::uint32_t left = (layer - width) / 2;
    const std::uint32_t top = (layer - height) / 2;
    for(std::uint32_t row = 0; row < height; ++row)
    {
        const auto source = scaled.texels.begin() + std::ptrdiff_t(std::size_t(row) * width * CHANNELS);
        const auto target = foreground.texels.begin() + std::ptrdiff_t((std::size_t(top + row) * layer + left) * CHANNELS);
        std::copy(source, source + std::ptrdiff_t(width * CHANNELS), target);
    }
    return foreground;
}

auto to_byte(float value) -> std::uint8_t
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * BYTE_MAX));
}

/// RGBA8 texels with straight alpha, as PNG stores them.
auto to_rgba8(const premultiplied_image& image) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> bytes(image.texels.size());
    for(std::size_t i = 0; i < image.texels.size(); i += CHANNELS)
    {
        const float alpha = image.texels[i + ALPHA];
        for(std::uint32_t channel = 0; channel < ALPHA; ++channel)
        {
            bytes[i + channel] = alpha > 0.0f ? to_byte(image.texels[i + channel] / alpha) : 0;
        }
        bytes[i + ALPHA] = to_byte(alpha);
    }
    return bytes;
}

auto write_png(const fs::path& file, const premultiplied_image& image) -> bool
{
    const auto bytes = to_rgba8(image);
    bx::FileWriter writer;
    bx::Error error;
    if(!bx::open(&writer, bx::FilePath(file.string().c_str()), false, &error))
    {
        return false;
    }
    bimg::imageWritePng(&writer, image.width, image.height, image.width * CHANNELS, bytes.data(), bimg::TextureFormat::RGBA8, false, &error);
    bx::close(&writer);
    return error.isOk();
}

auto write_text(const fs::path& file, const std::string& text) -> bool
{
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream << text;
    return stream.good();
}

auto make_adaptive_icon_xml() -> std::string
{
    return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                       "<adaptive-icon xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
                       "    <background android:drawable=\"@color/") +
           ANDROID_ICON_BACKGROUND_NAME + "\" />\n    <foreground android:drawable=\"@mipmap/" + ANDROID_ICON_FOREGROUND_NAME +
           "\" />\n</adaptive-icon>\n";
}

auto make_background_xml(const math::color& background) -> std::string
{
    return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources>\n    <color name=\"") +
           ANDROID_ICON_BACKGROUND_NAME + "\">" + make_android_color(background) + "</color>\n</resources>\n";
}

auto make_write_error(const fs::path& file) -> result
{
    return result::failure("Writing " + file.generic_string() + " failed.");
}
} // namespace

auto make_android_color(const math::color& color) -> std::string
{
    char text[8]{};
    std::snprintf(text, sizeof(text), "#%02X%02X%02X", to_byte(color.value.r), to_byte(color.value.g), to_byte(color.value.b));
    return text;
}

auto write_android_icon(const fs::path& image, const math::color& background, const fs::path& res_dir) -> result
{
    const auto source = load_image(image);
    if(source.texels.empty())
    {
        return result::failure("The icon image " + image.generic_string() + " could not be read as an image.");
    }
    fs::error_code ec;
    for(const auto& item : DENSITIES)
    {
        const auto folder = res_dir / (std::string("mipmap-") + item.name);
        fs::create_directories(folder, ec);
        const auto file = folder / (std::string(ANDROID_ICON_FOREGROUND_NAME) + ".png");
        if(!write_png(file, make_foreground(source, item.scale_percent)))
        {
            return make_write_error(file);
        }
    }
    fs::create_directories(res_dir / ADAPTIVE_ICON_FOLDER, ec);
    const auto icon_file = res_dir / ADAPTIVE_ICON_FOLDER / (std::string(ANDROID_ICON_NAME) + ".xml");
    if(!write_text(icon_file, make_adaptive_icon_xml()))
    {
        return make_write_error(icon_file);
    }
    fs::create_directories(res_dir / VALUES_FOLDER, ec);
    const auto background_file = res_dir / VALUES_FOLDER / (std::string(ANDROID_ICON_BACKGROUND_NAME) + ".xml");
    if(!write_text(background_file, make_background_xml(background)))
    {
        return make_write_error(background_file);
    }
    return {};
}

} // namespace unravel::deployment
