#include "process.h"
#include "startup_arguments.h"

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace unravel
{
namespace process
{

auto quote_windows_argument(std::string_view argument) -> std::string
{
    // Follow Windows CommandLineToArgvW rules used by CreateProcess.
    const bool needs_quotes =
        argument.empty() || argument.find_first_of(" \t\"") != std::string_view::npos;
    if(!needs_quotes)
    {
        return std::string(argument);
    }
    std::string result;
    result.reserve(argument.size() + 2);
    result.push_back('"');
    std::size_t backslash_count = 0;
    for(char ch : argument)
    {
        if(ch == '\\')
        {
            ++backslash_count;
            continue;
        }
        if(ch == '"')
        {
            result.append(backslash_count * 2 + 1, '\\');
            result.push_back('"');
            backslash_count = 0;
            continue;
        }
        if(backslash_count > 0)
        {
            result.append(backslash_count, '\\');
            backslash_count = 0;
        }
        result.push_back(ch);
    }
    if(backslash_count > 0)
    {
        result.append(backslash_count * 2, '\\');
    }
    result.push_back('"');
    return result;
}

#if defined(_WIN32)
namespace
{
auto utf8_to_wide(std::string_view text) -> std::wstring
{
    if(text.empty())
    {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8,
                                         0,
                                         text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr,
                                         0);
    if(size <= 0)
    {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8,
                        0,
                        text.data(),
                        static_cast<int>(text.size()),
                        wide.data(),
                        size);
    return wide;
}
} // namespace
#endif

auto build_windows_command_line(const std::vector<std::string>& arguments) -> std::wstring
{
    std::string utf8;
    for(std::size_t i = 0; i < arguments.size(); ++i)
    {
        if(i > 0)
        {
            utf8.push_back(' ');
        }
        utf8 += quote_windows_argument(arguments[i]);
    }
#if defined(_WIN32)
    return utf8_to_wide(utf8);
#else
    return std::wstring(utf8.begin(), utf8.end());
#endif
}

auto build_replacement_command_arguments(const std::vector<std::string>& application_arguments,
                                         std::uint32_t restart_count) -> std::vector<std::string>
{
    std::vector<std::string> arguments;
    arguments.reserve(application_arguments.size() + 4);
    arguments.push_back(get_executable_path());
    arguments.insert(arguments.end(), application_arguments.begin(), application_arguments.end());
    arguments.emplace_back(std::string(RESTART_FROM_PID_PREFIX) + std::to_string(get_current_process_id()));
    arguments.emplace_back(RESTARTED_FLAG);
    arguments.emplace_back(std::string(RESTART_COUNT_PREFIX) + std::to_string(restart_count));
    return arguments;
}

} // namespace process
} // namespace unravel
