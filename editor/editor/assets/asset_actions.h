#pragma once

#include <context/context.hpp>
#include <engine/assets/asset_handle.h>
#include <filesystem/filesystem.h>

#include <string>

namespace unravel
{
struct physics_material;
}

namespace unravel::asset_actions
{

/**
 * @brief Creates a physics material asset with default values at `absolute_path` and saves it.
 *        The content browser's Create menu and MCP both create materials through this.
 * @return The new asset's handle; empty when it could not be saved.
 */
auto create_physics_material(rtti::context& ctx, const fs::path& absolute_path) -> asset_handle<physics_material>;

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

} // namespace unravel::asset_actions
