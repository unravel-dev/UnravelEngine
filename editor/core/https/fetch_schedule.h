#pragma once

#include <filesystem/filesystem.h>

#include <hpp/string_view.hpp>

#include <chrono>
#include <cstdint>

/// What the editor's online checks (the update check, the sample catalog) share: when a check asks again, and how
/// it keeps what it fetched and its state.
namespace unravel::https
{

/// Seconds since the epoch, as the checks keep times.
auto get_epoch_seconds(std::chrono::system_clock::time_point time) -> std::int64_t;

/**
 * @brief Whether a check asks again at `now`.
 *
 * @param last_attempt When it last asked, in seconds since the epoch; 0: never.
 * @param last_answer When an attempt last got an answer, in seconds since the epoch; 0: never.
 * @return True when it never asked, when the last attempt lies in the future (the clock was set back), `interval`
 * after an attempt that got an answer and `retry_interval` after one that got none.
 */
auto is_fetch_due(std::int64_t last_attempt,
                  std::int64_t last_answer,
                  std::chrono::system_clock::time_point now,
                  std::chrono::seconds interval,
                  std::chrono::seconds retry_interval) -> bool;

/// Replaces the file at `path` with `data` in one step, making its folder when needed. False when it could not be
/// written.
auto write_file_atomically(const fs::path& path, hpp::string_view data) -> bool;

} // namespace unravel::https
