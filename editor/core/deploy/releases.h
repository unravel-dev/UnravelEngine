#pragma once

#include "keys.h"
#include "result.h"

#include <filesystem/filesystem.h>
#include <filesystem/pack.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/// Kept releases (full packed builds a later deploy patches) and the pack files of a deployed protocol.
namespace unravel::deployment
{

/// The project's folder of kept releases: one subfolder per profile, holding one per release.
constexpr const char* RELEASES_FOLDER = "releases";
/// The protocols a deploy packs, each into data/<protocol>.
constexpr std::array<const char*, 2> PACKED_PROTOCOLS = {ENGINE_PROTOCOL, CONTENT_PROTOCOL};

/// A release name is a folder name: no separators, reserved or control characters, no trailing dot or
/// space (Windows drops them, so two names would share a folder), no device name such as CON.
auto is_valid_release_name(const std::string& name) -> bool;

/// The folder of a profile's kept releases: releases/<profile> in the project.
auto get_profile_releases_folder(const fs::path& project_root, const std::string& profile) -> fs::path;

/// `releases` is a folder of kept releases (get_profile_releases_folder), one subfolder per release.
auto get_release_folder(const fs::path& releases, const std::string& name) -> fs::path;
/// Where a release keeps a protocol's pack.
auto get_release_pack(const fs::path& releases, const std::string& name, const std::string& protocol) -> fs::path;
/// Whether a release of that name is kept, with a pack for every protocol.
auto is_kept_release(const fs::path& releases, const std::string& name) -> bool;
/// The kept releases, by name.
auto list_releases(const fs::path& releases) -> std::vector<std::string>;

/// Keeps the full packed build in `deploy_location` as release `name`.
auto keep_release(const fs::path& releases, const fs::path& deploy_location, const std::string& name) -> result;
/// Deletes a kept release's packs.
auto remove_release(const fs::path& releases, const std::string& name) -> result;

/// Moves the releases a project kept before profiles (releases/<release>) into the releases of `profile`.
auto move_releases_into_profile(const fs::path& project_root, const std::string& profile) -> result;

/// Removes a protocol's patch packs from `first_level` up: the game would mount them over a pack they were
/// not made for.
void remove_patch_packs(const fs::path& protocol_root, std::uint32_t first_level);

/// A full pack: protocol_root/content.pak, and no patch over it. Entries are reused from the pack it
/// replaces; a failed write leaves that pack as it was. Throws on failure.
void write_full_pack(const fs::path& protocol_root, const std::vector<fs::pack_input>& inputs);

/// A patch over a kept release: the release's pack ships unchanged as content.pak, and content_p1.pak holds
/// every change since the release. Throws on failure.
void write_patch_over_release(const fs::path& protocol_root,
                              const std::vector<fs::pack_input>& inputs,
                              const fs::path& release_pack);

} // namespace unravel::deployment
