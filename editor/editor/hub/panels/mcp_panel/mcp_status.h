#pragma once

#include <imgui/imgui.h>

#include <string>

namespace unravel
{
class mcp_manager;

/// The state of the editor's MCP server as the header and the MCP Server window show it.
namespace mcp_status
{

enum class server_state
{
    /// Listening, no call running.
    listening,
    /// A tool call is running, or a request arrived a moment ago.
    busy,
    /// Not listening: stopped, or disabled at start.
    stopped,
    /// The last start could not listen on its port.
    failed
};

struct status
{
    server_state state{server_state::stopped};
    /// The color of the state: its dot, and its title in the window.
    ImU32 color{};
    /// One or two words, such as "Listening".
    const char* title{};
};

auto get_status(const mcp_manager& mcp) -> status;

/// The dot of a state, centered on @p center. A busy server's dot sends out a ring that pulses.
void draw_dot(ImDrawList* draw_list, const ImVec2& center, float radius, const status& state);

/// One line on what the state means, such as "Clients connect to http://127.0.0.1:27182/mcp".
auto get_summary(const mcp_manager& mcp, const status& state) -> std::string;

/// "3 s ago", "2 min ago" for the last request; "No requests yet" before the first one.
auto format_last_request(const mcp_manager& mcp) -> std::string;

/// The tooltip of the header's MCP indicator: state, endpoint, profile, counters and last request.
void draw_tooltip(const mcp_manager& mcp, const status& state);

} // namespace mcp_status
} // namespace unravel
