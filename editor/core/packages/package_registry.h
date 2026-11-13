#pragma once

#include <filesystem/filesystem.h>
#include <hpp/uuid.hpp>

#include <string>
#include <vector>

namespace unravel::packages
{

/// The name of the file at the project's root that records the installed packages. It belongs to the project
/// and goes into version control with it.
constexpr const char* PACKAGE_REGISTRY_FILE_NAME = "packages.cfg";

/// A file a package installed, as it was installed.
struct installed_file
{
    /// The id the file has in the project.
    hpp::uuid uid{};
    /// The id the package gives it; another than `uid` when it was installed as a copy.
    hpp::uuid source_uid{};
    /// Its path below the data folder in the package.
    std::string path;
    /// fingerprint_bytes of what was written: a file that no longer matches it was edited.
    std::string hash;
    /// The package's hash of its .meta: an update that brings the same .meta keeps the project's.
    std::string meta_hash;

    auto is_copy() const -> bool
    {
        return uid != source_uid;
    }
};

/// A package installed into the project.
struct installed_package
{
    std::string name;
    std::string display_name;
    std::string version;
    std::string root;
    std::string description;
    std::string author;
    /// The name of the package file it was installed from.
    std::string source_file;
    /// package_archive::get_hash of that file.
    std::string source_hash;
    /// The engine version that installed it.
    std::string installed_with;
    std::vector<installed_file> files;

    auto get_title() const -> const std::string&
    {
        return display_name.empty() ? name : display_name;
    }
    /// The record of the file the package gives `source_uid`; null when it installed none.
    auto find_by_source(const hpp::uuid& source_uid) const -> const installed_file*;
};

/// The packages a project has installed.
struct package_registry
{
    std::vector<installed_package> packages;

    auto find(const std::string& name) const -> const installed_package*;
    /// The package that installed the file with the project id `uid`; null when none did.
    auto find_owner(const hpp::uuid& uid) const -> const installed_package*;
    /// Adds the package, or replaces the record of the package with its name.
    void put(installed_package package);
    /// @return False when no package has the name.
    auto remove(const std::string& name) -> bool;
};

auto get_registry_path(const fs::path& project_root) -> fs::path;
/// Reads the registry. A project without one has an empty registry. False with `err` set when it does not read.
auto load_registry(const fs::path& file, package_registry& registry, std::string& err) -> bool;
/// Writes the registry; a registry without packages removes the file.
auto save_registry(const fs::path& file, const package_registry& registry, std::string& err) -> bool;

} // namespace unravel::packages
