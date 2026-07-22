#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace unravel
{
namespace process
{

inline constexpr const char* RESTART_FROM_PID_PREFIX = "--restart-from-pid=";
inline constexpr const char* RESTARTED_FLAG = "--restarted";
inline constexpr const char* RESTART_COUNT_PREFIX = "--restart-count=";

struct startup_arguments
{
    std::string app_name;
    std::vector<std::string> application_arguments;
    std::optional<std::uint64_t> restart_from_pid;
    bool restarted{false};
    std::uint32_t restart_count{0};
    bool has_parse_error{false};
    std::string parse_error;
};

/**
 * @brief Parses argc/argv, extracts internal restart markers, and preserves user args.
 */
[[nodiscard]] auto parse_startup_arguments(int argc, char* argv[]) -> startup_arguments;

/**
 * @brief Returns true if @p argument is an internal restart marker.
 */
[[nodiscard]] auto is_internal_restart_argument(std::string_view argument) -> bool;

/**
 * @brief Builds argv-style strings for constructing a cmd_line::parser / service.
 *        Index 0 is app_name; remaining entries are application_arguments.
 */
[[nodiscard]] auto build_service_argv(const startup_arguments& startup) -> std::vector<std::string>;

/**
 * @brief Builds the argument list passed to spawn_replacement (user args only).
 */
[[nodiscard]] auto build_replacement_application_arguments(const startup_arguments& startup)
    -> std::vector<std::string>;

} // namespace process
} // namespace unravel
