#pragma once

#include <filesystem/filesystem.h>

#include <hpp/string_view.hpp>

#include <cstdint>
#include <string>

namespace unravel::update_check
{

/// What the update check of one platform keeps between runs of the editor. No identifier and no counter.
struct update_state
{
    /// When the check last asked GitHub, in seconds since the epoch; 0: never.
    std::int64_t last_attempt = 0;
    /// When an attempt last read a manifest, in seconds since the epoch; 0: never.
    std::int64_t last_answer = 0;
    /// The version of this platform's build in that manifest ("1.2.0-413"); empty when it listed none.
    std::string newest_version;
    /// The release that holds that build ("v1.2.0").
    std::string newest_tag;
    /// The version the user dismissed: the notice stays away for this build, not for the next one.
    std::string dismissed_version;

    friend auto operator==(const update_state& a, const update_state& b) -> bool = default;
};

/// Where the state of `platform` is kept: update_check_<platform>.cfg beside the editor settings. Each platform has
/// its own, as editors built for different platforms can share a computer and the newest build differs between
/// them; editors of the same platform share it.
auto get_update_state_path(hpp::string_view platform) -> fs::path;

/// The state as the text of its file.
auto write_update_state(const update_state& state) -> std::string;
/// The state in the text of its file; the default state when the text holds none.
auto read_update_state(const std::string& text) -> update_state;

/// The state kept in `path`; the default state when the file is missing or unreadable.
auto load_update_state(const fs::path& path) -> update_state;
/// Replaces the file at `path` with `state` in one step. False when it could not be written.
auto save_update_state(const fs::path& path, const update_state& state) -> bool;

} // namespace unravel::update_check
