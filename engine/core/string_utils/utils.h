#pragma once

#include <string>
#include <vector>

namespace string_utils
{

using string_tokens_t = std::vector<std::string>;

auto ltrim(const std::string& str) -> std::string;

auto rtrim(const std::string& str) -> std::string;

auto trim(const std::string& str) -> std::string;

auto tokenize(const std::string& str, const std::string& delimiters) -> string_tokens_t;

auto replace(const std::string& subject, const std::string& search, const std::string& replace) -> std::string;

auto to_upper(const std::string& str) -> std::string;

auto to_lower(const std::string& str) -> std::string;

auto capitalize(const std::string& word) -> std::string;

// Returns the first substring between "from - to" without trailing spaces
auto extract_substring(const std::string& str, const std::string& from, const std::string& to) -> std::string;

/// The last `count` lines of `text` (a tool's output), without the line breaks that end it; all of it when it
/// has fewer lines.
auto get_last_lines(const std::string& text, std::size_t count) -> std::string;

/// `text` escaped for the inside of a JSON string: quotes, backslashes and control characters. Bytes from 0x80 up
/// pass as they are, so UTF-8 stays UTF-8.
auto escape_json(const std::string& text) -> std::string;

/// `text` as a JSON string literal, quotes included.
auto make_json_string(const std::string& text) -> std::string;

namespace alterable
{
void ltrim(std::string& str);

void rtrim(std::string& str);

void trim(std::string& str);

void replace(std::string& subject, const std::string& search, const std::string& replace);

void to_upper(std::string& str);

void to_lower(std::string& str);

void capitalize(std::string& str);

} // namespace alterable

} // namespace string_utils
