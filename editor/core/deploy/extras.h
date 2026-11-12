#pragma once

#include <filesystem/filesystem.h>

#include <string>
#include <vector>

/**
 * @brief What a project adds to a build beyond what the engine makes of it: a folder of extra files that ship as
 * they are, and a command that runs once the build is whole.
 */
namespace unravel::deployment
{

/// The file in a profile's cache that names the extra files its last deploy copied, one per line: what the next
/// deploy removes when the folder no longer has it.
constexpr const char* EXTRA_FILES_RECORD_NAME = "extra_files.txt";

/// The files under `folder`, however deep, as '/' separated names relative to it, in name order. None for an
/// empty path or a folder that is not there. Throws when the folder cannot be listed.
auto list_extra_files(const fs::path& folder) -> std::vector<std::string>;

/**
 * @brief Removes from `build` the files an earlier deploy_extra_files copied (the names `record` keeps) that
 * `folder` no longer has, with the folders that leaves empty.
 *
 * A deploy calls it before its steps write the build's own files: an extra file that had taken the place of one of
 * the build's goes first, and the step that makes that file writes it anew. Called after them, it would remove the
 * build's fresh file. An empty or missing `folder` has none of the earlier files. Throws when the folder cannot be
 * listed or a file cannot be removed.
 */
void remove_stale_extra_files(const fs::path& folder, const fs::path& build, const fs::path& record);

/**
 * @brief Copies the files of `folder` into `build` at the same places, once the build's own files are there, and
 * keeps their names in `record` for the next deploy's remove_stale_extra_files.
 *
 * A file of the build that an extra file is named like is replaced by it. Unchanged files are not copied again.
 * An empty or missing `folder` copies nothing.
 * @return The names copied, as list_extra_files gives them. Throws when a file cannot be copied or the record
 * cannot be written.
 */
auto deploy_extra_files(const fs::path& folder, const fs::path& build, const fs::path& record)
    -> std::vector<std::string>;

/// What a post-deploy command is told of the build, through its environment.
struct post_command_context
{
    /// The build's folder (UNRAVEL_DEPLOY_FOLDER), which the command also starts in.
    fs::path folder;
    /// UNRAVEL_DEPLOY_PROFILE.
    std::string profile;
    /// UNRAVEL_DEPLOY_PLATFORM: get_platform_name.
    std::string platform;
    /// UNRAVEL_DEPLOY_VERSION: the product version.
    std::string version;
};

/// The "NAME=value" entries a post-deploy command's environment gets on top of the editor's.
auto make_post_command_environment(const post_command_context& context) -> std::vector<std::string>;

/// The name of the script file a post-deploy command runs from: a batch file on Windows, a shell script elsewhere.
auto get_post_command_script_name() -> std::string;

/**
 * @brief The text of the script that runs `command`: it enters the build's folder (UNRAVEL_DEPLOY_FOLDER) first,
 * and ends with the command's exit code. A script file takes the command as the user wrote it, quotes and all,
 * where passing it on a command line would need a second layer of quoting that the system's interpreter does not
 * undo.
 */
auto make_post_command_script(const std::string& command) -> std::string;

/// The command line that runs the script at `script` through the system's command interpreter.
auto make_post_command_arguments(const fs::path& script) -> std::vector<std::string>;

} // namespace unravel::deployment
