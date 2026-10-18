#include "process.h"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace unravel
{
namespace process
{
namespace
{
constexpr const char* NULL_DEVICE = "/dev/null";
constexpr int FIRST_INHERITED_DESCRIPTOR = 3;
/// Descriptors are closed up to this bound when the system reports none, or an unusable one.
constexpr long DESCRIPTOR_LIMIT_FALLBACK = 1024;
constexpr long DESCRIPTOR_LIMIT_MAX = 65536;
/// The exit code of a child that could not start the program; the reason travels over the pipe.
constexpr int SPAWN_FAILED_EXIT_CODE = 127;

auto make_errno_error_code(int code) -> std::error_code
{
    return std::error_code(code, std::generic_category());
}

/// Everything the child needs, prepared before fork: after it only async-signal-safe calls are made.
struct spawn_request
{
    std::vector<std::string> argument_storage;
    std::vector<char*> argv;
    std::string working_directory;
    int descriptor_limit{};
    int error_pipe{};
};

auto get_descriptor_limit() -> int
{
    const long limit = sysconf(_SC_OPEN_MAX);
    return int(limit <= 0 ? DESCRIPTOR_LIMIT_FALLBACK : std::min(limit, DESCRIPTOR_LIMIT_MAX));
}

auto open_cloexec_pipe(int (&descriptors)[2]) -> bool
{
    if(pipe(descriptors) != 0)
    {
        return false;
    }
    fcntl(descriptors[0], F_SETFD, FD_CLOEXEC);
    fcntl(descriptors[1], F_SETFD, FD_CLOEXEC);
    return true;
}

/// Runs in the grandchild: a session of its own, the working directory, the standard streams on the
/// null device, nothing else of the caller's open, then the program. Never returns.
[[noreturn]] void exec_detached(const spawn_request& request)
{
    setsid();
    int error = 0;
    if(!request.working_directory.empty() && chdir(request.working_directory.c_str()) != 0)
    {
        error = errno;
    }
    const int null_device = error == 0 ? open(NULL_DEVICE, O_RDWR) : -1;
    for(int stream = 0; stream < FIRST_INHERITED_DESCRIPTOR && null_device >= 0; ++stream)
    {
        dup2(null_device, stream);
    }
    for(int descriptor = FIRST_INHERITED_DESCRIPTOR; descriptor < request.descriptor_limit; ++descriptor)
    {
        if(descriptor != request.error_pipe)
        {
            close(descriptor);
        }
    }
    if(error == 0)
    {
        execv(request.argv.front(), request.argv.data());
        error = errno;
    }
    // The pipe closes on a successful exec; reaching here, the caller reads why it failed.
    [[maybe_unused]] const auto written = write(request.error_pipe, &error, sizeof(error));
    _exit(SPAWN_FAILED_EXIT_CODE);
}

/// What the grandchild reported: 0 once the program runs (the pipe closed on exec), else the errno.
auto read_spawn_error(int read_end) -> int
{
    int error = 0;
    ssize_t count = 0;
    do
    {
        count = read(read_end, &error, sizeof(error));
    } while(count < 0 && errno == EINTR);
    return count == ssize_t(sizeof(error)) ? error : 0;
}
} // namespace

auto spawn_detached(const std::vector<std::string>& arguments, const std::string& working_directory) -> std::error_code
{
    if(arguments.empty() || arguments.front().empty())
    {
        return make_errno_error_code(ENOENT);
    }
    spawn_request request;
    request.argument_storage = arguments;
    for(auto& argument : request.argument_storage)
    {
        request.argv.push_back(argument.data());
    }
    request.argv.push_back(nullptr);
    request.working_directory = working_directory;
    request.descriptor_limit = get_descriptor_limit();
    int error_pipe[2] = {-1, -1};
    if(!open_cloexec_pipe(error_pipe))
    {
        return make_errno_error_code(errno);
    }
    request.error_pipe = error_pipe[1];
    // Double fork: the middle process exits at once, so the program is adopted by init and never
    // waits as a zombie of this process, whatever this process does later (even an in-place restart).
    const pid_t middle = fork();
    if(middle == 0)
    {
        const pid_t program = fork();
        if(program == 0)
        {
            exec_detached(request);
        }
        if(program < 0)
        {
            const int fork_error = errno;
            [[maybe_unused]] const auto written = write(request.error_pipe, &fork_error, sizeof(fork_error));
        }
        _exit(0);
    }
    close(error_pipe[1]);
    if(middle < 0)
    {
        const int fork_error = errno;
        close(error_pipe[0]);
        return make_errno_error_code(fork_error);
    }
    int status = 0;
    while(waitpid(middle, &status, 0) < 0 && errno == EINTR)
    {
    }
    const int spawn_error = read_spawn_error(error_pipe[0]);
    close(error_pipe[0]);
    return spawn_error == 0 ? std::error_code{} : make_errno_error_code(spawn_error);
}

} // namespace process
} // namespace unravel
