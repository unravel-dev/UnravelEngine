#pragma once

#include "deploy_settings.h"

#include <filesystem/filesystem.h>

#include <string>

/// Checks on the folder a deploy writes into.
namespace unravel::deployment
{

/// The file a deploy keeps at the top of a profile's folder: whose build the folder holds, and whether the
/// deploy that wrote it finished.
constexpr const char* BUILD_STAMP_NAME = "build.stamp";

/// What a build's stamp says.
struct build_stamp
{
    std::string profile;
    std::string engine_version;
    /// False from the deploy's start until its last step succeeded: a failed, cancelled or interrupted deploy
    /// leaves a folder with parts of two builds.
    bool is_finished{};
    /// The platform the build is for (get_platform_name); empty in the stamp of an editor that did not write it.
    std::string platform;
};

/// Writes `stamp` into `folder`; false when the file cannot be written.
auto write_build_stamp(const fs::path& folder, const build_stamp& stamp) -> bool;

/// The stamp in `folder`; one with empty fields, unfinished, when the folder has none.
auto read_build_stamp(const fs::path& folder) -> build_stamp;

/// Whether the last deploy into `folder` finished: what uploads the build or keeps it as a release asks first.
auto is_finished_build(const fs::path& folder) -> bool;

/// Why a deploy for `platform` cannot go into `folder`, in one sentence: the folder's stamp names another platform,
/// whose files would stay beside the new build and ship with it. Empty when it can.
auto get_other_platform_problem(const fs::path& folder, deploy_platform platform) -> std::string;

/// What a folder whose build is not finished is refused with.
constexpr const char* UNFINISHED_BUILD_PROBLEM =
    "The folder's build is not marked as finished: its last deploy failed, was cancelled, or came from an older "
    "editor. Deploy again first.";

/// Whether `folder` holds a build an earlier deploy wrote: every deploy writes its stamp first, and the app's
/// asset index, loose or inside its pack.
auto is_previous_deploy(const fs::path& folder) -> bool;

/// Why a desktop build cannot hold the game's executable at `executable`, in one sentence, or empty: where
/// executables carry no extension, a project named like the build's data folder would be written over that folder.
auto get_executable_name_problem(const fs::path& executable) -> std::string;

/// How the name of the native debug symbols a release Android deploy writes beside its package ends.
constexpr const char* NATIVE_SYMBOLS_FILE_SUFFIX = "-native-debug-symbols.zip";
/// The folder of player templates beside the editor (the Android package's, the browser player's).
constexpr const char* PLAYER_TEMPLATES_FOLDER = "binary:/players/";

/// Whether `folder` holds nothing but Android packages (.apk, .aab), their native debug symbols, a deploy's
/// stamp, and the data/ an earlier Android deploy left beside them: what an Android deploy may replace. True for an empty
/// folder; false for data/ without a package, which is a desktop build without its player.
auto is_android_package_folder(const fs::path& folder) -> bool;

/// Whether `path` is `folder` or lies inside it. Canonical paths, so "..", links and letter case cannot
/// slip past; works for paths that do not exist yet.
auto is_inside(const fs::path& path, const fs::path& folder) -> bool;

/// The folder of the project that `path` lies in, when the editor manages that folder: the assets, their
/// compiled files, the settings and profiles, the kept releases, the profiles' cache or the scripts' build.
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
