#include "https_get.h"
#include "https_get_platform.h"

namespace unravel::update_check
{
namespace
{

constexpr hpp::string_view HTTPS_PREFIX = "https://";

} // namespace

auto is_https_url(hpp::string_view url) -> bool
{
    return url.size() > HTTPS_PREFIX.size() && url.substr(0, HTTPS_PREFIX.size()) == HTTPS_PREFIX;
}

auto https_get(const std::string& url, const https_limits& limits, const stop_check& should_stop) -> https_result
{
    if(!is_https_url(url))
    {
        return {hpp::nullopt, "Only https:// addresses are fetched, not " + url + "."};
    }
    const auto deadline = std::chrono::steady_clock::now() + limits.total_time;
    const stop_check should_end = [deadline, should_stop]()
    {
        return std::chrono::steady_clock::now() >= deadline || (should_stop && should_stop());
    };
    return detail::get_over_https(url, limits, should_end);
}

} // namespace unravel::update_check
