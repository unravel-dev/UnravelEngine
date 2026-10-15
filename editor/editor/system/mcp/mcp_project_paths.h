#pragma once

#include <context/context.hpp>

#include <string>
#include <vector>

namespace unravel::mcp
{

/// The folder every MCP write goes to: the open project's data folder.
inline constexpr const char* k_project_data_root = "app:/data";

/**
 * @brief The project data key a tool argument names, for a tool that writes there.
 *
 * Accepts an "app:/data/..." key, a path relative to app:/data ("Props/Crate.mat") or an
 * absolute path inside the project's data folder. Refuses other protocols, ".." segments and
 * anything that resolves outside app:/data, links included. Backslashes become '/', empty and
 * "." segments and trailing separators are dropped.
 *
 * @param input The argument as the client passed it.
 * @param key Receives the normalized "app:/data/..." key.
 * @param error Receives one sentence naming the rule the input broke.
 * @return False when the input names no writable project location.
 */
auto to_project_data_key(rtti::context& ctx, const std::string& input, std::string& key, std::string& error) -> bool;

/**
 * @brief to_project_data_key for a file of one of `extensions` (".mat", ...): an input without an
 *        extension gets the first one, an input with another extension is refused.
 */
auto to_project_data_file_key(rtti::context& ctx,
                              const std::string& input,
                              const std::vector<std::string>& extensions,
                              std::string& key,
                              std::string& error) -> bool;

/**
 * @brief An asset key from a tool argument that reads an asset: backslashes become '/', an absolute
 *        path inside a mounted folder (project, engine, editor) becomes its protocol key, and a path
 *        without a protocol is taken relative to app:/data ("Mats/Red" -> "app:/data/Mats/Red"). A
 *        uid, or anything else, is returned unchanged for the asset lookup to accept or refuse.
 */
auto to_asset_key(const std::string& input) -> std::string;

/**
 * @brief The key of a file a tool reads: a key in any mounted folder (app:/, engine:/, editor:/),
 *        an absolute path inside one, or a path relative to app:/data - without ".." and with one
 *        of `extensions`. Keeps reads inside the editor's own folders instead of any file on disk.
 */
auto to_readable_file_key(const std::string& input,
                          const std::vector<std::string>& extensions,
                          std::string& key,
                          std::string& error) -> bool;

/**
 * @brief Refuses a write to an asset that lives outside the project's data folder, such as an
 *        engine or editor asset every project shares.
 * @param key The asset's key, as its handle reports it.
 */
auto require_project_data_asset(const std::string& key, std::string& error) -> bool;

/**
 * @brief True when `name` can be one file name inside a folder: not empty, no separators, no
 *        protocol and not "." or "..". For names that come from the scene, such as an entity's.
 */
auto is_plain_file_name(const std::string& name) -> bool;

} // namespace unravel::mcp
