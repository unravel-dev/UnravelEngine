#pragma once

#include "update_manifest.h"
#include "update_state.h"

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <chrono>
#include <cstdint>
#include <string>

namespace unravel::update_check
{

/// The file the release workflow attaches to every release; GitHub serves the latest release's copy here. It is a
/// release download, not a request to the GitHub API, so the API's limit per address does not apply.
inline constexpr const char* MANIFEST_URL =
    "https://github.com/unravel-dev/UnravelEngine/releases/latest/download/latest.json";
/// A release's page; the tag follows.
inline constexpr const char* RELEASE_PAGE_URL_PREFIX = "https://github.com/unravel-dev/UnravelEngine/releases/tag/";
/// The automatic check asks GitHub at most this often.
constexpr std::chrono::hours CHECK_INTERVAL{6};
/// After an attempt that read no manifest, the automatic check asks again this much later.
constexpr std::chrono::hours RETRY_INTERVAL{1};

/// What one attempt found.
struct check_outcome
{
    /// Whether a manifest was read.
    bool has_answer = false;
    /// This platform's build in that manifest, when it lists one.
    hpp::optional<release_build> build;
    /// Why no manifest was read.
    std::string error;
};

/// The platform part of this build's package name, which latest.json lists builds by.
auto get_own_platform() -> hpp::string_view;
/// The version this editor was built as; nothing for a build from sources without their tags, which has no
/// version to compare.
auto get_own_version() -> hpp::optional<version::engine_version>;

/// Seconds since the epoch, as the state keeps times.
auto to_epoch_seconds(std::chrono::system_clock::time_point time) -> std::int64_t;

/**
 * @brief Whether the automatic check asks GitHub at `now`.
 *
 * It does when it never asked, CHECK_INTERVAL after an attempt that read a manifest, RETRY_INTERVAL after one that
 * read none, and when the last attempt lies in the future (the clock was set back).
 */
auto is_check_due(const update_state& state, std::chrono::system_clock::time_point now) -> bool;

/// Records an attempt at `now` and, when it read a manifest, the build it found for this platform.
void record_outcome(update_state& state, const check_outcome& outcome, std::chrono::system_clock::time_point now);

/**
 * @brief The build to announce: the newest build the state knows of, when it is newer than `own`.
 *
 * The version the user dismissed is announced only when `is_manual` (the user asked).
 */
auto get_build_to_announce(const update_state& state, const version::engine_version& own, bool is_manual)
    -> hpp::optional<release_build>;

/// The page of the release with `tag`. The tag is a release tag (read_update_manifest checked it), so no address is
/// taken from the downloaded file.
auto get_release_page_url(const std::string& tag) -> std::string;

} // namespace unravel::update_check
