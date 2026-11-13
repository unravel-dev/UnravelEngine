#pragma once

#include <version/version.h>

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// The editor's update check: the release workflow attaches latest.json, the newest published build of every
/// platform, to the releases; the editor reads the latest release's copy and names a newer build of its own
/// platform.
namespace unravel::update_check
{

/// The largest latest.json that is read. The real file is about one kilobyte.
constexpr std::size_t MAX_MANIFEST_SIZE = 64 * 1024;
/// The layout of latest.json this editor reads. A file of another layout is no answer.
constexpr std::uint64_t MANIFEST_SCHEMA = 1;

/// A published build of one platform, as the release workflow lists it in latest.json.
struct release_build
{
    /// The platform part of the package name: "Windows-AMD64", "Linux-x86_64-Ubuntu-24.04".
    std::string platform;
    /// The package version: "1.2.0-413".
    version::engine_version version;
    /// The release that holds the package: "v1.2.0".
    std::string tag;
};

/// What latest.json holds: the newest published build of every platform.
struct update_manifest
{
    std::vector<release_build> builds;
};

/**
 * @brief Reads latest.json.
 *
 * Nothing when the text is larger than MAX_MANIFEST_SIZE, is not JSON, or is not an object with "schema" equal to
 * MANIFEST_SCHEMA and a "builds" array. An entry is kept only when its "platform" is a platform name, its "version"
 * is a package version, its "tag" is a release tag and its "asset" is the package name these make; other entries
 * are skipped. Unknown fields are ignored.
 */
auto read_update_manifest(hpp::string_view text) -> hpp::optional<update_manifest>;

/// The build of `platform`, when the manifest lists one.
auto find_platform_build(const update_manifest& manifest, hpp::string_view platform) -> hpp::optional<release_build>;

/// Whether `text` is the platform part of a package name: a letter, then letters, digits, dots, dashes and
/// underscores.
auto is_platform_name(hpp::string_view text) -> bool;

/// Whether `text` is a release tag: v<major>.<minor>.<patch>, as git describe names the release.
auto is_release_tag(hpp::string_view text) -> bool;

/// The package version in `text`, written as a package name carries it ("1.2.0", "1.2.0-413"); nothing for any other
/// spelling, such as one with a commit or a leading v.
auto parse_package_version(hpp::string_view text) -> hpp::optional<version::engine_version>;

/// The package file name of a build: UnravelEngine-<version>-<platform>.zip.
auto get_package_file_name(const version::engine_version& version, hpp::string_view platform) -> std::string;

} // namespace unravel::update_check
