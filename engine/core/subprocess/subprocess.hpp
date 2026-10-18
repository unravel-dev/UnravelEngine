#pragma once
#include "environment.h"
#include "subprocess.h"
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace subprocess
{
/// Gets one line of a process's output (stdout and stderr together), without its line break.
using line_callback = std::function<void(const std::string& line)>;

struct call_result
{
    /// The exit code; 1 when the process did not start.
    int retcode{1};
    /// Everything it printed, stdout and stderr together; the reason when it did not start.
    std::string out_output;
    std::string err_output;
    /// Whether the process started.
    bool started{};
};

/**
 * @brief Runs a process to completion and returns what it printed.
 *
 * @param args_array The program, then its arguments.
 * @param environment_array "NAME=value" entries that add to or replace this process's environment.
 * @param on_line Gets every output line as it arrives, on this thread; may be empty.
 */
auto call(const std::vector<std::string>& args_array,
          const std::vector<std::string>& environment_array = {},
          const line_callback& on_line = {}) -> call_result;

inline auto call(const std::string& process, const std::vector<std::string>& args_array = {}) -> call_result
{
    std::vector<std::string> merged{process};
    merged.insert(merged.end(), args_array.begin(), args_array.end());

    return call(merged);
}

/**
 * @brief A process that runs until it exits or is stopped, such as a log follower.
 *
 * Its output lines are handed over from a thread of its own. Stopping, or destroying the stream, ends the
 * process and waits for that thread.
 */
class stream
{
public:
    stream() = default;
    ~stream();
    stream(const stream&) = delete;
    auto operator=(const stream&) -> stream& = delete;

    /// Starts the process, stopping one this stream still runs. False when it does not start.
    auto start(const std::vector<std::string>& args_array, line_callback on_line) -> bool;
    void stop();
    /// Whether the process still prints: false once it exited or was stopped.
    auto is_running() const -> bool;

private:
    struct state;
    std::shared_ptr<state> state_;
    std::thread reader_;
};
} // namespace subprocess
