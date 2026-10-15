#include "mcp_status.h"

#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/system/mcp_manager.h>

#include <imgui/imgui_internal.h>
#include <logging/logging.h>

#include <chrono>
#include <cmath>

namespace unravel::mcp_status
{
namespace
{
constexpr ImU32 LISTENING_COLOR = IM_COL32(92, 196, 120, 255);
constexpr ImU32 STOPPED_COLOR = IM_COL32(140, 140, 148, 255);
constexpr ImU32 FAILED_COLOR = IM_COL32(255, 95, 95, 255);
/// A request this recent still counts as activity, so a call too quick to see running still blinks.
constexpr auto BUSY_AFTER_REQUEST = std::chrono::milliseconds(900);
/// Pulses of the busy ring per second, and how far it grows past the dot.
constexpr float PULSE_RATE = 1.4f;
constexpr float PULSE_GROWTH = 1.6f;
constexpr float PULSE_START_ALPHA = 0.55f;
constexpr float PULSE_THICKNESS = 1.5f;
constexpr float TOOLTIP_TITLE_SCALE = 1.1f;
/// The tooltip wraps its summary at this many font sizes.
constexpr float TOOLTIP_WRAP_WIDTH = 26.0f;
constexpr int SECONDS_PER_MINUTE = 60;
constexpr int SECONDS_PER_HOUR = 3600;

auto is_recent_request(const mcp_manager& mcp) -> bool
{
    const auto last = mcp.get_last_request_time();
    return last && std::chrono::steady_clock::now() - *last < BUSY_AFTER_REQUEST;
}

auto with_alpha(ImU32 color, float alpha) -> ImU32
{
    const auto base_alpha = float((color >> IM_COL32_A_SHIFT) & 0xFF);
    return (color & ~IM_COL32_A_MASK) | (ImU32(base_alpha * ImClamp(alpha, 0.0f, 1.0f)) << IM_COL32_A_SHIFT);
}

/// A dim label and its value on one line.
void draw_fact(const char* label, const std::string& value)
{
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(value.c_str());
}
} // namespace

auto get_status(const mcp_manager& mcp) -> status
{
    if(mcp.has_listen_failed())
    {
        return {server_state::failed, FAILED_COLOR, "Port in use"};
    }
    if(!mcp.is_running())
    {
        return {server_state::stopped, STOPPED_COLOR, "Stopped"};
    }
    if(mcp.get_active_call_count() > 0 || is_recent_request(mcp))
    {
        return {server_state::busy, ImGui::GetColorU32(imgui_style::get_accent_color()), "Working"};
    }
    return {server_state::listening, LISTENING_COLOR, "Listening"};
}

void draw_dot(ImDrawList* draw_list, const ImVec2& center, float radius, const status& state)
{
    if(state.state == server_state::busy)
    {
        const float phase = std::fmod(float(ImGui::GetTime()) * PULSE_RATE, 1.0f);
        const float ring_radius = radius * (1.0f + phase * PULSE_GROWTH);
        draw_list->AddCircle(center, ring_radius, with_alpha(state.color, PULSE_START_ALPHA * (1.0f - phase)), 0, PULSE_THICKNESS);
    }
    if(state.state == server_state::stopped)
    {
        draw_list->AddCircle(center, radius - PULSE_THICKNESS * 0.5f, state.color, 0, PULSE_THICKNESS);
        return;
    }
    draw_list->AddCircleFilled(center, radius, state.color);
}

auto get_summary(const mcp_manager& mcp, const status& state) -> std::string
{
    switch(state.state)
    {
        case server_state::failed:
            return fmt::format("Could not listen on port {}: another program, or another editor, holds it", mcp.get_port());
        case server_state::stopped:
            return mcp.is_enabled() ? "The server is stopped; clients cannot connect" : "The server is disabled";
        case server_state::busy:
        {
            const int calls = mcp.get_active_call_count();
            const std::string tool = mcp.get_last_tool_name();
            if(calls > 0 && !tool.empty())
            {
                return calls == 1 ? fmt::format("Running {}", tool) : fmt::format("Running {} calls, last {}", calls, tool);
            }
            return "Answering a request";
        }
        default:
            return "Clients connect to " + mcp.get_endpoint_url();
    }
}

auto format_last_request(const mcp_manager& mcp) -> std::string
{
    const auto last = mcp.get_last_request_time();
    if(!last)
    {
        return "No requests yet";
    }
    const auto seconds =
        int(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - *last).count());
    if(seconds < 1)
    {
        return "Just now";
    }
    if(seconds < SECONDS_PER_MINUTE)
    {
        return fmt::format("{} s ago", seconds);
    }
    if(seconds < SECONDS_PER_HOUR)
    {
        return fmt::format("{} min ago", seconds / SECONDS_PER_MINUTE);
    }
    return fmt::format("{} h ago", seconds / SECONDS_PER_HOUR);
}

void draw_tooltip(const mcp_manager& mcp, const status& state)
{
    if(!ImGui::BeginTooltip())
    {
        return;
    }
    ImGui::PushFont(ImGui::GetFont(ImGui::Font::Bold), ImGui::GetCurrentContext()->FontSizeBase * TOOLTIP_TITLE_SCALE);
    ImGui::TextUnformatted("MCP Server");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, state.color);
    ImGui::TextUnformatted(state.title);
    ImGui::PopStyleColor();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * TOOLTIP_WRAP_WIDTH);
    ImGui::TextUnformatted(get_summary(mcp, state).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    draw_fact("Endpoint", mcp.get_endpoint_url());
    draw_fact("Profile", fmt::format("{}, {} of {} tools", mcp.get_profile_name(), mcp.get_visible_tool_count(), mcp.get_tool_count()));
    draw_fact("Requests",
              fmt::format("{}  Calls {}  Errors {}", mcp.get_request_count(), mcp.get_tool_call_count(), mcp.get_error_count()));
    const std::string tool = mcp.get_last_tool_name();
    draw_fact("Last request", tool.empty() ? format_last_request(mcp) : fmt::format("{} ({})", format_last_request(mcp), tool));
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    ImGui::TextUnformatted("Click to open the MCP Server window.");
    ImGui::PopStyleColor();
    ImGui::EndTooltip();
}

} // namespace unravel::mcp_status
