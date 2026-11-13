#pragma once

#include "package_manifest.h"

#include <deploy/content.h>

#include <cstdint>
#include <string>
#include <vector>

namespace unravel::packages
{

/// A script type the project defines, and the script file that defines it.
struct script_type_source
{
    std::string full_name;
    /// The asset key of the .cs file ("app:/data/Scripts/Player.cs").
    std::string key;
};

/// What an export reads, taken on the main thread so the planning runs on any thread.
struct export_source
{
    deployment::content_source content;
    /// Every script type of the project's script library, with its file.
    std::vector<script_type_source> script_types;
};

/// Why a file goes into a package.
enum class export_reason : std::uint8_t
{
    /// It lies in the package's folder.
    in_folder,
    /// A file of the package references it.
    referenced,
    /// It defines a script type a scene or prefab of the package uses.
    script,
};

/// A file an export can put into the package.
struct export_entry
{
    std::string key;
    /// Its path below the data folder: where the package installs it.
    std::string path;
    hpp::uuid uid{};
    std::string type;
    std::uint64_t size{};
    export_reason reason{export_reason::in_folder};
    /// The file that led the export to it; empty for a file in the folder.
    std::string used_by;
    /// It lies outside the package's folder, where it is shared with the rest of the project.
    bool is_outside_root{};
    bool is_included{true};
};

/// What exporting a folder would put into a package: every file in it and every file they reach.
struct export_plan
{
    /// The folder ("app:/data/UIKit").
    std::string folder_key;
    /// Its path below the data folder ("UIKit").
    std::string root;
    fs::path project_root;
    /// The folder's package.json; defaults made from the folder when there is none.
    package_manifest definition;
    bool has_definition{};
    /// By path.
    std::vector<export_entry> entries;
    /// The engine assets the files reference.
    std::vector<package_engine_asset> engine_assets;
    /// Script types the scenes and prefabs use that no project or engine script defines now.
    std::vector<std::string> unresolved_script_types;
    /// The types each included script defines, for the manifest and for the clash check of an import.
    std::vector<script_type_source> script_types;

    auto get_included_count() const -> std::size_t;
    auto get_included_size() const -> std::uint64_t;
    /// The included scripts' files on disk.
    auto get_included_script_files() const -> std::vector<fs::path>;
};

/// The path of the package.json that makes the folder `root` (below the data folder) a package's source.
auto get_definition_path(const fs::path& project_root, const std::string& root) -> fs::path;

/**
 * @brief Plans the export of the folder `folder_key`: its files, then every project file they reference (asset
 * ids, asset paths, the files UI documents link, the files an import writes beside its source) and the scripts that
 * define the script types its scenes and prefabs use, followed until nothing new is reached. Engine assets are
 * listed, not added.
 */
auto plan_export(const export_source& source, const std::string& folder_key) -> export_plan;

/**
 * @brief Writes the package: the included entries with their .meta files, the manifest made from `definition`
 * with every file's hash, and the README.md, LICENSE and CHANGELOG.md of the folder when it has them.
 * @param written Receives the manifest as written.
 */
auto export_package(const export_plan& plan,
                    const package_manifest& definition,
                    const fs::path& output_file,
                    std::string& err,
                    package_manifest* written = nullptr) -> bool;

/// The type names a C# compiler's messages say it did not find: what scripts use that the package leaves out.
auto find_missing_type_names(const std::vector<std::string>& messages) -> std::vector<std::string>;

/**
 * @brief Adds to the plan the project scripts that declare a type (class, struct, enum, interface, record or
 * delegate) of one of `type_names` and are not in it yet, as scripts in use.
 * @return The keys of the scripts added.
 */
auto add_scripts_declaring(const export_source& source, const std::vector<std::string>& type_names, export_plan& plan)
    -> std::vector<std::string>;

/// A compiler message as a person reads it: "Game.cs line 12: The type ... could not be found."
auto format_compiler_message(const std::string& file, int line, const std::string& message) -> std::string;

/// The data-folder path of a project asset key ("app:/data/UIKit/a.png" -> "UIKit/a.png"); empty for another key.
auto get_data_path(const std::string& key) -> std::string;
/// The project asset key of a data-folder path.
auto get_data_key(const std::string& path) -> std::string;

auto get_export_reason_name(export_reason reason) -> const char*;

} // namespace unravel::packages
