#include "version.h"

#include <cctype>
#include <cstddef>

namespace version
{

namespace
{

auto ltrim(std::string_view s) -> std::string_view
{
    while(!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    {
        s.remove_prefix(1);
    }
    return s;
}

auto rtrim(std::string_view s) -> std::string_view
{
    while(!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    {
        s.remove_suffix(1);
    }
    return s;
}

auto trim(std::string_view s) -> std::string_view
{
    return rtrim(ltrim(s));
}

auto strip_optional_v(std::string_view s) -> std::string_view
{
    s = trim(s);
    if(!s.empty() && (s.front() == 'v' || s.front() == 'V'))
    {
        s.remove_prefix(1);
    }
    return s;
}

// Values over this cap are treated as malformed - protects against numeric
// overflow on very long digit runs without constraining realistic versions.
constexpr long long k_int_parse_cap = 1'000'000'000LL;

auto parse_int_token(std::string_view s, std::size_t& pos, int& out) -> bool
{
    if(pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])))
    {
        return false;
    }

    long long val = 0;
    while(pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
    {
        val = val * 10 + (s[pos] - '0');
        if(val > k_int_parse_cap)
        {
            return false;
        }
        ++pos;
    }
    out = static_cast<int>(val);
    return true;
}

auto expect_char(std::string_view s, std::size_t& pos, char c) -> bool
{
    if(pos >= s.size() || s[pos] != c)
    {
        return false;
    }
    ++pos;
    return true;
}

} // namespace

auto engine_version::to_string() const -> std::string
{
    std::string out;
    out.reserve(24);
    out += std::to_string(major);
    out += '.';
    out += std::to_string(minor);
    out += '.';
    out += std::to_string(patch);

    // Only emit the commit/sha suffix when present; a plain "1.0.0" should
    // roundtrip as "1.0.0" (not "1.0.0-0").
    if(commit_count != 0 || !sha.empty())
    {
        out += '-';
        out += std::to_string(commit_count);
        if(!sha.empty())
        {
            out += '-';
            out += sha;
        }
    }
    return out;
}

auto operator==(const engine_version& a, const engine_version& b) -> bool
{
    return a.major == b.major && a.minor == b.minor && a.patch == b.patch && a.commit_count == b.commit_count;
}

auto operator<=>(const engine_version& a, const engine_version& b) -> std::strong_ordering
{
    if(auto c = a.major <=> b.major; c != 0)
    {
        return c;
    }
    if(auto c = a.minor <=> b.minor; c != 0)
    {
        return c;
    }
    if(auto c = a.patch <=> b.patch; c != 0)
    {
        return c;
    }
    return a.commit_count <=> b.commit_count;
}

auto parse(std::string_view text) -> std::optional<engine_version>
{
    engine_version v;
    v.original = std::string(trim(text));

    std::string_view s = trim(strip_optional_v(text));
    std::size_t pos = 0;

    if(!parse_int_token(s, pos, v.major))
    {
        return std::nullopt;
    }
    if(!expect_char(s, pos, '.'))
    {
        return std::nullopt;
    }
    if(!parse_int_token(s, pos, v.minor))
    {
        return std::nullopt;
    }
    if(!expect_char(s, pos, '.'))
    {
        return std::nullopt;
    }
    if(!parse_int_token(s, pos, v.patch))
    {
        return std::nullopt;
    }

    // Optional "-commit_count"
    if(pos < s.size() && s[pos] == '-')
    {
        ++pos;
        if(!parse_int_token(s, pos, v.commit_count))
        {
            return std::nullopt;
        }

        // Optional "-sha" (sha kept verbatim, including any leading 'g')
        if(pos < s.size() && s[pos] == '-')
        {
            ++pos;
            std::string_view rest = trim(s.substr(pos));
            if(!rest.empty())
            {
                v.sha = std::string(rest);
                pos = s.size();
            }
        }
    }

    if(!trim(s.substr(pos)).empty())
    {
        return std::nullopt;
    }

    return v;
}

auto compare(const engine_version& a, const engine_version& b) -> int
{
    const auto cmp = a <=> b;
    if(cmp < 0)
    {
        return -1;
    }
    if(cmp > 0)
    {
        return 1;
    }
    return 0;
}

auto get_current() -> engine_version
{
    if(auto parsed = parse(get_full()))
    {
        return *parsed;
    }
    return {};
}

auto get_major() -> std::string
{
    return VERSION_MAJOR;
}

auto get_minor() -> std::string
{
    return VERSION_MINOR;
}

auto get_patch() -> std::string
{
    return VERSION_PATCH;
}

auto get_full() -> std::string
{
    return VERSION;
}

} // namespace version
