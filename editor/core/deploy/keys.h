#pragma once

#include <engine/assets/impl/asset_extensions.h>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <unordered_set>
#include <vector>

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

/// Whether the asset at `key` ("app:/data/a/b.png") is a `T` by its extension (gfx::texture, audio_clip, ...).
template<typename T>
auto is_asset_key(const std::string& key) -> bool
{
    return ex::is_format<T>(fs::path(key).extension().generic_string());
}

/// The assets a deploy leaves out, by key. Keys compare as the file system does (fs::make_path_key), whichever
/// spelling a caller holds.
class left_out_assets
{
public:
    left_out_assets() = default;
    left_out_assets(std::initializer_list<std::string> keys);

    void insert(const std::string& key);
    auto contains(const std::string& key) const -> bool;
    auto size() const -> std::size_t;
    auto empty() const -> bool;

private:
    std::unordered_set<std::string> path_keys_;
};

/// The key's last part: a file or folder name.
auto get_key_name(const std::string& key) -> std::string;

/// A key as the content browser shows it: under the data folder, without the protocol.
auto get_display_path(const std::string& key) -> std::string;

/// Keys for a sentence: the first `shown_max` separated by commas, then "and N more" for the rest.
auto list_keys(const std::vector<std::string>& keys, std::size_t shown_max) -> std::string;

} // namespace unravel::deployment
