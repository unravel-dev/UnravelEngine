#include "local_http.h"

#include <httplib.h>

#include <algorithm>
#include <cctype>

namespace unravel
{
namespace
{
/// The host part of an Origin ("http://localhost:3000") or Host ("127.0.0.1:27182") value.
auto host_of(std::string value) -> std::string
{
    const auto scheme = value.find("://");
    if(scheme != std::string::npos)
    {
        value = value.substr(scheme + 3);
    }
    if(!value.empty() && value.front() == '[')
    {
        return value.substr(0, value.find(']') + 1);
    }
    return value.substr(0, value.find_first_of(":/"));
}

auto is_local_host(const std::string& host) -> bool
{
    std::string lower(host);
    std::transform(lower.begin(),
                   lower.end(),
                   lower.begin(),
                   [](unsigned char c)
                   {
                       return static_cast<char>(std::tolower(c));
                   });
    return lower == "127.0.0.1" || lower == "localhost" || lower == "[::1]";
}
} // namespace

auto is_loopback_request(const httplib::Request& request, std::string& refusal) -> bool
{
    if(request.has_header("Origin") && !is_local_host(host_of(request.get_header_value("Origin"))))
    {
        refusal = "Requests from web pages are refused (Origin " + request.get_header_value("Origin") + ")";
        return false;
    }
    if(request.has_header("Host") && !is_local_host(host_of(request.get_header_value("Host"))))
    {
        refusal = "Requests must address the loopback host (Host " + request.get_header_value("Host") + ")";
        return false;
    }
    return true;
}

} // namespace unravel
