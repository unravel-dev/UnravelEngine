#include "utils.h"
#include <algorithm>
#include <array>
#include <cstdio>

namespace string_utils
{
namespace
{
/// The first character JSON writes as it is: the control characters below it are escaped.
constexpr unsigned char JSON_FIRST_PLAIN_CHARACTER = 0x20;
/// Room for "\uXXXX" and its terminator.
constexpr std::size_t JSON_UNICODE_ESCAPE_SIZE = 7;
/// What an escaped string usually grows by, reserved up front.
constexpr std::size_t JSON_ESCAPE_RESERVE = 8;

/// The two-character escape JSON has for `character` ("\n" for a line feed), or null.
auto get_json_short_escape(char character) -> const char*
{
    switch(character)
    {
        case '"':
            return "\\\"";
        case '\\':
            return "\\\\";
        case '\b':
            return "\\b";
        case '\f':
            return "\\f";
        case '\n':
            return "\\n";
        case '\r':
            return "\\r";
        case '\t':
            return "\\t";
        default:
            return nullptr;
    }
}
} // namespace


auto ltrim(const std::string& str) -> std::string
{
    auto str_modified = str;
    alterable::ltrim(str_modified);
    return str_modified;
}

auto rtrim(const std::string& str) -> std::string
{
    auto str_modified = str;
    alterable::rtrim(str_modified);
    return str_modified;
}

auto trim(const std::string& str) -> std::string
{
    auto str_modified = str;
    alterable::trim(str_modified);
    return str_modified;
}

auto replace(const std::string& str, const std::string& search, const std::string& replace) -> std::string
{
    auto str_modified = str;
    alterable::replace(str_modified, search, replace);
    return str_modified;
}

auto to_upper(const std::string& str) -> std::string
{
    auto str_modified = str;
    alterable::to_upper(str_modified);
    return str_modified;
}

auto to_lower(const std::string& str) -> std::string
{
    auto str_modified = str;
    alterable::to_lower(str_modified);
    return str_modified;
}

auto capitalize(const std::string& word) -> std::string
{
    auto str_modified = word;
    alterable::capitalize(str_modified);
    return str_modified;
}

auto extract_substring(const std::string& str, const std::string& from, const std::string& to) -> std::string
{
    auto it_from = str.find(from);
    if(it_from != std::string::npos)
    {
        auto result = str.substr(it_from + from.size(), std::string::npos);
        auto it_to = result.find_first_of(to);
        if(it_to != std::string::npos)
        {
            result = result.substr(0, it_to);
            alterable::trim(result);
            return result;
        }
    }

    return {};
}

auto get_last_lines(const std::string& text, std::size_t count) -> std::string
{
    const auto last = text.find_last_not_of("\r\n");
    if(count == 0 || last == std::string::npos)
    {
        return {};
    }
    const auto end = last + 1;
    auto begin = end;
    for(std::size_t found = 0; begin > 0; --begin)
    {
        if(text[begin - 1] == '\n' && ++found == count)
        {
            break;
        }
    }
    return text.substr(begin, end - begin);
}

auto escape_json(const std::string& text) -> std::string
{
    std::string escaped;
    escaped.reserve(text.size() + JSON_ESCAPE_RESERVE);
    for(const char character : text)
    {
        const auto byte = static_cast<unsigned char>(character);
        const char* short_escape = get_json_short_escape(character);
        if(short_escape)
        {
            escaped += short_escape;
        }
        else if(byte < JSON_FIRST_PLAIN_CHARACTER)
        {
            std::array<char, JSON_UNICODE_ESCAPE_SIZE> unicode{};
            std::snprintf(unicode.data(), unicode.size(), "\\u%04x", byte);
            escaped += unicode.data();
        }
        else
        {
            escaped += character;
        }
    }
    return escaped;
}

auto make_json_string(const std::string& text) -> std::string
{
    return "\"" + escape_json(text) + "\"";
}

namespace alterable
{
void ltrim(std::string& str)
{
    str.erase(str.begin(),
              std::find_if(str.begin(),
                           str.end(),
                           [](unsigned char ch)
                           {
                               return !std::isspace(ch);
                           }));
}

void rtrim(std::string& str)
{
    str.erase(std::find_if(str.rbegin(),
                           str.rend(),
                           [](unsigned char ch)
                           {
                               return !std::isspace(ch);
                           })
                  .base(),
              str.end());
}

void trim(std::string& str)
{
    alterable::ltrim(str);
    alterable::rtrim(str);
}

void replace(std::string& str, const std::string& search, const std::string& replace)
{
    if(search.empty())
    {
        return;
    }

    size_t pos = 0;
    while((pos = str.find(search, pos)) != std::string::npos)
    {
        str.replace(pos, search.length(), replace);
        pos += replace.length();
    }
}

void to_upper(std::string& str)
{
    std::transform(std::begin(str),
                   std::end(str),
                   std::begin(str),
                   [](unsigned char c)
                   {
                       return std::toupper(c);
                   });
}

void to_lower(std::string& str)
{
    std::transform(std::begin(str),
                   std::end(str),
                   std::begin(str),
                   [](unsigned char c)
                   {
                       return std::tolower(c);
                   });
}

void capitalize(std::string& str)
{
    if (str.empty())
    {
        return;
    }

    str[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(str[0])));
}
}

auto tokenize(const std::string& str, const std::string& delimiters) -> string_tokens_t
{
    string_tokens_t tokens;
    // Skip delimiters at beginning.
    auto last_pos = str.find_first_not_of(delimiters, 0);
    // Find first "non-delimiter".
    auto pos = str.find_first_of(delimiters, last_pos);

    while(std::string::npos != pos || std::string::npos != last_pos)
    {
        // Found a token, add it to the vector.
        tokens.emplace_back(str.substr(last_pos, pos - last_pos));
        // Skip delimiters.  Note the "not_of"
        last_pos = str.find_first_not_of(delimiters, pos);
        // Find next "non-delimiter"
        pos = str.find_first_of(delimiters, last_pos);
    }

    return tokens;
}

// namespace alterable

// namespace alterable

} // namespace string_utils
