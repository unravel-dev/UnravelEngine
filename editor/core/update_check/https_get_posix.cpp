#include "https_get_platform.h"

#include <subprocess/subprocess.hpp>

#include <algorithm>
#include <vector>

// There is no HTTPS client in the base system, so the request runs an installed tool: wget, which every Ubuntu
// desktop has, else curl. Both check certificates against the system's store and read the proxy from the
// environment. Each is told to print the body and nothing else, as the output and the errors arrive as one stream.
namespace unravel::update_check::detail
{
namespace
{

/// GNU wget's exit code for a failure without a closer code, and BusyBox wget's for an option it does not know.
constexpr int WGET_GENERIC_ERROR_EXIT_CODE = 1;
/// GNU wget's exit code for an option it cannot parse (older than 1.17 knows no --no-hsts).
constexpr int WGET_PARSE_ERROR_EXIT_CODE = 2;
/// curl's exit code for an option it does not know.
constexpr int CURL_INIT_ERROR_EXIT_CODE = 2;

/// A tool that can run the request, and the exit codes it ends with when it could not run it at all, such as for an
/// option it does not know: the next tool is tried then. Any other failure is the answer, as a check makes one
/// request.
struct download_tool
{
    std::vector<std::string> arguments;
    std::vector<int> unusable_exit_codes;
};

auto make_wget(const std::string& url, const https_limits& limits) -> download_tool
{
    return {{"wget",
             "--quiet",
             "--output-document=-",
             "--tries=1",
             "--timeout=" + std::to_string(limits.total_time.count()),
             "--max-redirect=" + std::to_string(limits.max_redirects),
             // Nothing is written to the user's home folder.
             "--no-hsts",
             "--no-cookies",
             std::string("--user-agent=") + USER_AGENT,
             url},
            {WGET_GENERIC_ERROR_EXIT_CODE, WGET_PARSE_ERROR_EXIT_CODE}};
}

auto make_curl(const std::string& url, const https_limits& limits) -> download_tool
{
    return {{"curl",
             "--silent",
             "--fail",
             "--location",
             "--max-redirs",
             std::to_string(limits.max_redirects),
             "--max-time",
             std::to_string(limits.total_time.count()),
             "--max-filesize",
             std::to_string(limits.max_body_size),
             "--proto",
             "=https",
             "--proto-redir",
             "=https",
             "--user-agent",
             USER_AGENT,
             url},
            {CURL_INIT_ERROR_EXIT_CODE}};
}

auto fail(std::string error) -> https_result
{
    return {hpp::nullopt, std::move(error)};
}

auto is_unusable_exit_code(const download_tool& tool, int exit_code) -> bool
{
    return std::find(tool.unusable_exit_codes.begin(), tool.unusable_exit_codes.end(), exit_code) !=
           tool.unusable_exit_codes.end();
}

/// Runs one tool to its end. Nothing when the tool is not installed or could not run the request at all.
auto run_tool(const download_tool& tool, const https_limits& limits, const stop_check& should_end)
    -> hpp::optional<https_result>
{
    auto result = subprocess::call(tool.arguments, {}, {}, should_end, limits.max_body_size);
    if(!result.started)
    {
        return hpp::nullopt;
    }
    if(result.is_output_cut)
    {
        return fail("The answer is larger than " + std::to_string(limits.max_body_size) + " bytes.");
    }
    if(result.retcode == 0)
    {
        return https_result{std::move(result.out_output), {}};
    }
    if(is_unusable_exit_code(tool, result.retcode))
    {
        return hpp::nullopt;
    }
    return fail(tool.arguments.front() + " ended with exit code " + std::to_string(result.retcode) + ".");
}

} // namespace

auto get_over_https(const std::string& url, const https_limits& limits, const stop_check& should_end) -> https_result
{
    for(const auto& tool : {make_wget(url, limits), make_curl(url, limits)})
    {
        if(should_end())
        {
            return fail("The request was stopped or ran out of time.");
        }
        if(auto result = run_tool(tool, limits, should_end))
        {
            return std::move(*result);
        }
    }
    return fail("Neither wget nor curl is installed and able to run the request.");
}

} // namespace unravel::update_check::detail
