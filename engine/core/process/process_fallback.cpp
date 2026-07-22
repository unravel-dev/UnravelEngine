#include "process.h"

#include <cerrno>
#include <system_error>

namespace unravel
{
namespace process
{
namespace
{

auto make_unsupported_error() -> std::error_code
{
    return std::error_code(ENOSYS, std::generic_category());
}

} // namespace

auto get_executable_path() -> std::string
{
    return {};
}

auto get_current_process_id() -> std::uint64_t
{
    return 0;
}

auto spawn_replacement(const std::vector<std::string>& arguments, std::uint32_t restart_count) -> restart_result
{
    (void)arguments;
    (void)restart_count;
    restart_result result{};
    result.error = make_unsupported_error();
    return result;
}

auto wait_for_process_exit(std::uint64_t process_id) -> bool
{
    (void)process_id;
    return false;
}

} // namespace process
} // namespace unravel
