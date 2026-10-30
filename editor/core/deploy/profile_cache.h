#pragma once

#include "result.h"

#include <filesystem/filesystem.h>

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

/// The profiles' cooked texture variants in the project: cache/profiles/<profile>/<protocol>/, laid out
/// as the protocol's compiled folder, one file per asset and profile. Derived data: any of it can be
/// deleted and is compiled again by the next deploy that needs it.
namespace unravel::deployment
{

/// The project's folder of derived data beside the compiled cache.
constexpr const char* CACHE_FOLDER = "cache";

/// cache/profiles/<profile> in the project.
auto get_profile_cache_folder(const fs::path& project_root, const std::string& profile) -> fs::path;

/// The folder of a profile's variants of a protocol's compiled files: cache/profiles/<profile>/<protocol>.
auto get_profile_variants_folder(const fs::path& project_root, const std::string& profile, const std::string& protocol) -> fs::path;

/// The debug files of the profile's last deploy: its executable and the player's symbols, which a kept
/// release takes along (cache/profiles/<profile>/symbols).
auto get_profile_symbols_folder(const fs::path& project_root, const std::string& profile) -> fs::path;

/// What a sweep did: how many files it deleted, and the ones it could not delete.
struct sweep_report
{
    std::size_t removed{};
    std::vector<fs::path> failed;
};

/**
 * @brief Deletes every file under `folder` that `kept` does not name, and the folders left empty.
 * @param kept Paths relative to `folder` as fs::make_path_key makes them of generic strings.
 */
auto sweep_folder(const fs::path& folder, const std::unordered_set<std::string>& kept) -> sweep_report;

/// Deletes a profile's cache folder.
auto remove_profile_cache(const fs::path& project_root, const std::string& profile) -> result;

/// Moves a profile's cache folder to the profile's new name; nothing to move is no failure.
auto rename_profile_cache(const fs::path& project_root, const std::string& profile, const std::string& new_name) -> result;

/// Deletes the cache folders of profiles the project no longer has (deleted outside the editor, say).
/// @return The names of the folders deleted.
auto remove_unknown_profile_caches(const fs::path& project_root, const std::vector<std::string>& profiles) -> std::vector<std::string>;

} // namespace unravel::deployment
