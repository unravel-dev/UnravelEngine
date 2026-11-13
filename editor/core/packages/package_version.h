#pragma once

#include <compare>
#include <optional>
#include <string>
#include <vector>

/// The version a package gives itself, and the ranges a dependency on it is written with.
namespace unravel::packages
{

/// A package version in the form major.minor.patch, with an optional pre-release after a '-' ("1.2.0-preview.1").
/// A pre-release orders before its release.
struct package_version
{
    int major{};
    int minor{};
    int patch{};
    std::string pre_release;

    /// The version written as it is parsed.
    auto to_string() const -> std::string;
    /// The next patch version without a pre-release: what an export suggests after this one.
    auto get_next_patch() const -> package_version;

    friend auto operator==(const package_version& lhs, const package_version& rhs) -> bool;
    friend auto operator<=>(const package_version& lhs, const package_version& rhs) -> std::strong_ordering;
};

/// Parses "1.2.3" or "1.2.3-tag". A leading 'v' is accepted. Empty when the text is no version.
auto parse_version(const std::string& text) -> std::optional<package_version>;

/// One bound of a range.
struct version_bound
{
    enum class kind
    {
        equal,
        greater,
        greater_or_equal,
        less,
        less_or_equal,
    };
    kind relation{kind::equal};
    package_version version;
};

/**
 * @brief The versions a dependency accepts: every bound must hold.
 *
 * Written as bounds separated by spaces (">=1.2.0 <2.0.0"), a caret ("^1.2.0": from it to the next major
 * version), a tilde ("~1.2.0": from it to the next minor version), one version (exactly it), or "*" and the empty
 * text (any version).
 */
struct version_range
{
    std::vector<version_bound> bounds;

    auto contains(const package_version& version) const -> bool;
};

/// Parses a range. Empty when the text is no range.
auto parse_version_range(const std::string& text) -> std::optional<version_range>;

/// Whether `version` lies in `range`, both as text. False when either does not parse.
auto is_in_range(const std::string& version, const std::string& range) -> bool;

} // namespace unravel::packages
