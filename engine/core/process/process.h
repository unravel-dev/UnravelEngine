#pragma once

/**
 * @file process.h
 * @brief Cross-platform process restart helpers.
 *
 * Static/global constructors must not acquire exclusive resources (locks, sockets,
 * GPU, windows, writable files). Resource-sensitive init runs only after
 * wait_for_process_exit when started with --restart-from-pid.
 *
 * Lifetime token for waiting is currently the old process PID. A stronger token
 * (pipe / pidfd) may be added later without changing the public restart flow.
 */

#include <cstdint>
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
 * @brief Spawns a detached replacement process of the current executable.
 *
 * @param arguments Application arguments only (no argv[0], no internal restart flags).
 *        Internal restart markers are appended by this function.
 * @param restart_count Consecutive restart count to embed in the replacement command line.
 */
[[nodiscard]] auto spawn_replacement(const std::vector<std::string>& arguments,
                                     std::uint32_t restart_count) -> restart_result;

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
 */
[[nodiscard]] auto build_replacement_command_arguments(const std::vector<std::string>& application_arguments,
                                                       std::uint32_t restart_count) -> std::vector<std::string>;

} // namespace process
} // namespace unravel
