#pragma once

#include <filesystem/filesystem.h>

#include <string>

/// Checks on the folder a deploy writes into.
namespace unravel::deployment
{

/// Whether `folder` holds a build an earlier deploy wrote: every deploy writes the app's asset index,
/// loose or inside its pack.
auto is_previous_deploy(const fs::path& folder) -> bool;

/// Whether `folder` holds nothing but Android packages (.apk), and the data/ an earlier Android deploy
/// left beside them: what an Android deploy may replace. True for an empty folder; false for data/
/// without a package, which is a desktop build without its player.
auto is_android_package_folder(const fs::path& folder) -> bool;

/// Whether `path` is `folder` or lies inside it. Canonical paths, so "..", links and letter case cannot
/// slip past; works for paths that do not exist yet.
auto is_inside(const fs::path& path, const fs::path& folder) -> bool;

/// The folder of the project that `path` lies in, when the editor manages that folder: the assets, their
/// compiled files, the settings and profiles, the kept releases, the texture cache or the scripts' build.
/// Empty for any other path. A
/// deploy folder in the project stays out of them, since the deploy replaces and removes files in it.
auto get_managed_project_folder(const fs::path& path, const fs::path& project_root) -> std::string;

/// Whether two names name the same folder: letter case is ignored where the file system ignores it.
auto is_same_folder_name(const std::string& lhs, const std::string& rhs) -> bool;

/// Reads `file` whole, bytes as they are; false when it cannot be opened.
auto read_whole_file(const fs::path& file, std::string& out) -> bool;

/**
 * @brief Renames the folder `from` to `to`, a change of letter case only included: that goes through a free
 * name, since a file system ignoring case takes both names for one. A rename that fails leaves `from` as it
 * was, so `ec` set means nothing moved.
 */
void rename_folder(const fs::path& from, const fs::path& to, fs::error_code& ec);

/// Whether a running program holds `executable` (Windows locks a running executable against writes).
/// Always false where the platform does not lock it, since replacing it there is safe.
auto is_executable_in_use(const fs::path& executable) -> bool;

} // namespace unravel::deployment
