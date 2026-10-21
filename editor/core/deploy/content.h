#pragma once

#include "deploy_settings.h"

#include <engine/assets/asset_storage.h>
#include <engine/settings/settings.h>
#include <filesystem/filesystem.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace unravel
{
class asset_manager;
}

/// What a deploy ships of the project's assets: the walk from the scenes, and the include / exclude rules
/// over it. Engine assets always ship whole.
namespace unravel::deployment
{

/// Why a project asset ships with the game, or why it does not.
enum class content_reason : std::uint8_t
{
    /// Nothing the game loads uses it.
    unused,
    /// One of the deploy's scenes.
    scene,
    /// A shipped asset (or the project settings) references it.
    referenced,
    /// A script names its path.
    named_in_script,
    /// An include rule, on it or on a folder holding it.
    included,
    /// An exclude rule, on it or on a folder holding it.
    excluded,
};

/// The rule a folder or asset key is under: its own, or the closest folder's above it.
enum class content_rule : std::uint8_t
{
    none,
    include,
    exclude,
};

/// Which assets a listing shows.
enum class content_filter : std::uint8_t
{
    all,
    shipped,
    left_out,
    /// Under an include or exclude rule.
    changed,
};

/// A project asset as the deploy's content selection sees it.
struct content_entry
{
    /// The asset's key, such as "app:/data/textures/wall.png".
    std::string key;
    /// Whether the walk from the scenes reached it, whatever the rules say.
    bool is_used{};
    /// How the walk reached it; unused when it did not.
    content_reason use{content_reason::unused};
    /// The scene, asset, script or settings file that led the walk to it; empty for a deploy scene.
    std::string used_by;
    /// Bytes of its compiled files.
    std::uint64_t size{};
};

/// A folder scripts build asset paths in at run time: what they load from there cannot be followed, so
/// the folder is worth an include rule.
struct named_folder
{
    std::string key;
    /// The scripts that build paths in it, by key.
    std::vector<std::string> scripts;
};

/// What the walk from the deploy's scenes found in the project.
struct content
{
    /// Every project asset that can ship, in key order.
    std::vector<content_entry> entries;
    /// Deploy scenes that are no longer in the project.
    std::vector<std::string> missing_scenes;
    /// Folders scripts build asset paths in.
    std::vector<named_folder> named_folders;

    /// The entry of a key, compared the way the file system compares; null when none.
    auto find(const std::string& key) const -> const content_entry*;
    /// Whether some entry lies under the folder `key`.
    auto has_folder(const std::string& key) const -> bool;
    /// Indexes the entries for find; called once they are complete.
    void build_index();

private:
    std::unordered_map<std::string, std::size_t> index_;
};

/// What the walk reads: the project's asset database and its folder. Taken on the main thread, so the
/// walk itself touches no protocol and no asset manager and runs on any thread.
struct content_source
{
    asset_database::database_t database;
    /// The folder of the app protocol (the project).
    fs::path project_root;
    /// The settings the deploy ships, as a document: the handles in them (splash logos and the like) are
    /// reached. Empty: the project's settings file.
    std::string settings_document;
};

/// The open project's source, with `shipped_settings`, the settings its deploy ships (a profile's); main
/// thread.
auto make_project_content_source(asset_manager& am, const settings& shipped_settings) -> content_source;

/**
 * @brief Walks from the deploy's scenes - `startup_scene` and params.deploy_scenes - through what they
 * reference: quoted uids and app:/ paths in text asset files, the files UI documents and style sheets
 * link, the assets an import generates beside its source, the project settings' handles, and the paths
 * the project's scripts name.
 */
auto detect_content(const content_source& source, const deploy_settings& params, const std::string& startup_scene)
    -> content;

/// Every asset that can ship, without the walk: what a deploy needs when only exclude rules apply.
auto list_content(const content_source& source) -> content;

auto find_rule(const deploy_settings& params, const std::string& key) -> content_rule;
/// Whether a rule names `key` itself.
auto has_own_rule(const deploy_settings& params, const std::string& key) -> bool;
/// Whether an asset ships: by its rule, else by use when only used assets ship, else always.
auto is_shipped(const deploy_settings& params, const content_entry& entry) -> bool;
/// Why an asset ships or not: its rule first, then its use.
auto get_reason(const deploy_settings& params, const content_entry& entry) -> content_reason;
auto passes_filter(const deploy_settings& params, const content_entry& entry, content_filter filter) -> bool;

/**
 * @brief Makes `key` (a folder or an asset) ship or not, with everything under it.
 *
 * Rules under it are dropped, and a rule on it is kept only where use alone would decide otherwise.
 */
void set_shipped(deploy_settings& params, const content& content, const std::string& key, bool is_shipped);

/// Drops the rules on `key` and under it, so use alone decides again.
void reset_rules(deploy_settings& params, const std::string& key);

/// Whether a deploy can leave assets out: only used assets ship, or a rule excludes something.
auto is_filtered(const deploy_settings& params) -> bool;
/// Whether what ships depends on the walk (only used assets ship), not just on the rules.
auto needs_walk(const deploy_settings& params) -> bool;

/// The path keys (fs::make_path_key) of the assets that do not ship.
auto get_left_out_keys(const deploy_settings& params, const content& content) -> std::unordered_set<std::string>;

/// Rules on keys that name no asset and no folder of the content.
auto find_orphan_rules(const deploy_settings& params, const content& content) -> std::vector<std::string>;

/**
 * @brief Tidies the scene list and the rules: keys normalized and spelled as the project spells them,
 * repeats dropped.
 * @return The keys that name no asset and no folder of the content.
 */
auto normalize_keys(deploy_settings& params, const content& content) -> std::vector<std::string>;

auto get_reason_name(content_reason reason) -> const char*;
auto get_filter_name(content_filter filter) -> const char*;
/// The filter a name means; false for a name that is none.
auto parse_filter(const std::string& name, content_filter& filter) -> bool;

} // namespace unravel::deployment
