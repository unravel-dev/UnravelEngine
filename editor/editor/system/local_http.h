#pragma once

#include <string>

namespace httplib
{
struct Request;
}

namespace unravel
{

/**
 * @brief Whether an HTTP request to one of the editor's local servers (MCP, the web deploy preview) comes from this
 * computer. A request's Origin, when it has one, and its Host must name the loopback host: a web page elsewhere sends
 * its own Origin, and a DNS-rebinding page its own Host, so both are refused - otherwise any site open in a browser
 * could reach the server.
 * @param refusal Receives why the request is refused.
 */
auto is_loopback_request(const httplib::Request& request, std::string& refusal) -> bool;

} // namespace unravel
