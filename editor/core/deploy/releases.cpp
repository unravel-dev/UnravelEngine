#include "releases.h"
#include "files.h"

#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/mirror.h>
#include <logging/logging.h>
#include <string_utils/utils.h>

#include <hpp/string_view.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
constexpr hpp::string_view RESERVED_NAME_CHARACTERS = "<>:\"/\\|?*";
constexpr unsigned char FIRST_PRINTABLE = ' ';
constexpr std::array<hpp::string_view, 4> RESERVED_DEVICE_NAMES = {"con", "prn", "aux", "nul"};
constexpr std::array<hpp::string_view, 2> NUMBERED_DEVICE_NAMES = {"com", "lpt"};
constexpr double BYTES_PER_MEGABYTE = 1024.0 * 1024.0;

auto has_reserved_character(const std::string& name) -> bool
{
    return std::any_of(name.begin(),
                       name.end(),
                       [](char c)
                       {
                           return RESERVED_NAME_CHARACTERS.find(c) != hpp::string_view::npos ||
                                  static_cast<unsigned char>(c) < FIRST_PRINTABLE;
                       });
}

/// "CON", "con.txt", "COM1": names Windows keeps for devices, with or without an extension.
auto is_device_name(const std::string& name) -> bool
{
    const auto stem = string_utils::to_lower(name.substr(0, name.find('.')));
    const bool is_plain_device = std::find(RESERVED_DEVICE_NAMES.begin(), RESERVED_DEVICE_NAMES.end(), stem) !=
                                 RESERVED_DEVICE_NAMES.end();
    const bool is_numbered_device = stem.size() == 4 && std::isdigit(static_cast<unsigned char>(stem[3])) != 0 &&
                                    std::find(NUMBERED_DEVICE_NAMES.begin(),
                                              NUMBERED_DEVICE_NAMES.end(),
                                              hpp::string_view(stem).substr(0, 3)) != NUMBERED_DEVICE_NAMES.end();
    return is_plain_device || is_numbered_device;
}

auto get_deployed_pack(const fs::path& deploy_location, const std::string& protocol) -> fs::path
{
    return deploy_location / DATA_FOLDER / protocol / ex::get_pack_file_name();
}

/// The packs of a protocol's full build that keep_release copies; an error when the folder holds none.
auto check_full_packed_build(const fs::path& deploy_location) -> result
{
    fs::error_code ec;
    for(const auto& protocol : PACKED_PROTOCOLS)
    {
        const auto root = deploy_location / DATA_FOLDER / protocol;
        if(!fs::exists(root / ex::get_pack_file_name(), ec))
        {
            return result::failure("The deploy folder holds no packed build: deploy with Pack Assets on first.");
        }
        if(fs::exists(root / ex::get_patch_pack_file_name(1), ec))
        {
            return result::failure("The deploy folder holds a patch: keep a release from a full deploy (no Patch "
                                   "Against Release).");
        }
    }
    return {};
}

/// fs::write_pack for a deploy step, reusing the entries of the pack file it replaces. Throws on failure.
void write_pack_or_throw(const fs::path& pack_file, const std::vector<fs::pack_input>& inputs, fs::pack_write_options options)
{
    options.reuse_from = pack_file;
    std::string error;
    fs::pack_write_stats stats;
    if(!fs::write_pack(pack_file, inputs, options, error, &stats))
    {
        throw std::runtime_error(fmt::format("Packing {} failed: {}", pack_file.generic_string(), error));
    }
    APPLOG_INFO("Packed {} entries into {} ({} copied from the last deploy, {} deleted, {} left to the base), "
                "{:.1f} MB -> {:.1f} MB",
                stats.entry_count,
                pack_file.generic_string(),
                stats.reused_count,
                stats.deleted_count,
                stats.unchanged_count,
                double(stats.raw_bytes) / BYTES_PER_MEGABYTE,
                double(stats.stored_bytes) / BYTES_PER_MEGABYTE);
}
} // namespace

auto is_valid_release_name(const std::string& name) -> bool
{
    const bool has_bad_end = !name.empty() && (name.back() == '.' || name.back() == ' ');
    return !name.empty() && name != "." && name != ".." && !has_bad_end && !has_reserved_character(name) &&
           !is_device_name(name);
}

auto get_profile_releases_folder(const fs::path& project_root, const std::string& profile) -> fs::path
{
    return project_root / RELEASES_FOLDER / fs::from_utf8_string(profile);
}

auto get_release_folder(const fs::path& releases, const std::string& name) -> fs::path
{
    return releases / name;
}

