#include "startup_arguments.h"

#include <charconv>
#include <cstring>

namespace unravel
{
namespace process
{
namespace
{

auto starts_with(std::string_view value, std::string_view prefix) -> bool
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

auto parse_u64(std::string_view text, std::uint64_t& out_value) -> bool
{
    if(text.empty())
    {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, out_value);
    return ec == std::errc{} && ptr == end;
}

auto parse_u32(std::string_view text, std::uint32_t& out_value) -> bool
{
    if(text.empty())
    {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, out_value);
    return ec == std::errc{} && ptr == end;
}

} // namespace

auto is_internal_restart_argument(std::string_view argument) -> bool
{
    return starts_with(argument, RESTART_FROM_PID_PREFIX) || argument == RESTARTED_FLAG ||
           starts_with(argument, RESTART_COUNT_PREFIX);
}

auto parse_startup_arguments(int argc, char* argv[]) -> startup_arguments
{
    startup_arguments startup{};
    if(argc <= 0 || argv == nullptr || argv[0] == nullptr)
    {
        startup.has_parse_error = true;
        startup.parse_error = "missing argv";
        return startup;
    }
    startup.app_name = argv[0];
    for(int i = 1; i < argc; ++i)
    {
        if(argv[i] == nullptr)
        {
            startup.has_parse_error = true;
            startup.parse_error = "null argument";
            return startup;
        }
        const std::string_view argument = argv[i];
        if(starts_with(argument, RESTART_FROM_PID_PREFIX))
        {
            const std::string_view pid_text = argument.substr(std::strlen(RESTART_FROM_PID_PREFIX));
            std::uint64_t pid = 0;
            if(!parse_u64(pid_text, pid) || pid == 0)
            {
                startup.has_parse_error = true;
                startup.parse_error = "invalid --restart-from-pid value";
                return startup;
            }
            startup.restart_from_pid = pid;
            continue;
        }
        if(argument == RESTARTED_FLAG)
        {
            startup.restarted = true;
            continue;
        }
        if(starts_with(argument, RESTART_COUNT_PREFIX))
        {
            const std::string_view count_text = argument.substr(std::strlen(RESTART_COUNT_PREFIX));
            std::uint32_t count = 0;
            if(!parse_u32(count_text, count))
            {
                startup.has_parse_error = true;
                startup.parse_error = "invalid --restart-count value";
                return startup;
            }
            startup.restart_count = count;
            continue;
        }
        startup.application_arguments.emplace_back(argument);
    }
    return startup;
}

auto build_service_argv(const startup_arguments& startup) -> std::vector<std::string>
{
    std::vector<std::string> argv;
    argv.reserve(1 + startup.application_arguments.size());
    argv.push_back(startup.app_name);
    argv.insert(argv.end(), startup.application_arguments.begin(), startup.application_arguments.end());
    return argv;
}

auto build_replacement_application_arguments(const startup_arguments& startup) -> std::vector<std::string>
{
    return startup.application_arguments;
}

} // namespace process
} // namespace unravel
