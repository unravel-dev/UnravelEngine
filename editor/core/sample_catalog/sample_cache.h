#pragma once

#include "sample_manifest.h"

#include <filesystem/filesystem.h>

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <string>

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

/// Replaces the file at `path` with `data` in one step, making its folder when needed. False when it could not be
/// written.
auto write_file_atomically(const fs::path& path, hpp::string_view data) -> bool;

/// Removes the pictures in `dir` that `manifest` no longer names. Only files named as fetched pictures are touched.
void remove_unlisted_thumbnails(const fs::path& dir, const sample_manifest& manifest);

} // namespace unravel::sample_catalog
