#pragma once

#include <filesystem/filesystem.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace unravel::sample_catalog
{

/// The automatic fetch asks GitHub at most this often.
constexpr std::chrono::hours FETCH_INTERVAL{24};
/// After an attempt that read no samples.json, or missed a picture it names, the automatic fetch asks again this
/// much later.
constexpr std::chrono::hours RETRY_INTERVAL{1};

/// What the sample fetch keeps between runs of the editor. No identifier and no counter.
struct fetch_state
{
    /// When the fetch last asked GitHub, in seconds since the epoch; 0: never.
    std::int64_t last_attempt = 0;
    /// When an attempt last read a samples.json and every picture it names, in seconds since the epoch; 0: never.
    std::int64_t last_answer = 0;

    friend auto operator==(const fetch_state& a, const fetch_state& b) -> bool = default;
};

/**
 * @brief Whether the automatic fetch asks GitHub at `now`.
 *
 * It does when it never asked, FETCH_INTERVAL after an attempt that read a samples.json and its pictures,
 * RETRY_INTERVAL after one that missed any of them, and when the last attempt lies in the future (the clock was
 * set back).
 */
auto is_fetch_due(const fetch_state& state, std::chrono::system_clock::time_point now) -> bool;

/// Records an attempt at `now`, and whether it read a samples.json and every picture it names.
void record_fetch(fetch_state& state, bool has_answer, std::chrono::system_clock::time_point now);

/// The state as the text of its file.
auto write_fetch_state(const fetch_state& state) -> std::string;
/// The state in the text of its file; the default state when the text holds none.
auto read_fetch_state(const std::string& text) -> fetch_state;

/// The state kept in `path`; the default state when the file is missing or unreadable.
auto load_fetch_state(const fs::path& path) -> fetch_state;
/// Replaces the file at `path` with `state` in one step. False when it could not be written.
auto save_fetch_state(const fs::path& path, const fetch_state& state) -> bool;

} // namespace unravel::sample_catalog
