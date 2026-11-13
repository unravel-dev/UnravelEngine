#pragma once

#include <filesystem/filesystem.h>
#include <hpp/uuid.hpp>

#include <cstdint>
#include <string>
#include <vector>

/// A content package: a set of project assets with their .meta files, shared between projects as one
/// .unravelpkg file (a pack: deterministic, every entry hashed). The package names itself, gives itself a
/// version, records the engine it was made with, and lists every file it holds.
namespace unravel::packages
{

/// The layout of the package file and of its manifest. A reader refuses a newer one.
constexpr std::uint32_t PACKAGE_FORMAT_VERSION = 1;
constexpr const char* PACKAGE_EXTENSION = ".unravelpkg";
/// The manifest's name in a package file, and the name of the file that makes a project folder a package.
constexpr const char* PACKAGE_MANIFEST_NAME = "package.json";
/// Where a package file holds the assets: below it, the paths they have below the data folder.
constexpr const char* PACKAGE_CONTENT_PREFIX = "content/";
constexpr const char* PACKAGE_README_NAME = "README.md";
constexpr const char* PACKAGE_LICENSE_NAME = "LICENSE";
constexpr const char* PACKAGE_CHANGELOG_NAME = "CHANGELOG.md";
/// The longest package name; a name is a short id such as "com.studio.ui-kit".
constexpr std::size_t PACKAGE_NAME_MAX_LENGTH = 128;

/// One asset of a package.
struct package_file
{
    /// Its path below the data folder, '/' separated ("UIKit/Button.pfb").
    std::string path;
    /// The id its .meta gives it.
    hpp::uuid uid{};
    /// Its extension (".pfb").
    std::string type;
    std::uint64_t size{};
    /// fingerprint_bytes of the file as the package holds it.
    std::string hash;
    /// fingerprint_bytes of its .meta as the package holds it.
    std::string meta_hash;
    /// Scenes and prefabs: the layout their document is written in; 0 for any other asset.
    std::uint32_t document_version{};
    /// Scripts: the full names of the script types the file defines.
    std::vector<std::string> script_types;
};

/// Another package this one needs, and the versions of it that do.
struct package_dependency
{
    std::string name;
    /// A version range, see version_range.
    std::string range;
};

/// An engine asset the package's assets reference: not shipped, the engine that installs the package has it.
struct package_engine_asset
{
    hpp::uuid uid{};
    std::string key;
};

/// What a package says about itself, and every file it holds. A project folder that holds a manifest is a
/// package's source: its manifest gives the name, the version and the rest, the export fills in the files.
struct package_manifest
{
    std::uint32_t format_version{PACKAGE_FORMAT_VERSION};
    /// The package's id: lower case letters, digits, '.', '-' and '_' ("com.studio.ui-kit").
    std::string name;
    std::string display_name;
    /// The package's own version, see package_version.
    std::string version;
    std::string description;
    std::string author;
    std::string license;
    /// The engine version the package was exported with.
    std::string exported_with;
    /// The oldest engine version the package works with, when the author sets one.
    std::string minimum_engine;
    /// The folder below the data folder the package installs into ("UIKit").
    std::string root;
    /// A scene to open after the import, by its path below the data folder.
    std::string entry_scene;
    std::vector<package_dependency> dependencies;
    std::vector<package_engine_asset> engine_assets;
    /// The full names of the script types the package's scripts define.
    std::vector<std::string> script_types;
    std::vector<package_file> files;

    /// The display name, or the name when there is none.
    auto get_title() const -> const std::string&;
    /// The file at `path` (below the data folder); null when there is none.
    auto find_file(const std::string& path) const -> const package_file*;
};

/// Why a name cannot name a package; empty when it can.
auto check_package_name(const std::string& name) -> std::string;

/// The manifest as JSON.
auto write_manifest(const package_manifest& manifest) -> std::string;
/// Reads a manifest from JSON. False with `err` set when the text is no manifest, or one of a newer format.
auto read_manifest(const std::string& text, package_manifest& manifest, std::string& err) -> bool;
auto save_manifest(const fs::path& file, const package_manifest& manifest, std::string& err) -> bool;
auto load_manifest(const fs::path& file, package_manifest& manifest, std::string& err) -> bool;

/// The fingerprint a package records for a file's bytes. Text (bytes without a zero) is hashed with its
/// line ends as LF, so a checkout that turns them into CRLF still matches.
auto fingerprint_bytes(const fs::byte_array_t& bytes) -> std::string;
/// fingerprint_bytes of a file; empty when it cannot be read.
auto fingerprint_file(const fs::path& file) -> std::string;
/// Whether the bytes read as text: no zero among them.
auto is_text(const fs::byte_array_t& bytes) -> bool;

/// The name a package file of `manifest` gets: "<name>-<version>.unravelpkg".
auto get_package_file_name(const package_manifest& manifest) -> std::string;

} // namespace unravel::packages
