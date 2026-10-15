#pragma once

#include <context/context.hpp>
#include <engine/assets/asset_handle.h>
#include <filesystem/filesystem.h>

#include <string>
#include <vector>

namespace unravel
{
struct physics_material;
class material;
}

namespace unravel::asset_actions
{

/**
 * @brief Creates a physics material asset with default values at `absolute_path` and saves it.
 *        The content browser's Create menu and MCP both create materials through this.
 * @return The new asset's handle; empty when it could not be saved.
 */
auto create_physics_material(rtti::context& ctx, const fs::path& absolute_path) -> asset_handle<physics_material>;

/**
 * @brief Creates a PBR material asset with default values at `absolute_path` and saves it.
 *        The content browser's Create menu and MCP both create materials through this.
 * @return The new asset's handle; empty when it could not be saved.
 */
auto create_material(rtti::context& ctx, const fs::path& absolute_path) -> asset_handle<material>;

auto resolve_asset_source_path(const std::string& asset_key) -> fs::path;
auto resolve_asset_source_path(const fs::path& path) -> fs::path;

auto can_reimport(const fs::path& absolute_path) -> bool;

void reimport_path(const fs::path& absolute_path);
void reimport_key(const std::string& asset_key);

template<typename T>
void reimport(const asset_handle<T>& asset)
{
    reimport_key(asset.id());
}

auto is_valid_csharp_identifier(const std::string& name) -> bool;

/// Reads a whole text file. Returns false (with `error` set when given) if it cannot be opened.
auto read_text_file(const fs::path& path, std::string& out, std::string* error = nullptr) -> bool;

/// Writes `content` to `dst` atomically (temp file then rename), creating parent folders. Every
/// editor-side source write (scripts, UI documents, style sheets) goes through this.
auto write_text_file(const fs::path& dst, const std::string& content, std::string* error = nullptr) -> bool;

/// Creates a UI document (.rhtml) at `dst` from the engine's template.
auto create_ui_document(const fs::path& dst, std::string* error = nullptr) -> bool;

/// Creates a style sheet (.rcss) at `dst` from the engine's template.
auto create_style_sheet(const fs::path& dst, std::string* error = nullptr) -> bool;

/**
 * @brief Instantiate a ScriptComponent template (.cs.in), substituting #SCRIPTNAME#
 * with the destination file stem. Uses atomic write when possible.
 */
auto create_script_from_template(const fs::path& template_path,
                                 const fs::path& dst,
                                 std::string* error = nullptr) -> bool;

/// The first free path "<stem> (n)<extension>" in @p directory, n counting from 0.
auto get_free_copy_path(const fs::path& directory, const std::string& stem, const std::string& extension) -> fs::path;

/// Renames a C# script's class along with its file when the class still has the old file name,
/// matching whole words only so identifiers that merely contain the name stay.
void sync_script_class_name(const fs::path& script_path, const std::string& old_stem, const std::string& new_stem);

/**
 * @brief Renames or moves a file or folder inside a watched data folder, as the Content Browser's
 *        rename and drag-and-drop do.
 *
 * The asset watcher sees the rename and moves the .meta with it, so the asset keeps its id and
 * every reference to it. A C# script whose class still has the old file name is renamed along.
 * @return False with @p error set when @p to exists or the move fails.
 */
auto rename_entry(const fs::path& from, const fs::path& to, std::string* error = nullptr) -> bool;

/**
 * @brief Copies a file or folder next to itself under the first free "name (n)" name, as the
 *        Content Browser's Duplicate does.
 *
 * .meta files are not copied: the watcher gives each copy a .meta, and so an id, of its own.
 * @return The copy's path; empty with @p error set when the copy fails.
 */
auto duplicate_entry(const fs::path& absolute_path, std::string* error = nullptr) -> fs::path;

/// The project's trash: <project>/editor/trash, outside the watched data folder.
auto get_trash_directory() -> fs::path;

/**
 * @brief Deletes assets and folders recoverably: moves them, each asset with its .meta, out of the
 *        project's data folder into a new batch folder of the trash.
 *
 * To the asset database the move is a delete. restore_from_trash puts a batch back with the same
 * ids, so references to the assets work again.
 * @param paths Absolute paths inside app:/data.
 * @param batch Receives the batch's name, which restore_from_trash takes.
 */
auto move_to_trash(const std::vector<fs::path>& paths, std::string& batch, std::string* error = nullptr) -> bool;

/**
 * @brief Moves a trash batch back into the data folder, each asset with its .meta.
 *
 * A file whose place in the data folder is taken stays in the batch and is reported in @p error.
 * @param restored Receives the absolute paths moved back.
 */
auto restore_from_trash(const std::string& batch, std::vector<fs::path>& restored, std::string* error = nullptr)
    -> bool;

/// The trash's batches, newest first.
auto list_trash_batches() -> std::vector<std::string>;

} // namespace unravel::asset_actions
