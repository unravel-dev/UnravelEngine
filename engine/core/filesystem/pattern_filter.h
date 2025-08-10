#pragma once

#include <string>
#include <vector>
#include <functional>
#include "filesystem.h"

namespace fs
{

/// <summary>
/// A wildcard pattern matcher that supports * (match any sequence) and ? (match single character)
/// </summary>
class wildcard_pattern
{
public:
    /// <summary>
    /// Constructs a wildcard pattern from a string
    /// </summary>
    /// <param name="pattern">Pattern string with * and ? wildcards</param>
    explicit wildcard_pattern(const std::string& pattern);

    /// <summary>
    /// Tests if the given string matches this pattern
    /// </summary>
    /// <param name="str">String to test against the pattern</param>
    /// <returns>True if the string matches the pattern</returns>
    auto matches(const std::string& str) const -> bool;

    /// <summary>
    /// Gets the original pattern string
    /// </summary>
    auto get_pattern() const -> const std::string& { return pattern_; }

private:
    std::string pattern_;
    
    /// <summary>
    /// Internal matching implementation using recursive approach
    /// </summary>
    auto match_impl(const char* pattern, const char* str) const -> bool;
};

/// <summary>
/// A filter that combines include and exclude patterns for file/directory filtering
/// </summary>
class pattern_filter
{
public:
    /// <summary>
    /// Default constructor creates a filter that accepts everything
    /// </summary>
    pattern_filter() = default;

    /// <summary>
    /// Constructs a filter with include patterns only (for backward compatibility)
    /// </summary>
    /// <param name="include_pattern">Single include pattern</param>
    explicit pattern_filter(const std::string& include_pattern);

    /// <summary>
    /// Constructs a filter with multiple include and exclude patterns
    /// </summary>
    /// <param name="include_patterns">List of include patterns (OR logic)</param>
    /// <param name="exclude_patterns">List of exclude patterns (NOT logic)</param>
    pattern_filter(const std::vector<std::string>& include_patterns,
                   const std::vector<std::string>& exclude_patterns = {});

    /// <summary>
    /// Adds an include pattern to the filter
    /// </summary>
    /// <param name="pattern">Pattern to add to includes</param>
    void add_include_pattern(const std::string& pattern);

    /// <summary>
    /// Adds an exclude pattern to the filter
    /// </summary>
    /// <param name="pattern">Pattern to add to excludes</param>
    void add_exclude_pattern(const std::string& pattern);

    /// <summary>
    /// Tests if a path should be included based on the filter rules
    /// Logic: (matches any include pattern OR no include patterns) AND (matches no exclude patterns)
    /// </summary>
    /// <param name="path">Path to test</param>
    /// <returns>True if the path should be included</returns>
    auto should_include(const fs::path& path) const -> bool;

    /// <summary>
    /// Tests if a filename should be included based on the filter rules
    /// </summary>
    /// <param name="filename">Filename to test</param>
    /// <returns>True if the filename should be included</returns>
    auto should_include_filename(const std::string& filename) const -> bool;

    /// <summary>
    /// Checks if this filter has any patterns
    /// </summary>
    auto has_patterns() const -> bool;

    /// <summary>
    /// Checks if this filter is effectively a wildcard (no restrictions)
    /// </summary>
    auto is_wildcard() const -> bool;

    /// <summary>
    /// Gets all include patterns
    /// </summary>
    auto get_include_patterns() const -> const std::vector<wildcard_pattern>&;

    /// <summary>
    /// Gets all exclude patterns
    /// </summary>
    auto get_exclude_patterns() const -> const std::vector<wildcard_pattern>&;

private:
    std::vector<wildcard_pattern> include_patterns_;
    std::vector<wildcard_pattern> exclude_patterns_;
};

/// <summary>
/// Convenience function to create a pattern filter from a single wildcard string
/// Maintains backward compatibility with existing "*" usage
/// </summary>
/// <param name="pattern">Single pattern string</param>
/// <returns>Pattern filter configured with the single include pattern</returns>
auto make_pattern_filter(const std::string& pattern) -> pattern_filter;

/// <summary>
/// Convenience function to create a pattern filter with include and exclude lists
/// </summary>
/// <param name="includes">Include patterns (OR logic)</param>
/// <param name="excludes">Exclude patterns (NOT logic)</param>
/// <returns>Pattern filter configured with the specified patterns</returns>
auto make_pattern_filter(const std::vector<std::string>& includes,
                         const std::vector<std::string>& excludes = {}) -> pattern_filter;

} // namespace fs 