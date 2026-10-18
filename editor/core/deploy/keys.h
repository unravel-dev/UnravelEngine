#pragma once

#include <string>

/// Asset and folder keys as the deploy's rules, scene lists and content compare them.
namespace unravel::deployment
{

/// The protocol whose content a deploy selects; engine assets always ship whole.
constexpr const char* CONTENT_PROTOCOL = "app";
/// The protocol of the engine's own assets and runtime.
constexpr const char* ENGINE_PROTOCOL = "engine";
/// The project settings the game loads first: its startup scene and splash logos are handles there.
constexpr const char* PROJECT_SETTINGS_KEY = "app:/settings/settings.cfg";

/// A key as rules and scene lists store it: '/' separated, no trailing '/'.
auto normalize_key(std::string key) -> std::string;

/// Whether two keys name the same asset or folder: letter case is ignored where the file system ignores
/// it, as the game's lookups do.
auto is_same_key(const std::string& lhs, const std::string& rhs) -> bool;

/// Whether `key` is `root` itself or lies under the folder `root`; "app:/data/a" does not cover
/// "app:/data/ab". Both are normalized first.
auto is_key_under(const std::string& key, const std::string& root) -> bool;

/// The key without its file extension ("app:/data/levels/one.spfb" -> "app:/data/levels/one"), the
/// way the asset manager resolves a key given without one.
auto get_key_without_extension(const std::string& key) -> std::string;

/// The key's last part: a file or folder name.
auto get_key_name(const std::string& key) -> std::string;

/// A key as the content browser shows it: under the data folder, without the protocol.
auto get_display_path(const std::string& key) -> std::string;

} // namespace unravel::deployment
