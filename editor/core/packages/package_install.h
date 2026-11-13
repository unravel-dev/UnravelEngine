#pragma once

#include "package_import.h"

#include <functional>
#include <string>
#include <vector>

namespace unravel::packages
{

/// How an install puts project files aside: the editor's trash, where the user can get them back.
struct install_hooks
{
    /// Moves project files (absolute paths) with their .meta files into a new trash batch, named in `batch`.
    std::function<bool(const std::vector<fs::path>& files, std::string& batch, std::string& err)> move_to_trash;
    /// Puts a batch back, when the install that made it fails.
    std::function<bool(const std::string& batch, std::string& err)> restore_from_trash;
};

/// What an install or a removal did.
struct install_result
{
    bool is_done{};
    /// Why it failed, a sentence; the project is as it was.
    std::string error;
    /// The project files written, by key.
    std::vector<std::string> written_keys;
    /// The project files that went to the trash or away, by key.
    std::vector<std::string> removed_keys;
    /// Files kept: edited ones, and ones no longer the package's.
    std::size_t kept_count{};
    /// The trash batch the replaced and removed files went to; empty when none did.
    std::string trash_batch;
    /// The record the registry now holds; empty after a removal.
    installed_package record;
};

/**
 * @brief Applies an import plan: every file is read from the package and checked, the ids of files installed as
 * copies are rewritten in the package's text files, and everything is staged in `stage_dir` before the project is
 * touched. Then project files that are replaced or removed go to the trash, the .meta files and the files are
 * written, and the registry records the package. A failure on the way puts everything back.
 *
 * The caller pauses the file watcher around it and tells it about the written and removed files afterwards.
 */
auto install_package(const package_archive& archive,
                     const import_plan& plan,
                     const project_view& project,
                     const fs::path& stage_dir,
                     const install_hooks& hooks) -> install_result;

/// What removing an installed package would do.
struct removal_plan
{
    installed_package package;
    /// The package's files still in the project: unchanged ones go to the trash, edited ones stay.
    std::vector<planned_removal> files;
};

auto plan_removal(const project_view& project, const std::string& name) -> removal_plan;

/// Removes the package's unchanged files (to the trash) and its record. Edited files stay, no longer the package's.
auto remove_package(const removal_plan& plan, const project_view& project, const install_hooks& hooks) -> install_result;

} // namespace unravel::packages
