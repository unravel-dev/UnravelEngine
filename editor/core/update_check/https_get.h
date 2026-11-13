#pragma once

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>

namespace unravel::update_check
{

/// The product name sent as User-Agent. Nothing about the user, the computer or this build is sent.
inline constexpr const char* USER_AGENT = "UnravelEngine-Editor";
constexpr std::chrono::seconds DEFAULT_TOTAL_TIME{10};
constexpr std::size_t DEFAULT_MAX_BODY_SIZE = 64 * 1024;
constexpr int DEFAULT_MAX_REDIRECTS = 5;

/// The bounds of one request.
struct https_limits
{
    /// From the start of the request to the end of its body, redirects included.
    std::chrono::seconds total_time = DEFAULT_TOTAL_TIME;
    /// A larger body ends the request with no answer.
    std::size_t max_body_size = DEFAULT_MAX_BODY_SIZE;
    int max_redirects = DEFAULT_MAX_REDIRECTS;
};

/// What a request gave: the body, or why there is none.
struct https_result
{
    /// The body of a 200 answer that came within the limits.
    hpp::optional<std::string> body;
    /// Why there is no body. For the log only.
    std::string error;
};

/// Asked a few times a second while a request runs; true ends it.
using stop_check = std::function<bool()>;

/// Whether `url` is an https:// address.
auto is_https_url(hpp::string_view url) -> bool;

/**
 * @brief GETs an https:// address on the calling thread.
 *
 * Any other address is refused before anything starts. Proxies and trusted certificates are the system's, and
 * certificates are always checked. On Windows the request goes through WinHTTP, elsewhere through wget, or curl
 * when there is no wget. No cookie is kept or sent.
 *
 * @param should_stop Ends the request early when it returns true; may be empty.
 */
auto https_get(const std::string& url, const https_limits& limits = {}, const stop_check& should_stop = {})
    -> https_result;

} // namespace unravel::update_check
