#include "package_version.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>

namespace unravel::packages
{
namespace
{
/// A whole number of at most this many digits: a damaged version must not overflow.
constexpr std::size_t VERSION_NUMBER_MAX_DIGITS = 9;

auto parse_number(const std::string& text, std::size_t& position, int& value) -> bool
{
    const std::size_t start = position;
    while(position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])) != 0)
    {
        ++position;
    }
    const std::size_t length = position - start;
    if(length == 0 || length > VERSION_NUMBER_MAX_DIGITS)
    {
        return false;
    }
    const auto result = std::from_chars(text.data() + start, text.data() + position, value);
    return result.ec == std::errc{};
}

auto is_pre_release_char(char value) -> bool
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '.' || value == '-';
}

/// Pre-releases compare by their dot-separated parts: numbers as numbers, below words.
auto compare_pre_release(const std::string& lhs, const std::string& rhs) -> std::strong_ordering
{
    if(lhs == rhs)
    {
        return std::strong_ordering::equal;
    }
    if(lhs.empty())
    {
        return std::strong_ordering::greater;
    }
    if(rhs.empty())
    {
        return std::strong_ordering::less;
    }
    std::istringstream left(lhs);
    std::istringstream right(rhs);
    std::string left_part;
    std::string right_part;
    while(true)
    {
        const bool has_left = static_cast<bool>(std::getline(left, left_part, '.'));
        const bool has_right = static_cast<bool>(std::getline(right, right_part, '.'));
        if(!has_left || !has_right)
        {
            return has_left ? std::strong_ordering::greater : (has_right ? std::strong_ordering::less : std::strong_ordering::equal);
        }
        const bool is_left_number = std::all_of(left_part.begin(), left_part.end(), ::isdigit);
        const bool is_right_number = std::all_of(right_part.begin(), right_part.end(), ::isdigit);
        if(is_left_number && is_right_number && left_part.size() != right_part.size())
        {
            return left_part.size() <=> right_part.size();
        }
        if(is_left_number != is_right_number)
        {
            return is_left_number ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        const int order = left_part.compare(right_part);
        if(order != 0)
        {
            return order < 0 ? std::strong_ordering::less : std::strong_ordering::greater;
        }
    }
}

auto make_bound(version_bound::kind relation, const package_version& version) -> version_bound
{
    return {relation, version};
}

/// One space-separated part of a range, which may stand for two bounds (a caret or a tilde).
auto parse_range_part(const std::string& part, std::vector<version_bound>& bounds) -> bool
{
    struct prefix
    {
        const char* text;
        version_bound::kind relation;
    };
    static const prefix PREFIXES[] = {
        {">=", version_bound::kind::greater_or_equal},
        {"<=", version_bound::kind::less_or_equal},
        {">", version_bound::kind::greater},
        {"<", version_bound::kind::less},
        {"=", version_bound::kind::equal},
    };
    if(part.front() == '^' || part.front() == '~')
    {
        const auto version = parse_version(part.substr(1));
        if(!version)
        {
            return false;
        }
        package_version upper{};
        if(part.front() == '^')
        {
            // Below 1.0.0 every minor version may break, as semantic versioning has it.
            upper = version->major > 0 ? package_version{version->major + 1, 0, 0, {}} : package_version{0, version->minor + 1, 0, {}};
        }
        else
        {
            upper = package_version{version->major, version->minor + 1, 0, {}};
        }
        bounds.push_back(make_bound(version_bound::kind::greater_or_equal, *version));
        bounds.push_back(make_bound(version_bound::kind::less, upper));
        return true;
    }
    for(const auto& candidate : PREFIXES)
    {
        const std::string text(candidate.text);
        if(part.compare(0, text.size(), text) == 0)
        {
            const auto version = parse_version(part.substr(text.size()));
            if(!version)
            {
                return false;
            }
            bounds.push_back(make_bound(candidate.relation, *version));
            return true;
        }
    }
    const auto version = parse_version(part);
    if(!version)
    {
        return false;
    }
    bounds.push_back(make_bound(version_bound::kind::equal, *version));
    return true;
}

} // namespace

auto package_version::to_string() const -> std::string
{
    std::string text = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if(!pre_release.empty())
    {
        text += "-" + pre_release;
    }
    return text;
}

auto package_version::get_next_patch() const -> package_version
{
    // A pre-release's next version is its release.
    if(!pre_release.empty())
    {
        return {major, minor, patch, {}};
    }
    return {major, minor, patch + 1, {}};
}

auto operator==(const package_version& lhs, const package_version& rhs) -> bool
{
    return (lhs <=> rhs) == std::strong_ordering::equal;
}

auto operator<=>(const package_version& lhs, const package_version& rhs) -> std::strong_ordering
{
    if(const auto order = lhs.major <=> rhs.major; order != 0)
    {
        return order;
    }
    if(const auto order = lhs.minor <=> rhs.minor; order != 0)
    {
        return order;
    }
    if(const auto order = lhs.patch <=> rhs.patch; order != 0)
    {
        return order;
    }
    return compare_pre_release(lhs.pre_release, rhs.pre_release);
}

auto parse_version(const std::string& text) -> std::optional<package_version>
{
    std::size_t position = (!text.empty() && (text.front() == 'v' || text.front() == 'V')) ? 1 : 0;
    package_version version;
    if(!parse_number(text, position, version.major) || position >= text.size() || text[position++] != '.')
    {
        return std::nullopt;
    }
    if(!parse_number(text, position, version.minor) || position >= text.size() || text[position++] != '.')
    {
        return std::nullopt;
    }
    if(!parse_number(text, position, version.patch))
    {
        return std::nullopt;
    }
    if(position == text.size())
    {
        return version;
    }
    if(text[position] != '-' || position + 1 == text.size())
    {
        return std::nullopt;
    }
    version.pre_release = text.substr(position + 1);
    if(!std::all_of(version.pre_release.begin(), version.pre_release.end(), is_pre_release_char))
    {
        return std::nullopt;
    }
    return version;
}

auto version_range::contains(const package_version& version) const -> bool
{
    return std::all_of(bounds.begin(),
                       bounds.end(),
                       [&version](const version_bound& bound)
                       {
                           const auto order = version <=> bound.version;
                           switch(bound.relation)
                           {
                               case version_bound::kind::equal:
                                   return order == 0;
                               case version_bound::kind::greater:
                                   return order > 0;
                               case version_bound::kind::greater_or_equal:
                                   return order >= 0;
                               case version_bound::kind::less:
                                   return order < 0;
                               case version_bound::kind::less_or_equal:
                                   return order <= 0;
                           }
                           return false;
                       });
}

auto parse_version_range(const std::string& text) -> std::optional<version_range>
{
    version_range range;
    std::istringstream parts(text);
    std::string part;
    while(parts >> part)
    {
        if(part == "*")
        {
            continue;
        }
        if(!parse_range_part(part, range.bounds))
        {
            return std::nullopt;
        }
    }
    return range;
}

auto is_in_range(const std::string& version, const std::string& range) -> bool
{
    const auto parsed_version = parse_version(version);
    const auto parsed_range = parse_version_range(range);
    return parsed_version && parsed_range && parsed_range->contains(*parsed_version);
}

} // namespace unravel::packages
