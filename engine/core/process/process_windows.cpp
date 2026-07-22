#include "process.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <limits>
#include <vector>

namespace unravel
{
namespace process
{
namespace
{

auto make_windows_error(DWORD code) -> std::error_code
{
    return std::error_code(static_cast<int>(code), std::system_category());
}

auto wide_to_utf8(std::wstring_view text) -> std::string
{
    if(text.empty())
    {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8,
                                         0,
                                         text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr,
                                         0,
                                         nullptr,
                                         nullptr);
    if(size <= 0)
    {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8,
                        0,
                        text.data(),
                        static_cast<int>(text.size()),
                        utf8.data(),
                        size,
                        nullptr,
                        nullptr);
    return utf8;
}

} // namespace

auto get_executable_path() -> std::string
{
    std::wstring buffer(MAX_PATH, L'\0');
    for(;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if(length == 0)
        {
            return {};
        }
        if(length < buffer.size())
        {
            buffer.resize(length);
            return wide_to_utf8(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

auto get_current_process_id() -> std::uint64_t
{
    return static_cast<std::uint64_t>(GetCurrentProcessId());
}

auto spawn_replacement(const std::vector<std::string>& arguments, std::uint32_t restart_count) -> restart_result
{
    restart_result result{};
    const std::vector<std::string> command_arguments =
        build_replacement_command_arguments(arguments, restart_count);
    if(command_arguments.empty() || command_arguments.front().empty())
    {
        result.error = make_windows_error(ERROR_FILE_NOT_FOUND);
        return result;
    }
    std::wstring command_line = build_windows_command_line(command_arguments);
    if(command_line.empty())
    {
        result.error = make_windows_error(ERROR_INVALID_PARAMETER);
        return result;
    }
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    const BOOL created = CreateProcessW(nullptr,
                                        command_line.data(),
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        0,
                                        nullptr,
                                        nullptr,
                                        &startup_info,
                                        &process_info);
    if(!created)
    {
        result.error = make_windows_error(GetLastError());
        return result;
    }
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    result.success = true;
    return result;
}

auto wait_for_process_exit(std::uint64_t process_id) -> bool
{
    if(process_id == 0 || process_id > static_cast<std::uint64_t>((std::numeric_limits<DWORD>::max)()))
    {
        return false;
    }
    const DWORD pid = static_cast<DWORD>(process_id);
    HANDLE process_handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if(process_handle == nullptr)
    {
        // Process already terminated (or PID invalid): treat as successful wait.
        return true;
    }
    const DWORD wait_result = WaitForSingleObject(process_handle, INFINITE);
    CloseHandle(process_handle);
    return wait_result == WAIT_OBJECT_0;
}

} // namespace process
} // namespace unravel
