#include "package_archive.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <unordered_set>

#include <logging/logging.h>

namespace unravel::packages
{
namespace
{
constexpr const char* PACKAGE_META_SUFFIX = ".meta";
/// Characters no file name on the platforms the editor runs on may hold.
constexpr const char* PACKAGE_FORBIDDEN_PATH_CHARS = "<>:\"\\|?*";
/// Names Windows keeps for devices, with or without an extension: a file named so is never written.
constexpr const char* PACKAGE_RESERVED_NAMES[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                                  "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                                  "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

/// Whether Windows reads the path part as a device rather than a file.
auto is_reserved_name(const std::string& part) -> bool
{
    std::string stem = part.substr(0, part.find('.'));
    std::transform(stem.begin(),
                   stem.end(),
                   stem.begin(),
                   [](char value)
                   {
                       return char(std::toupper(static_cast<unsigned char>(value)));
                   });
    return std::any_of(std::begin(PACKAGE_RESERVED_NAMES),
                       std::end(PACKAGE_RESERVED_NAMES),
                       [&stem](const char* name)
                       {
                           return stem == name;
                       });
}

/// The manifest's own paths (its folder and its demo scene) are installed to or opened like the files.
auto check_manifest_paths(const package_manifest& manifest, std::string& err) -> bool
{
    for(const auto* path : {&manifest.root, &manifest.entry_scene})
    {
        if(!path->empty() && !is_safe_package_path(*path))
        {
            err = fmt::format("It names a folder or scene under a path that is not safe ({}).", *path);
            return false;
        }
    }
    return true;
}

auto make_temporary_file(const fs::path& next_to, const std::string& suffix) -> fs::path
{
    return next_to.parent_path() / (next_to.filename().string() + suffix);
}

/// Every file the manifest lists is under a safe path, is listed once, and is in the pack with its .meta.
auto check_manifest_files(const package_manifest& manifest, const fs::pack_reader& pack, std::string& err) -> bool
{
    std::unordered_set<std::string> seen_paths;
    std::unordered_set<hpp::uuid> seen_uids;
    for(const auto& file : manifest.files)
    {
        if(!is_safe_package_path(file.path))
        {
            err = fmt::format("It lists a file under a path that is not safe to install to ({}).", file.path);
            return false;
        }
        if(!seen_paths.insert(fs::make_path_key(file.path)).second)
        {
            err = fmt::format("It lists {} twice.", file.path);
            return false;
        }
        if(file.uid.is_nil() || !seen_uids.insert(file.uid).second)
        {
            err = fmt::format("{} has no id of its own in the package.", file.path);
            return false;
        }
        if(!pack.has_entry(get_content_entry_name(file.path)) || !pack.has_entry(get_meta_entry_name(file.path)))
        {
            err = fmt::format("{} is listed but missing from the package.", file.path);
            return false;
        }
    }
    return true;
}

auto read_checked(const fs::pack_reader& pack,
                  const std::string& entry,
                  const std::string& expected_hash,
                  fs::byte_array_t& bytes,
                  std::string& err) -> bool
{
    if(!pack.read(entry, bytes, err))
    {
        return false;
    }
    if(fingerprint_bytes(bytes) != expected_hash)
    {
        err = fmt::format("{} does not match the hash its manifest gives (the package is damaged).", entry);
        bytes.clear();
        return false;
    }
    return true;
}
} // namespace

auto get_content_entry_name(const std::string& path) -> std::string
{
    return std::string(PACKAGE_CONTENT_PREFIX) + path;
}

auto get_meta_entry_name(const std::string& path) -> std::string
{
    return std::string(PACKAGE_CONTENT_PREFIX) + path + PACKAGE_META_SUFFIX;
}

auto is_safe_package_path(const std::string& path) -> bool
{
    if(path.empty() || path.front() == '/' || path.back() == '/')
    {
        return false;
    }
    if(path.find_first_of(PACKAGE_FORBIDDEN_PATH_CHARS) != std::string::npos)
    {
        return false;
    }
    if(std::any_of(path.begin(),
                   path.end(),
                   [](char value)
                   {
                       return static_cast<unsigned char>(value) < 0x20;
                   }))
    {
        return false;
    }
    std::size_t start = 0;
    while(start <= path.size())
    {
        const auto end = std::min(path.find('/', start), path.size());
        const auto part = path.substr(start, end - start);
        // Windows drops a trailing dot or space, so "Button.pfb." would land on "Button.pfb".
        if(part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ' || is_reserved_name(part))
        {
            return false;
        }
        start = end + 1;
    }
    return true;
}

auto package_archive::open(const fs::path& package_file, std::string& err) -> bool
{
    manifest_ = {};
    location_ = package_file;
    std::string pack_error;
    if(!pack_.open(package_file, pack_error))
    {
        fs::error_code ec;
        err = fs::exists(package_file, ec) ? "The file is damaged or is no package." : "The file does not exist.";
        return false;
    }
    fs::byte_array_t manifest_bytes;
    if(!pack_.read(PACKAGE_MANIFEST_NAME, manifest_bytes, pack_error))
    {
        err = "The file holds no package manifest.";
        return false;
    }
    if(!read_manifest(std::string(manifest_bytes.begin(), manifest_bytes.end()), manifest_, err))
    {
        return false;
    }
    if(!check_package_name(manifest_.name).empty())
    {
        err = fmt::format("Its name ({}) is no package name.", manifest_.name);
        return false;
    }
    if(!check_manifest_paths(manifest_, err) || !check_manifest_files(manifest_, pack_, err))
    {
        return false;
    }
    hash_ = fmt::format("{:016x}", pack_.get_contents_hash());
    fs::error_code ec;
    file_size_ = fs::file_size(package_file, ec);
    return true;
}

auto package_archive::read_file(const package_file& file, fs::byte_array_t& bytes, std::string& err) const -> bool
{
    return read_checked(pack_, get_content_entry_name(file.path), file.hash, bytes, err);
}

auto package_archive::read_meta(const package_file& file, fs::byte_array_t& bytes, std::string& err) const -> bool
{
    return read_checked(pack_, get_meta_entry_name(file.path), file.meta_hash, bytes, err);
}

auto package_archive::read_document(const std::string& name) const -> std::string
{
    fs::byte_array_t bytes;
    std::string err;
    if(!pack_.has_entry(name) || !pack_.read(name, bytes, err))
    {
        return {};
    }
    return std::string(bytes.begin(), bytes.end());
}

auto write_package(const fs::path& package_file,
                   const package_manifest& manifest,
                   const std::vector<package_input>& inputs,
                   const std::vector<std::pair<std::string, fs::path>>& documents,
                   std::string& err) -> bool
{
    fs::error_code ec;
    fs::create_directories(package_file.parent_path(), ec);
    const auto manifest_file = make_temporary_file(package_file, ".manifest");
    if(!save_manifest(manifest_file, manifest, err))
    {
        return false;
    }
    std::vector<fs::pack_input> entries;
    entries.reserve(inputs.size() * 2 + documents.size() + 1);
    entries.push_back({PACKAGE_MANIFEST_NAME, manifest_file});
    for(const auto& [name, source] : documents)
    {
        entries.push_back({name, source});
    }
    for(const auto& input : inputs)
    {
        entries.push_back({get_content_entry_name(input.path), input.source});
        entries.push_back({get_meta_entry_name(input.path), input.meta_source});
    }
    // Sorted, so the same content packs to the same bytes whatever order it was found in.
    std::sort(entries.begin(),
              entries.end(),
              [](const fs::pack_input& lhs, const fs::pack_input& rhs)
              {
                  return lhs.name < rhs.name;
              });
    std::string pack_error;
    const bool is_written = fs::write_pack(package_file, entries, {}, pack_error);
    fs::remove(manifest_file, ec);
    if(!is_written)
    {
        err = "The package could not be written: " + pack_error;
        return false;
    }
    return true;
}

} // namespace unravel::packages
