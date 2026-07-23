#pragma once

/**
 * @file process.h
 * @brief Cross-platform process restart helpers.
 *
 * spawn_replacement picks the platform handoff and calls release_resources at the
 * right time:
 * - Sibling spawn (Windows): CreateProcess with --restart-from-pid, then release,
 *   then return so the caller can exit. The child waits for the old PID.
 * - In-place exec (Linux): release, then execve. Same PID / process group so a
 *   controlling terminal keeps delivering SIGINT (Ctrl+C). Does not return on success.
 *
 * Static/global constructors must not acquire exclusive resources (locks, sockets,
 * GPU, windows, writable files). With sibling spawn, resource-sensitive init runs
 * only after wait_for_process_exit when started with --restart-from-pid.
 *
 * Lifetime token for waiting is currently the old process PID. A stronger token
 * (pipe / pidfd) may be added later without changing the public restart flow.
 */

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace unravel
{
namespace process
{

struct restart_result
{
    bool success{false};
    /// True if release_resources already ran (caller must not keep running).
    bool resources_released{false};
    std::error_code error{};
};

/**
 * @brief Resolves the path of the currently running executable.
 */
[[nodiscard]] auto get_executable_path() -> std::string;

/**
 * @brief Returns the current process identifier.
 */
[[nodiscard]] auto get_current_process_id() -> std::uint64_t;

/**
 * @brief Hands off to a restarted instance of the current executable.
 *
 * Platform chooses sibling spawn vs in-place exec and invokes @p release_resources
 * at the correct point in that sequence.
 *
 * @param arguments Application arguments only (no argv[0], no internal restart flags).
 * @param restart_count Consecutive restart count embedded in the replacement command line.
 * @param release_resources Unloads exclusive resources; must return true on success.
 *
 * @return On sibling-spawn success, success=true and the caller should exit.
 *         On in-place exec success, does not return.
 *         On failure, check resources_released before continuing the old instance.
 */
[[nodiscard]] auto spawn_replacement(const std::vector<std::string>& arguments,
                                     std::uint32_t restart_count,
                                     const std::function<bool()>& release_resources) -> restart_result;

/**
 * @brief Blocks until the process with @p process_id terminates.
 *
 * A missing or already-dead process is treated as success.
 */
[[nodiscard]] auto wait_for_process_exit(std::uint64_t process_id) -> bool;

/**
 * @brief Quotes a single argument for Windows CreateProcess command lines.
 */
[[nodiscard]] auto quote_windows_argument(std::string_view argument) -> std::string;

/**
 * @brief Builds a writable Windows command line from an argv-style vector.
 */
[[nodiscard]] auto build_windows_command_line(const std::vector<std::string>& arguments) -> std::wstring;

/**
 * @brief Builds full argv for a replacement process (exe + app args + internal flags).
 *
 * @param include_restart_from_pid When true, append --restart-from-pid=<this pid>
 *        for sibling-spawn handoff. Omit for in-place exec.
 */
[[nodiscard]] auto build_replacement_command_arguments(const std::vector<std::string>& application_arguments,
                                                       std::uint32_t restart_count,
                                                       bool include_restart_from_pid = true)
    -> std::vector<std::string>;

} // namespace process
} // namespace unravel
