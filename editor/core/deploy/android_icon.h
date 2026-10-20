#pragma once

#include "result.h"

#include <filesystem/filesystem.h>
#include <math/math.h>

#include <cstdint>
#include <string>

/// The launcher icon of an Android package, made from one image.
namespace unravel::deployment
{

/// The resource names the manifest and the adaptive icon refer to.
constexpr const char* ANDROID_ICON_NAME = "ic_launcher";
constexpr const char* ANDROID_ICON_FOREGROUND_NAME = "ic_launcher_foreground";
constexpr const char* ANDROID_ICON_BACKGROUND_NAME = "ic_launcher_background";

/// The side of an adaptive icon's layers, in dp, and of their middle square every launcher shows.
constexpr std::uint32_t ANDROID_ICON_LAYER_DP = 108;
constexpr std::uint32_t ANDROID_ICON_VISIBLE_DP = 72;

/// The color as an opaque Android color resource value, "#RRGGBB".
auto make_android_color(const math::color& color) -> std::string;

/**
 * @brief Writes the Android resources of an adaptive launcher icon into `res_dir`.
 *
 * The foreground is `image`, scaled to fit the layer's middle square that every launcher shows and
 * centered, one PNG per density; the background is `background`, opaque. Launchers cut the icon to their
 * shape (a circle, a rounded square) and move or grow it, which shows a little of the background around
 * the image.
 *
 * Written: mipmap-anydpi-v26/ic_launcher.xml, mipmap-<density>/ic_launcher_foreground.png and
 * values/ic_launcher_background.xml.
 */
auto write_android_icon(const fs::path& image, const math::color& background, const fs::path& res_dir) -> result;

} // namespace unravel::deployment
