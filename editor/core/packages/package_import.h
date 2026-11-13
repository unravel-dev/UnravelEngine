#pragma once

#include "package_archive.h"
#include "package_export.h"
#include "package_registry.h"

#include <engine/assets/asset_storage.h>
#include <hpp/optional.hpp>
#include <version/version.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace unravel::packages
{

/// What the project looks like to an import, taken on the main thread.
struct project_view
{
    /// The project's folder; its data folder is "data" in it.
    fs::path project_root;
    /// The project's id: copies get ids made from it, so another project gets other ones.
    std::string project_guid;
    /// The project's assets.
    asset_database::database_t database;
    /// The ids of the engine's assets.
    std::unordered_set<hpp::uuid> engine_uids;
    /// The project's script types and their files.
    std::vector<script_type_source> script_types;
    version::engine_version engine;
    package_registry registry;
    /// Why the project's packages.cfg did not read; empty when it did (or there is none). An import or a removal
    /// would save the registry over it, losing every record it holds, so neither runs then.
    std::string registry_error;
};

/// Where a package's file stands against the project.
enum class file_status : std::uint8_t
{
    /// New to the project.
    added,
    /// Installed by an earlier version, unchanged since, and the package brings another one.
    updated,
    /// Installed and the same as the package's.
    unchanged,
    /// Installed by an earlier version and edited since.
    edited,
    /// Installed by an earlier version and deleted since.
    deleted,
    /// Not installed by the package, but the project holds the same file under the same id.
    present,
    /// Another file of the project has the package file's id.
    uid_clash,
    /// Another file of the project is where the package puts this one.
    path_clash,
    /// This engine does not read it (a scene or prefab of a newer layout).
    incompatible,
};

/// What the import does with a file.
enum class file_action : std::uint8_t
{
    /// Leave the project as it is.
    skip,
    /// Write the package's file where it goes (or where the installed one is now), keeping its id.
    write,
    /// Write the package's file beside the project's, under a free name, with an id of its own.
    write_copy,
    /// Keep the project's edited file as it is, still as the package's.
    keep_mine,
    /// Move the project's file to the trash and write the package's in its place, with the project file's id.
    replace,
    /// Record the project's file as the package's, without writing.
    adopt,
};

/// One file of the package in the plan.
struct planned_file
{
    /// Its index in the manifest's files.
    std::size_t index{};
    file_status status{file_status::added};
    file_action action{file_action::write};
    /// Where the package's file goes when it is written.
    std::string target_key;
    /// Where a copy goes.
    std::string copy_key;
    /// The project file the status is about (the installed or clashing one); empty when none.
    std::string existing_key;
    /// The id of that project file.
    hpp::uuid existing_uid{};
    /// The actions the import offers for it, the default first.
    std::vector<file_action> choices;
};

/// A file an earlier version installed that the package no longer has.
struct planned_removal
{
    std::string key;
    hpp::uuid uid{};
    bool is_edited{};
    /// True: to the trash; false: kept, no longer the package's.
    bool is_removed{true};
};

/// A dependency of the package, checked against the installed packages.
struct dependency_check
{
    package_dependency dependency;
    /// The version installed; empty when the package is not installed.
    std::string installed_version;
    bool is_satisfied{};
};

/// What importing a package would do.
struct import_plan
{
    package_manifest manifest;
    std::string package_hash;
    std::string package_file_name;
    /// The installed version of the package, when an earlier one is installed.
    hpp::optional<installed_package> installed;
    std::vector<planned_file> files;
    std::vector<planned_removal> removals;
    std::vector<dependency_check> dependencies;
    /// Engine assets the package references that this engine does not have.
    std::vector<std::string> missing_engine_assets;
    /// Why the package cannot be imported: sentences.
    std::vector<std::string> problems;
    /// Script types two files would define with the actions as they are now (update_script_clashes): sentences.
    /// They block the import like problems, and another action can clear them.
    std::vector<std::string> clashes;
    /// What the user should know before importing: sentences.
    std::vector<std::string> warnings;

    auto is_update() const -> bool
    {
        return installed.has_value();
    }
    /// The installed version is newer than the package's.
    auto is_downgrade() const -> bool;
    auto can_apply() const -> bool;
    /// Whether applying it changes anything.
    auto has_changes() const -> bool;
    auto count(file_status status) const -> std::size_t;
};

/// The id a package's file gets in this project when it is installed as a copy. The same project, package and file
/// give the same id, so an update finds the copy again.
auto make_copy_uid(const std::string& project_guid, const std::string& package_name, const hpp::uuid& source_uid) -> hpp::uuid;

/// Plans the import of `archive` into the project. Reads the project files it compares, writes nothing.
auto plan_import(const package_archive& archive, const project_view& project) -> import_plan;

/// Where the planned file is written with its current action; empty when the action writes nothing.
auto get_destination(const planned_file& planned) -> std::string;

/**
 * @brief The script types two files of the project would define once the plan is applied with its current actions:
 * a project script that stays defines a type that a package script written to another place defines too. The scripts
 * would no longer build. Sentences.
 */
auto find_script_clashes(const import_plan& plan, const project_view& project) -> std::vector<std::string>;

/// Sets plan.clashes for the plan's current actions; call it after an action changes.
void update_script_clashes(import_plan& plan, const project_view& project);

/**
 * @brief Whether two plans of one package treat the project alike: per file the same status, places and choices, the
 * same files dropped, the same problems. The actions picked in a review are not compared.
 */
auto is_same_plan(const import_plan& lhs, const import_plan& rhs) -> bool;

/// Takes over into @p plan the actions @p picked chose, file by file, where the file is planned alike in both.
void carry_actions(const import_plan& picked, import_plan& plan);

auto get_file_status_name(file_status status) -> const char*;
auto get_file_action_name(file_action action) -> const char*;
/// A sentence on what the action does, for a tooltip.
auto describe_file_action(file_action action) -> const char*;

} // namespace unravel::packages
