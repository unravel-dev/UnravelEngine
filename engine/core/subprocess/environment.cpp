#include "environment.h"

#include <algorithm>
#include <cctype>
#include <cwchar>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
extern char** environ;
#endif

namespace subprocess
{
namespace
{
auto get_variable_name(const std::string& entry) -> std::string
{
    // Windows keeps per-drive folders in entries like "=C:=C:\path", whose name starts with '='.
    const auto separator = entry.find('=', 1);
    std::string name = entry.substr(0, separator);
#if defined(_WIN32)
    std::transform(name.begin(),
                   name.end(),
                   name.begin(),
                   [](unsigned char c)
                   {
                       return static_cast<char>(std::toupper(c));
                   });
#endif
    return name;
}

auto get_current_environment() -> std::vector<std::string>
{
    std::vector<std::string> entries;
#if defined(_WIN32)
    // The wide block, converted to the ANSI code page: subprocess.h starts processes with CreateProcessA.
    wchar_t* block = GetEnvironmentStringsW();
    for(const wchar_t* entry = block; entry != nullptr && *entry != L'\0'; entry += std::wcslen(entry) + 1)
    {
        const int length = static_cast<int>(std::wcslen(entry));
        const int size = WideCharToMultiByte(CP_ACP, 0, entry, length, nullptr, 0, nullptr, nullptr);
        std::string converted(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_ACP, 0, entry, length, converted.data(), size, nullptr, nullptr);
        entries.push_back(std::move(converted));
    }
    if(block != nullptr)
    {
        FreeEnvironmentStringsW(block);
    }
#else
    for(char** entry = environ; entry != nullptr && *entry != nullptr; ++entry)
    {
        entries.emplace_back(*entry);
    }
#endif
    return entries;
}
} // namespace

auto merge_environment(const std::vector<std::string>& changes) -> std::vector<std::string>
{
    std::vector<std::string> names;
    names.reserve(changes.size());
    for(const auto& change : changes)
    {
        names.push_back(get_variable_name(change));
    }
    std::vector<std::string> merged;
    for(auto& entry : get_current_environment())
    {
        if(std::find(names.begin(), names.end(), get_variable_name(entry)) == names.end())
        {
            merged.push_back(std::move(entry));
        }
    }
    merged.insert(merged.end(), changes.begin(), changes.end());
    return merged;
}
} // namespace subprocess
