#include "android_icon.h"
#include "icon_image.h"

#include <array>
#include <cstdio>
#include <fstream>

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
constexpr const char* ADAPTIVE_ICON_FOLDER = "mipmap-anydpi-v26";
constexpr const char* VALUES_FOLDER = "values";

/// The foreground layer at `scale_percent`: the image scaled to fit the visible square, centered on a
/// clear layer.
auto make_foreground(const icon_image& image, std::uint32_t scale_percent) -> icon_image
{
    return fit_icon_image(image,
                          ANDROID_ICON_LAYER_DP * scale_percent / PERCENT,
                          ANDROID_ICON_VISIBLE_DP * scale_percent / PERCENT);
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
    std::snprintf(text,
                  sizeof(text),
                  "#%02X%02X%02X",
                  to_color_byte(color.value.r),
                  to_color_byte(color.value.g),
                  to_color_byte(color.value.b));
    return text;
}

auto write_android_icon(const fs::path& image, const math::color& background, const fs::path& res_dir) -> result
{
    const auto source = load_icon_image(image);
    if(source.is_empty())
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
