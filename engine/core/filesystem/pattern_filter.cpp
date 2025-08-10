#include "pattern_filter.h"
#include <algorithm>

namespace fs
{

wildcard_pattern::wildcard_pattern(const std::string& pattern)
    : pattern_(pattern)
{
}

auto wildcard_pattern::matches(const std::string& str) const -> bool
{
    return match_impl(pattern_.c_str(), str.c_str());
}

auto wildcard_pattern::match_impl(const char* pattern, const char* str) const -> bool
{
    // Handle empty pattern
    if (*pattern == '\0')
    {
        return *str == '\0';
    }

    // Handle '*' wildcard
    if (*pattern == '*')
    {
        // Skip consecutive '*' characters
        while (*pattern == '*')
        {
            ++pattern;
        }

        // If pattern ends with '*', it matches everything remaining
        if (*pattern == '\0')
        {
            return true;
        }

        // Try matching '*' with empty string, or with one or more characters
        while (*str != '\0')
        {
            if (match_impl(pattern, str))
            {
                return true;
            }
            ++str;
        }

        // Try matching '*' with empty string at end
        return match_impl(pattern, str);
    }

    // Handle '?' wildcard (matches exactly one character)
    if (*pattern == '?' && *str != '\0')
    {
        return match_impl(pattern + 1, str + 1);
    }

    // Handle regular character matching
    if (*pattern == *str)
    {
        return match_impl(pattern + 1, str + 1);
    }

    // No match
    return false;
}

pattern_filter::pattern_filter(const std::string& include_pattern)
{
    if (!include_pattern.empty())
    {
        add_include_pattern(include_pattern);
    }
}

pattern_filter::pattern_filter(const std::vector<std::string>& include_patterns,
                               const std::vector<std::string>& exclude_patterns)
{
    for (const auto& pattern : include_patterns)
    {
        add_include_pattern(pattern);
    }
    for (const auto& pattern : exclude_patterns)
    {
        add_exclude_pattern(pattern);
    }
}

void pattern_filter::add_include_pattern(const std::string& pattern)
{
    if (!pattern.empty())
    {
        include_patterns_.emplace_back(pattern);
    }
}

void pattern_filter::add_exclude_pattern(const std::string& pattern)
{
    if (!pattern.empty())
    {
        exclude_patterns_.emplace_back(pattern);
    }
}

auto pattern_filter::should_include(const fs::path& path) const -> bool
{
    return should_include_filename(path.filename().string());
}

auto pattern_filter::should_include_filename(const std::string& filename) const -> bool
{
    // First check exclude patterns - if any match, exclude the file
    for (const auto& exclude_pattern : exclude_patterns_)
    {
        if (exclude_pattern.matches(filename))
        {
            return false;
        }
    }

    // If no include patterns are specified, include everything (that wasn't excluded)
    if (include_patterns_.empty())
    {
        return true;
    }

    // Check include patterns - if any match, include the file
    for (const auto& include_pattern : include_patterns_)
    {
        if (include_pattern.matches(filename))
        {
            return true;
        }
    }

    // No include patterns matched
    return false;
}

auto pattern_filter::has_patterns() const -> bool
{
    return !include_patterns_.empty() || !exclude_patterns_.empty();
}

auto pattern_filter::is_wildcard() const -> bool
{
    // Consider it a wildcard if:
    // 1. No patterns at all
    // 2. Single include pattern that is "*" and no exclude patterns
    if (!has_patterns())
    {
        return true;
    }

    if (include_patterns_.size() == 1 && exclude_patterns_.empty())
    {
        return include_patterns_[0].get_pattern() == "*";
    }

    return false;
}

auto pattern_filter::get_include_patterns() const -> const std::vector<wildcard_pattern>&
{
    return include_patterns_;
}

auto pattern_filter::get_exclude_patterns() const -> const std::vector<wildcard_pattern>&
{
    return exclude_patterns_;
}

auto make_pattern_filter(const std::string& pattern) -> pattern_filter
{
    return pattern_filter(pattern);
}

auto make_pattern_filter(const std::vector<std::string>& includes,
                         const std::vector<std::string>& excludes) -> pattern_filter
{
    return pattern_filter(includes, excludes);
}

} // namespace fs 