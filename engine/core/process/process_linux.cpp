#include "process.h"

#include <cerrno>
#include <chrono>
#include <limits>
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

auto make_argv_pointers(std::vector<std::string>& command_arguments) -> std::vector<char*>
{
    std::vector<char*> argv;
    argv.reserve(command_arguments.size() + 1);
    for(auto& argument : command_arguments)
    {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    return argv;
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

auto spawn_replacement(const std::vector<std::string>& arguments,
                       std::uint32_t restart_count,
                       const std::function<bool()>& release_resources) -> restart_result
{
    // In-place exec: shell keeps this PID as the foreground job (Ctrl+C works).
    // Sibling spawn + parent exit makes waitpid return and the shell reclaim the TTY.
    // Do not use setsid() — that detaches from the controlling terminal.
    restart_result result{};
    if(!release_resources)
    {
        result.error = make_errno_error(EINVAL);
        return result;
    }
    if(!release_resources())
    {
        result.resources_released = true;
        result.error = make_errno_error(EIO);
        return result;
    }
    result.resources_released = true;
    std::vector<std::string> command_arguments =
        build_replacement_command_arguments(arguments, restart_count, false);
    if(command_arguments.empty() || command_arguments.front().empty())
    {
        result.error = make_errno_error(ENOENT);
        return result;
    }
    std::vector<char*> argv = make_argv_pointers(command_arguments);
    execve(command_arguments.front().c_str(), argv.data(), environ);
    result.error = make_errno_error(errno);
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
