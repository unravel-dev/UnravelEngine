#pragma once

#include "sample_manifest.h"

#include <filesystem/filesystem.h>
#include <https/https_get.h>

#include <hpp/optional.hpp>

#include <string>
#include <vector>

namespace unravel::sample_catalog
{

/// The file the sample catalog workflow attaches to the latest release; GitHub serves the latest release's copy
/// here. It is a release download, not a request to the GitHub API, so the API's limit per address does not apply.
inline constexpr const char* MANIFEST_URL = UNRAVEL_GITHUB_LATEST_DOWNLOAD_URL "samples.json";

/// What one fetch found.
struct fetch_outcome
{
    /// The samples.json it read; nothing when it read none, or one that lists no sample this editor can show.
    hpp::optional<sample_manifest> manifest;
    /// Why no samples.json was read.
    std::string error;
    /// The files of the read samples.json that could not be fetched or kept, one line each: while there is any,
    /// the attempt is not complete.
    std::vector<std::string> file_errors;
};

/**
 * @brief Fetches the pictures of `manifest` that `dir` lacks into `dir`, then removes the ones it no longer names.
 *
 * Each picture is read at its commit from the sample's repository, up to MAX_THUMBNAIL_SIZE, and kept only when it
 * is a JPEG or a PNG file.
 *
 * @return The pictures that could not be fetched, one line each.
 */
auto fetch_missing_thumbnails(const sample_manifest& manifest, const fs::path& dir, const https::stop_check& should_stop)
    -> std::vector<std::string>;

/**
 * @brief Fetches samples.json from MANIFEST_URL, keeps it as get_cached_manifest_path(), and fetches the pictures
 * of its samples into get_thumbnails_dir().
 *
 * A samples.json that lists no sample this editor can show is no answer: it is neither kept nor listed, and the
 * pictures stay.
 *
 * Blocks the calling thread for up to the requests' time limits; run it off the frame. `should_stop` ends it
 * earlier. It does not throw: any failure is an outcome without a manifest.
 */
auto fetch_samples(const https::stop_check& should_stop) -> fetch_outcome;

} // namespace unravel::sample_catalog