auto get_release_pack(const fs::path& releases, const std::string& name, const std::string& protocol) -> fs::path
{
    return get_release_folder(releases, name) / protocol / ex::get_pack_file_name();
}

auto is_kept_release(const fs::path& releases, const std::string& name) -> bool
{
    fs::error_code ec;
    return is_valid_release_name(name) && std::all_of(PACKED_PROTOCOLS.begin(),
                                                      PACKED_PROTOCOLS.end(),
                                                      [&](const char* protocol)
                                                      {
                                                          return fs::exists(get_release_pack(releases, name, protocol), ec);
                                                      });
}

auto list_releases(const fs::path& releases) -> std::vector<std::string>
{
    std::vector<std::string> names;
    fs::error_code ec;
    for(const auto& entry : fs::directory_iterator(releases, ec))
    {
        const auto name = entry.path().filename().string();
        if(entry.is_directory(ec) && is_kept_release(releases, name))
        {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

auto move_releases_into_profile(const fs::path& project_root, const std::string& profile) -> result
{
    const auto old_releases = project_root / RELEASES_FOLDER;
    const auto names = list_releases(old_releases);
    if(names.empty())
    {
        return {};
    }
    const auto destination = get_profile_releases_folder(project_root, profile);
    fs::error_code ec;
    fs::create_directories(destination, ec);
    for(const auto& name : names)
    {
        fs::rename(get_release_folder(old_releases, name), get_release_folder(destination, name), ec);
        if(ec)
        {
            return result::failure(fmt::format("Moving release {} into the releases of profile {} failed: {}", name, profile, ec.message()));
        }
    }
    APPLOG_INFO("Moved {} kept release(s) into the releases of profile {}", names.size(), profile);
    return {};
}

auto keep_release(const fs::path& releases, const fs::path& deploy_location, const std::string& name) -> result
{
    if(!is_valid_release_name(name))
    {
        return result::failure(name.empty() ? "Set the product version, or give the release a name, to keep it."
                                            : fmt::format("'{}' cannot name a release folder.", name));
    }
    const auto release = get_release_folder(releases, name);
    fs::error_code ec;
    if(fs::exists(release, ec))
    {
        return result::failure(fmt::format("A release named {} is already kept in {}.", name, release.generic_string()));
    }
    if(auto checked = check_full_packed_build(deploy_location); !checked.is_ok())
    {
        return checked;
    }
    for(const auto& protocol : PACKED_PROTOCOLS)
    {
        bool is_copied = false;
        if(!fs::copy_file_if_changed(get_deployed_pack(deploy_location, protocol),
                                     get_release_pack(releases, name, protocol),
                                     is_copied,
                                     ec))
        {
            fs::error_code cleanup_ec;
            fs::remove_all(release, cleanup_ec);
            return result::failure(fmt::format("Keeping the release failed: {}", ec.message()));
        }
    }
    APPLOG_INFO("Kept release {} in {}", name, release.generic_string());
    return {};
}

auto remove_release(const fs::path& releases, const std::string& name) -> result
{
    const auto names = list_releases(releases);
    if(std::find(names.begin(), names.end(), name) == names.end())
    {
        return result::failure(fmt::format("No release named '{}' is kept.", name));
    }
    const auto release = get_release_folder(releases, name);
    fs::error_code ec;
    fs::remove_all(release, ec);
    if(ec)
    {
        return result::failure(fmt::format("Removing release {} failed: {}", name, ec.message()));
    }
    APPLOG_INFO("Removed release {} from {}", name, release.generic_string());
    return {};
}

void remove_patch_packs(const fs::path& protocol_root, std::uint32_t first_level)
{
    fs::error_code ec;
    for(std::uint32_t level = first_level; fs::exists(protocol_root / ex::get_patch_pack_file_name(level), ec); ++level)
    {
        fs::remove(protocol_root / ex::get_patch_pack_file_name(level), ec);
    }
}

void write_full_pack(const fs::path& protocol_root, const std::vector<fs::pack_input>& inputs)
{
    write_pack_or_throw(protocol_root / ex::get_pack_file_name(), inputs, {});
    remove_patch_packs(protocol_root, 1);
}

void write_patch_over_release(const fs::path& protocol_root,
                              const std::vector<fs::pack_input>& inputs,
                              const fs::path& release_pack)
{
    copy_file_or_throw(release_pack, protocol_root / ex::get_pack_file_name());
    fs::pack_write_options options;
    options.patch_base = release_pack;
    write_pack_or_throw(protocol_root / ex::get_patch_pack_file_name(1), inputs, options);
    remove_patch_packs(protocol_root, 2);
}

} // namespace unravel::deployment
