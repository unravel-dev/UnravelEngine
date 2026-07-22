#include "process.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <spawn.h>
#include <signal.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace unravel
{
namespace process
{
namespace
{

constexpr auto WAIT_POLL_INTERVAL = std::chrono::milliseconds(20);

auto make_errno_error(int code) -> std::error_code
{
    return std::error_code(code, std::generic_category());
}

} // namespace

auto get_executable_path() -> std::string
{
    std::vector<char> buffer(1024);
    for(;;)
    {
        const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if(length < 0)
        {
            return {};
        }
        if(static_cast<std::size_t>(length) < buffer.size())
        {
            return std::string(buffer.data(), static_cast<std::size_t>(length));
        }
        buffer.resize(buffer.size() * 2);
    }
}

auto get_current_process_id() -> std::uint64_t
{
    return static_cast<std::uint64_t>(getpid());
}

auto spawn_replacement(const std::vector<std::string>& arguments, std::uint32_t restart_count) -> restart_result
{
    restart_result result{};
    const std::vector<std::string> command_arguments =
        build_replacement_command_arguments(arguments, restart_count);
    if(command_arguments.empty() || command_arguments.front().empty())
    {
        result.error = make_errno_error(ENOENT);
        return result;
    }
    std::vector<char*> argv;
    argv.reserve(command_arguments.size() + 1);
    for(const auto& argument : command_arguments)
    {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    posix_spawnattr_t attributes{};
    if(posix_spawnattr_init(&attributes) != 0)
    {
        result.error = make_errno_error(errno);
        return result;
    }
    posix_spawn_file_actions_t file_actions{};
    if(posix_spawn_file_actions_init(&file_actions) != 0)
    {
        posix_spawnattr_destroy(&attributes);
        result.error = make_errno_error(errno);
        return result;
    }
    pid_t child_pid = 0;
    const int spawn_status = posix_spawn(&child_pid,
                                         command_arguments.front().c_str(),
                                         &file_actions,
                                         &attributes,
                                         argv.data(),
                                         environ);
    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&attributes);
    if(spawn_status != 0)
    {
        result.error = make_errno_error(spawn_status);
        return result;
    }
    result.success = true;
    return result;
}

auto wait_for_process_exit(std::uint64_t process_id) -> bool
{
    if(process_id == 0 || process_id > static_cast<std::uint64_t>((std::numeric_limits<pid_t>::max)()))
    {
        return false;
    }
    const pid_t pid = static_cast<pid_t>(process_id);
    for(;;)
    {
        if(kill(pid, 0) == 0)
        {
            std::this_thread::sleep_for(WAIT_POLL_INTERVAL);
            continue;
        }
        if(errno == EPERM)
        {
            std::this_thread::sleep_for(WAIT_POLL_INTERVAL);
            continue;
        }
        // ESRCH: process no longer exists. Treat as successful wait completion.
        return errno == ESRCH;
    }
}

} // namespace process
} // namespace unravel
