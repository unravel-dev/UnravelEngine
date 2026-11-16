#pragma once

#include "sample_manifest.h"

#include <filesystem/filesystem.h>

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <functional>
#include <string>
#include <vector>

/// The sample catalog's files on this computer: the cache of what was fetched, the pictures, and the copies of the
/// samples among the user's projects.
namespace unravel::sample_catalog
{

/// The largest picture that is fetched for a sample. A 16:9 JPEG of 800 x 450 is under 100 KB.
constexpr std::size_t MAX_THUMBNAIL_SIZE = 1024 * 1024;
/// How many hex digits of the picture's commit its cached copy's name carries.
constexpr std::size_t THUMBNAIL_COMMIT_DIGITS = 12;

/// Where the editor keeps the samples.json it fetched last, the pictures of its samples and the fetch state: in
/// the editor settings folder, shared by every editor on the computer.
auto get_cache_dir() -> fs::path;
/// The samples.json the editor fetched last, in get_cache_dir().
auto get_cached_manifest_path() -> fs::path;
/// The fetch state, in get_cache_dir().
auto get_fetch_state_path() -> fs::path;
/// The fetched pictures, in get_cache_dir().
auto get_thumbnails_dir() -> fs::path;

/// The name of a fetched picture: the repository, the start of the picture's commit and the picture's extension,
/// "UnravelEngine-Sample-2048-0123456789ab.jpg".
auto get_thumbnail_file_name(const sample_entry& sample) -> std::string;
/// The name of the picture the editor ships for a sample: the repository and the picture's extension,
/// "UnravelEngine-Sample-2048.jpg".
auto get_shipped_thumbnail_file_name(const sample_entry& sample) -> std::string;

/// Whether `data` starts as a JPEG or a PNG file does.
auto is_picture_data(hpp::string_view data) -> bool;

/// The samples.json in `path`; nothing when the file is missing, larger than MAX_MANIFEST_SIZE or no manifest.
auto load_manifest_file(const fs::path& path) -> hpp::optional<sample_manifest>;

/**
 * @brief The manifest the start page lists: the shipped one, replaced by the fetched one when
 * is_replacement_manifest says so; an empty manifest when there is neither.
 */
auto choose_manifest(hpp::optional<sample_manifest> shipped, hpp::optional<sample_manifest> fetched) -> sample_manifest;

/// Removes the pictures in `dir` that `manifest` no longer names. Only files named as fetched pictures are touched.
void remove_unlisted_thumbnails(const fs::path& dir, const sample_manifest& manifest);

/// The sample.json of the copy of a sample in `copy_dir`.
auto get_copy_sample_json_path(const fs::path& copy_dir) -> fs::path;
/// The version of the copy of a sample in `copy_dir`, from its sample.json (read_sample_version); nothing when the
/// copy has no readable sample.json, as one made before the sample had it.
auto load_copy_version(const fs::path& copy_dir) -> hpp::optional<std::uint32_t>;

/// How find_sample_copy looks at a project folder; given by the caller, which can cache what it read.
struct project_lookup
{
    /// The project_guid in the folder's project.cfg; empty when it has none.
    std::function<std::string(const fs::path&)> read_project_guid;
    /// Whether the folder is there.
    std::function<bool(const fs::path&)> is_folder;
};

/**
 * @brief The project in `projects` that is a copy of the sample, or an empty path.
 *
 * A folder with the repository's name (a clone) is one, case aside. A folder whose name is the repository's
 * followed by a dash and more (the folder a ZIP of a branch unpacks to, "UnravelEngine-Sample-2048-main") is one
 * when its project.cfg has the sample's `project_guid`. A project under any other name is not, even with that
 * project_guid: it is a game made from the sample. Only folders named so are read.
 */
auto find_sample_copy(hpp::string_view repository,
                      hpp::string_view project_guid,
                      const std::vector<fs::path>& projects,
                      const project_lookup& lookup) -> fs::path;

} // namespace unravel::sample_catalog
