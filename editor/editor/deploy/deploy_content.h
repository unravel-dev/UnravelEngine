#pragma once

#include "deploy.h"

#include <filesystem/filesystem.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace unravel
{
class asset_manager;

/// Whether the asset at `location` is a script source: scripts ship as their library, not as assets.
auto is_deploy_script_location(const std::string& location) -> bool;

/// The compiled files a deployed game reads for the asset at `location`: one per renderer of the
/// platform for a shader, none for scripts and for assets that live outside a data folder (the
/// embedded primitives).
auto get_deploy_compiled_paths(const std::string& location) -> std::vector<fs::path>;

/// The asset key ("app:/data/a/b.png") of a file in a protocol's compiled folder, from its path
/// relative to that folder ("a/b.png.asset").
auto get_deploy_source_key(const std::string& protocol, const fs::path& compiled_relative) -> std::string;

/// Why a project asset ships with the game, or why it does not.
enum class deploy_content_reason : std::uint8_t
{
    /// Nothing the game loads uses it.
    unused,
    /// One of the deploy's scenes.
    scene,
    /// A shipped asset references it.
    referenced,
    /// A script names its path.
    named_in_script,
    /// An include rule, on it or on a folder holding it.
    included,
    /// An exclude rule, on it or on a folder holding it.
    excluded,
};

/// The rule a folder or asset path is under: its own, or the closest folder's above it.
enum class deploy_content_rule : std::uint8_t
{
    none,
    include,
    exclude,
};

/// A project asset as the deploy's content selection sees it.
struct deploy_content_entry
{
    /// The asset's key, such as "app:/data/textures/wall.png".
    std::string key;
    /// Whether the walk from the scenes reached it, whatever the rules say.
    bool is_used{};
    /// How the walk reached it; unused when it did not.
    deploy_content_reason use{deploy_content_reason::unused};
    /// The scene, asset or script that led the walk to it; empty for a scene of the deploy.
    std::string used_by;
    /// Bytes of its compiled files.
    std::uint64_t size{};
};

/// What the walk from the deploy's scenes found in the project.
struct deploy_content
{
    /// Every project asset that can ship, in key order.
    std::vector<deploy_content_entry> entries;
    /// Deploy scenes that are no longer in the project.
    std::vector<std::string> missing_scenes;
    /// Folders scripts name: what they load from there cannot be followed, so the folders are
    /// worth an include rule.
    std::vector<std::string> named_folders;
};

/// The rule that applies to `key`: the most specific of the include and exclude rules on it or a
/// folder above it. An exclude wins over an include on the same path.
auto find_deploy_content_rule(const deploy_settings& params, const std::string& key) -> deploy_content_rule;

/// Whether an asset ships: by its rule, else by use when only used assets ship, else always.
auto is_deploy_content_shipped(const deploy_settings& params, const deploy_content_entry& entry) -> bool;

/// Why an asset ships or not, as the Deploy window shows it: its rule first, then its use.
auto get_deploy_content_reason(const deploy_settings& params, const deploy_content_entry& entry)
    -> deploy_content_reason;

/// Makes `key` (a folder or an asset) ship or not, for itself and everything under it: rules under
/// it are dropped, and a rule on it is kept only where the scenes' use alone would decide otherwise.
void set_deploy_content_shipped(deploy_settings& params,
                                const deploy_content& content,
                                const std::string& key,
                                bool is_shipped);

/// Drops the rules on `key` and under it, so use alone decides again.
void reset_deploy_content_rules(deploy_settings& params, const std::string& key);

/// Whether the deploy leaves anything out, and so has to walk the scenes: only used assets ship, or
/// a rule excludes something.
auto is_deploy_content_filtered(const deploy_settings& params) -> bool;

/// The keys of the assets that do not ship, the rules and the scenes' use applied.
auto get_deploy_left_out_keys(const deploy_settings& params, const deploy_content& content)
    -> std::unordered_set<std::string>;

/**
 * @brief Walks from the deploy's scenes - `startup_scene` and params.deploy_scenes - through what
 * they reference: asset handles in scenes, prefabs, materials and other asset files, the files UI
 * documents link, and asset paths written into them or into the project's scripts.
 *
 * Reads the project's source files; safe off the main thread.
 */
auto detect_deploy_content(asset_manager& am, const deploy_settings& params, const std::string& startup_scene)
    -> deploy_content;

} // namespace unravel
