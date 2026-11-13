#pragma once

#include "package_manifest.h"

#include <cstdint>
#include <string>
#include <vector>

namespace unravel::packages
{

/// The folder at the project root that exports write to and the Packages window lists, as deploys go to .deploy.
constexpr const char* PACKAGES_FOLDER_NAME = ".packages";

/// The project's package folder, `project_root`/.packages; it exists once something was exported or put there.
auto get_packages_folder(const fs::path& project_root) -> fs::path;

/// A package file found in a library folder.
struct library_entry
{
    fs::path file;
    package_manifest manifest;
    std::uint64_t size{};
    /// Why the file is not a package that can be read; empty when it is one.
    std::string error;
};

/// The package files in `folders` (not their subfolders), each read; sorted by title, then newest version first.
auto scan_library(const std::vector<fs::path>& folders) -> std::vector<library_entry>;

/// The newest version of the package `name` among `entries`; null when none is there.
auto find_newest(const std::vector<library_entry>& entries, const std::string& name) -> const library_entry*;

} // namespace unravel::packages
