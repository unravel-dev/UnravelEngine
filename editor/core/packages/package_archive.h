#pragma once

#include "package_manifest.h"

#include <filesystem/pack.h>

#include <string>
#include <vector>

namespace unravel::packages
{

/**
 * @brief An opened package: a .unravelpkg file. Opening reads and checks the manifest: every file it lists
 * is in the package with its .meta, under a safe path, and no path is listed twice. Reading a file checks its
 * bytes against the pack's hash and the manifest's.
 */
class package_archive
{
public:
    /// @return False with `err` set (a sentence) when the file is missing, damaged, or no package.
    auto open(const fs::path& package_file, std::string& err) -> bool;

    auto get_manifest() const -> const package_manifest&
    {
        return manifest_;
    }

    auto get_location() const -> const fs::path&
    {
        return location_;
    }

    /// Identifies the package's content: two files with the same hash hold the same package.
    auto get_hash() const -> const std::string&
    {
        return hash_;
    }

    /// The bytes of a file the manifest lists, checked against its hash.
    auto read_file(const package_file& file, fs::byte_array_t& bytes, std::string& err) const -> bool;
    /// The bytes of a file's .meta, checked against its hash.
    auto read_meta(const package_file& file, fs::byte_array_t& bytes, std::string& err) const -> bool;
    /// A document the package carries beside its content (README.md, LICENSE, CHANGELOG.md); empty when it has none.
    auto read_document(const std::string& name) const -> std::string;
    /// The package file's size on disk.
    auto get_file_size() const -> std::uint64_t
    {
        return file_size_;
    }

private:
    fs::pack_reader pack_;
    package_manifest manifest_;
    fs::path location_;
    std::string hash_;
    std::uint64_t file_size_{};
};

/// The entry name a package file holds an asset under.
auto get_content_entry_name(const std::string& path) -> std::string;
/// The entry name a package file holds an asset's .meta under.
auto get_meta_entry_name(const std::string& path) -> std::string;
/// Whether a path below the data folder is safe to write a package's file to: relative, '/' separated, with
/// no "." or ".." part and no characters a file name cannot hold.
auto is_safe_package_path(const std::string& path) -> bool;

/// A file a package is written from.
struct package_input
{
    /// Its path below the data folder.
    std::string path;
    fs::path source;
    fs::path meta_source;
};

/**
 * @brief Writes a package file: the manifest, the documents and every input with its .meta. The output depends
 * only on what goes in, so the same content gives the same bytes. Written next to `package_file` and moved into
 * place.
 * @param documents Files to carry beside the content, by the name they get (README.md and the like).
 */
auto write_package(const fs::path& package_file,
                   const package_manifest& manifest,
                   const std::vector<package_input>& inputs,
                   const std::vector<std::pair<std::string, fs::path>>& documents,
                   std::string& err) -> bool;

} // namespace unravel::packages
