#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace version
{

/// Structured representation of the engine version string scheme used across
/// the project: "major.minor.patch[-commit_count[-sha]]" (e.g. "1.0.0-66-gc83fa23").
/// Parsing tolerates an optional leading "v"/"V".
///
/// Equality and ordering compare only (major, minor, patch, commit_count).
/// The SHA is metadata and is intentionally *not* part of the ordering.
struct engine_version
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    /// Commits since the tagged release (e.g. the `66` in `1.0.0-66-g...`).
    int commit_count = 0;
    /// Git SHA suffix (including any leading 'g'), or empty if absent.
    std::string sha;
    /// The original string this was parsed from. For logging/UI only; never
    /// used for comparisons.
    std::string original;

    /// Returns the canonical "M.m.p[-commit[-sha]]" string. This is what
    /// should be written to disk, and roundtrips through `parse`.
    auto to_string() const -> std::string;

    friend auto operator==(const engine_version& a, const engine_version& b) -> bool;
    friend auto operator<=>(const engine_version& a, const engine_version& b) -> std::strong_ordering;
};

/// Parses a version string. Returns nullopt on malformed input.
auto parse(std::string_view text) -> std::optional<engine_version>;

/// Three-way compare: returns -1 if a<b, 0 if equal, +1 if a>b.
/// Compatible with the legacy API surface in version_manager.cpp.
auto compare(const engine_version& a, const engine_version& b) -> int;

/// Returns the version the engine was built with (parsed from get_full()).
/// Falls back to a zero-initialized value if get_full() is malformed (should
/// only happen in broken build configurations).
auto get_current() -> engine_version;

auto get_major() -> std::string;
auto get_minor() -> std::string;
auto get_patch() -> std::string;

auto get_full() -> std::string;

} // namespace version
