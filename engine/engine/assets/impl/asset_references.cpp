#include "asset_references.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <unordered_set>

namespace unravel
{
namespace asset_references
{
namespace
{
// "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
constexpr size_t UUID_TEXT_SIZE = 36;
constexpr std::array<size_t, 4> UUID_DASH_POSITIONS = {8, 13, 18, 23};

auto is_uuid_text(hpp::string_view text) -> bool
{
    if(text.size() != UUID_TEXT_SIZE)
    {
        return false;
    }
    for(size_t i = 0; i < text.size(); ++i)
    {
        const bool is_dash_position =
            std::find(UUID_DASH_POSITIONS.begin(), UUID_DASH_POSITIONS.end(), i) != UUID_DASH_POSITIONS.end();
        const bool is_valid = is_dash_position ? text[i] == '-' : std::isxdigit(static_cast<unsigned char>(text[i])) != 0;
        if(!is_valid)
        {
            return false;
        }
    }
    return true;
}

auto is_quote(char c) -> bool
{
    return c == '"' || c == '\'';
}

auto is_space(char c) -> bool
{
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

auto trim(hpp::string_view text) -> hpp::string_view
{
    while(!text.empty() && is_space(text.front()))
    {
        text.remove_prefix(1);
    }
    while(!text.empty() && is_space(text.back()))
    {
        text.remove_suffix(1);
    }
    return text;
}

/// Collects strings in order of first appearance, without repeats.
class unique_paths
{
public:
    void add(hpp::string_view path)
    {
        std::string value(path);
        if(!value.empty() && seen_.insert(value).second)
        {
            paths_.push_back(std::move(value));
        }
    }

    auto take() -> std::vector<std::string>
    {
        return std::move(paths_);
    }

private:
    std::unordered_set<std::string> seen_;
    std::vector<std::string> paths_;
};

/// Whether a linked value names a file of the project rather than a fragment, inline data, a web
/// page or a nested function (`@import url(...)`, which the url( marker reads).
auto is_file_link(hpp::string_view value) -> bool
{
    constexpr std::array<hpp::string_view, 4> skipped_prefixes = {"#", "data:", "http:", "https:"};
    return !value.empty() && value.find('(') == hpp::string_view::npos &&
           std::none_of(skipped_prefixes.begin(),
                                          skipped_prefixes.end(),
                                          [value](hpp::string_view prefix)
                                          {
                                              return value.starts_with(prefix);
                                          });
}

/// The value after `pos` (just past an `=` or `:`): a quoted string, else a bare token ending at
/// white space, ';', '}', ')' or ','.
auto read_value(hpp::string_view text, size_t pos) -> hpp::string_view
{
    while(pos < text.size() && is_space(text[pos]))
    {
        ++pos;
    }
    if(pos >= text.size())
    {
        return {};
    }
    if(is_quote(text[pos]))
    {
        const size_t end = text.find(text[pos], pos + 1);
        return end == hpp::string_view::npos ? hpp::string_view{} : text.substr(pos + 1, end - pos - 1);
    }
    size_t end = pos;
    constexpr hpp::string_view terminators = ";}),";
    while(end < text.size() && !is_space(text[end]) && terminators.find(text[end]) == hpp::string_view::npos)
    {
        ++end;
    }
    return text.substr(pos, end - pos);
}

void add_link(hpp::string_view value, unique_paths& paths)
{
    value = trim(value);
    if(is_file_link(value))
    {
        paths.add(value);
    }
}

/// Adds the value after every `marker` in `text` (`url(`, `image(`, `@import`).
void add_values_after(hpp::string_view text, hpp::string_view marker, unique_paths& paths)
{
    for(size_t pos = text.find(marker); pos != hpp::string_view::npos; pos = text.find(marker, pos + marker.size()))
    {
        add_link(read_value(text, pos + marker.size()), paths);
    }
}

auto is_name_char(char c) -> bool
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
}

/// Adds the value of every attribute or property `name` in `text`: `name="..."` in a document,
/// `name: ...;` in a style sheet, spaces allowed around the '=' or ':'. `data-src` is not `src`.
void add_named_values(hpp::string_view text, hpp::string_view name, unique_paths& paths)
{
    for(size_t pos = text.find(name); pos != hpp::string_view::npos; pos = text.find(name, pos + name.size()))
    {
        const size_t name_end = pos + name.size();
        const bool is_whole_name =
            (pos == 0 || !is_name_char(text[pos - 1])) && (name_end == text.size() || !is_name_char(text[name_end]));
        size_t separator = name_end;
        while(separator < text.size() && is_space(text[separator]))
        {
            ++separator;
        }
        if(is_whole_name && separator < text.size() && (text[separator] == '=' || text[separator] == ':'))
        {
            add_link(read_value(text, separator + 1), paths);
        }
    }
}

/// Where the quoted text that starts at `begin` ends: the next `quote` not escaped by a backslash.
auto find_closing_quote(hpp::string_view text, size_t begin, char quote) -> size_t
{
    for(size_t end = text.find(quote, begin); end != hpp::string_view::npos; end = text.find(quote, end + 1))
    {
        if(text[end - 1] != '\\')
        {
            return end;
        }
    }
    return hpp::string_view::npos;
}
} // namespace

auto find_quoted_uids(hpp::string_view text) -> std::vector<hpp::uuid>
{
    std::vector<hpp::uuid> uids;
    std::set<hpp::uuid> seen;
    for(size_t pos = text.find('"'); pos != hpp::string_view::npos; pos = text.find('"', pos + 1))
    {
        const size_t end = pos + 1 + UUID_TEXT_SIZE;
        if(end >= text.size() || text[end] != '"')
        {
            continue;
        }
        const auto candidate = text.substr(pos + 1, UUID_TEXT_SIZE);
        if(!is_uuid_text(candidate))
        {
            continue;
        }
        const auto uid = hpp::uuid::from_string(candidate);
        if(uid && !uid->is_nil() && seen.insert(*uid).second)
        {
            uids.push_back(*uid);
        }
        pos = end;
    }
    return uids;
}

auto find_quoted_protocol_paths(hpp::string_view text, hpp::string_view protocol) -> std::vector<std::string>
{
    const std::string prefix = std::string(protocol) + ":/";
    unique_paths paths;
    for(size_t pos = text.find(prefix); pos != hpp::string_view::npos; pos = text.find(prefix, pos + prefix.size()))
    {
        if(pos == 0 || !is_quote(text[pos - 1]))
        {
            continue;
        }
        const size_t end = find_closing_quote(text, pos, text[pos - 1]);
        if(end == hpp::string_view::npos)
        {
            break;
        }
        const auto path = text.substr(pos, end - pos);
        if(path.find('\n') == hpp::string_view::npos)
        {
            paths.add(path);
        }
    }
    return paths.take();
}

auto find_ui_linked_paths(hpp::string_view text) -> std::vector<std::string>
{
    unique_paths paths;
    constexpr std::array<hpp::string_view, 2> names = {"href", "src"};
    for(const auto& name : names)
    {
        add_named_values(text, name, paths);
    }
    constexpr std::array<hpp::string_view, 3> markers = {"url(", "image(", "@import"};
    for(const auto& marker : markers)
    {
        add_values_after(text, marker, paths);
    }
    return paths.take();
}

} // namespace asset_references
} // namespace unravel
