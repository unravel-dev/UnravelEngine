#pragma once

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

/// The key a cache folder's sweep knows a file by, from its path relative to the folder.
auto get_cache_file_key(const fs::path& relative) -> std::string;

/// Keeps a cache file and its manifest from the sweep.
void keep_cache_file(std::unordered_set<std::string>& kept, const std::string& file_key);

/**
 * @brief Cooks every key: `cook(i)` for keys[i], on the background pool, since a compile can hold every thread it
 * gets for seconds; the largest sources first.
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
