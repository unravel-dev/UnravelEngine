#pragma once

#include "keys.h"

#include <engine/assets/asset_storage.h>
#include <filesystem/filesystem.h>

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

/// What the texture, audio and shader cooks share: each brings a folder of the profile's cache in step with the
/// assets a deploy ships, compiling on the background pool what is not current there and deleting what nothing needs.
namespace unravel::deployment
{

/// What cooking one asset did.
struct cook_outcome
{
    /// The keys (get_cache_file_key) of the files it ships from the cache folder; none when it ships the editor's
    /// own compiled file.
    std::vector<std::string> files;
    /// Of those, the ones compiled because none was current.
    std::size_t compiled{};
    bool has_failed{};
};

/// What cooking did, for the log and the tests.
struct cook_report
{
    /// Files of the cache folder the deploy ships.
    std::size_t variants{};
    /// Of those, the ones compiled because none was current.
    std::size_t compiled{};
    /// Files of the cache folder deleted because nothing needed them.
    std::size_t removed{};
    /// Files of the cache folder nothing needs that could not be deleted: a deploy would ship them.
    std::vector<fs::path> undeletable;
    /// The keys of the assets that did not compile.
    std::vector<std::string> failed;
};

/// How a deploy step names what it cooks, in its log line and its errors.
struct cook_wording
{
    /// The assets, as "texture(s)".
    const char* assets;
    /// The files it ships from the cache, as "texture variant(s)".
    const char* files;
    /// The cache folder, as "texture cache".
    const char* cache;
};

/// One protocol's assets of one kind, by whether the deploy ships them.
struct asset_split
{
    /// The keys ("app:/data/...", "engine:/data/...") of the assets the deploy ships.
    std::vector<std::string> shipped;
    /// The keys of the assets the content selection leaves out: their cooked files are kept for a later deploy,
    /// which checks them again before shipping them.
    std::vector<std::string> left_out;
};

/// Whether the asset at `key` is of the kind a cook takes (is_asset_key).
using asset_kind_filter = std::function<bool(const std::string& key)>;

/// The assets of `database` that `is_kind` takes, by whether `left_out` names them.
auto split_assets(const asset_database::database_t& database, const asset_kind_filter& is_kind, const left_out_assets& left_out)
    -> asset_split;

/// How one asset compiles for the profile and for the editor, as cook_variant asks it.
struct variant_compiler
{
    /// Whether the profile compiles the asset differently from the editor.
    bool needs_variant{};
    /// Whether `file` holds the asset as the editor compiles it, from its current source and settings.
    std::function<bool(const fs::path& file)> is_editor_compiled;
    /// Whether `file` holds the asset as the profile compiles it, from its current source and settings.
    std::function<bool(const fs::path& file)> is_profile_compiled;
    /// Compiles the asset for the profile into `file`; whether it did.
    std::function<bool(const fs::path& file)> compile;
};

/**
 * @brief Cooks the asset at `key` by the rule every kind with one compiled file shares: the editor's file in
 * `compiled` ships where the profile compiles the asset as the editor does and that file is current (settings changed
 * while the editor was closed leave it compiled with the old ones, and a deploy never ships those); otherwise its
 * variant in `variants` ships, compiled first when it is not current.
 */
auto cook_variant(const std::string& key, const fs::path& compiled, const fs::path& variants, const variant_compiler& compiler)
    -> cook_outcome;

/// Cooks the shipped asset at `key` (cook_variant).
using asset_cook = std::function<cook_outcome(const std::string& key)>;
/// Whether the profile compiles the asset at `key` differently from the editor.
using variant_need = std::function<bool(const std::string& key)>;

/**
 * @brief Brings the cache folder `variants` in step with `assets`: `cook` for every shipped asset on the background
 * pool, the variant of a left-out asset kept where `needs_variant` (a later deploy checks it before shipping it), and
 * every other file of the folder deleted, so it never holds a variant that could ship stale.
 */
auto cook_variants(const asset_split& assets, const fs::path& variants, const asset_cook& cook, const variant_need& needs_variant)
    -> cook_report;

/// The key a cache folder's sweep knows a file by, from its path relative to the folder.
auto get_cache_file_key(const fs::path& relative) -> std::string;

/// Keeps a cache file and its manifest from the sweep.
void keep_cache_file(std::unordered_set<std::string>& kept, const std::string& file_key);

/**
 * @brief Cooks every key: `cook(i)` for keys[i], on the background pool, since a compile can hold every thread it
 * gets for seconds; the largest sources first. A cancelled deploy cooks no further key, and the call throws.
 * @return The outcomes, by the keys' order.
 */
auto cook_all(const std::vector<std::string>& keys, const std::function<cook_outcome(std::size_t index)>& cook)
    -> std::vector<cook_outcome>;

/// Adds what cooking `key` did to the report, and the files it ships to `kept`.
void add_outcome(const std::string& key, const cook_outcome& outcome, std::unordered_set<std::string>& kept, cook_report& report);

/// Deletes every file of `folder` that `kept` does not name, and adds what it did to the report.
void sweep_cache_folder(const fs::path& folder, const std::unordered_set<std::string>& kept, cook_report& report);

/// Logs what a deploy step's cook did; throws with the assets that did not compile, or with the files of the cache
/// it could not delete.
void report_cook_or_throw(const std::string& profile, const cook_report& report, const cook_wording& wording);

} // namespace unravel::deployment
