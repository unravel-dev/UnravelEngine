#pragma once

#include "icon_image.h"
#include "result.h"

#include <filesystem/filesystem.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/// What Windows shows of an executable: its icon and its version information. A deploy writes them into its
/// copy of the player, so one player serves every game.
namespace unravel::deployment
{

/// The pixel sizes of the images an executable's icon carries. Explorer, the taskbar and the title bar each
/// take the one nearest to the size they draw at, on any display scale.
constexpr std::array<std::uint32_t, 7> WINDOWS_ICON_SIZES = {16, 24, 32, 48, 64, 128, 256};

/// One image of an icon, PNG encoded, as Windows reads it from an icon resource.
struct windows_icon_image
{
    std::uint32_t size{};
    std::vector<std::uint8_t> png;
};

/// The version information (VERSIONINFO) of an executable, which Explorer's Details tab and crash reports show.
struct windows_version_info
{
    /// The version as the project writes it ("1.2" or "1.2.3-beta"): shown as it is; its leading numbers make
    /// the binary version.
    std::string version;
    std::string product_name;
    /// May be empty, which leaves it out.
    std::string company_name;
    /// The executable's file name.
    std::string original_filename;
};

/// The images of an icon made from `image`, scaled to fit each of WINDOWS_ICON_SIZES and centered.
auto make_windows_icon_images(const icon_image& image) -> std::vector<windows_icon_image>;

/// The four numbers of a version text: "1.2.3-beta" is 1, 2, 3, 0. A number past 65535 is 65535.
auto parse_windows_version(const std::string& version) -> std::array<std::uint16_t, 4>;

/// The icon group resource (RT_GROUP_ICON) of `images`, whose image resources (RT_ICON) are numbered from
/// `first_id` on.
auto make_icon_group_resource(const std::vector<windows_icon_image>& images, std::uint16_t first_id) -> std::vector<std::uint8_t>;

/// The version resource (RT_VERSION, a VS_VERSIONINFO) of `info`, in US English and Unicode.
auto make_version_resource(const windows_version_info& info) -> std::vector<std::uint8_t>;

/**
 * @brief Replaces the icon and the version information of the executable `file`.
 *
 * Every icon and version resource of the file is removed first. The new icon takes the name of the first icon
 * the file had, so a lookup by that name still finds it, and as its only icon it is the one Explorer and the
 * window show. Windows only; elsewhere it fails.
 */
auto set_windows_executable_resources(const fs::path& file,
                                      const std::vector<windows_icon_image>& icon,
                                      const windows_version_info& info) -> result;

} // namespace unravel::deployment
