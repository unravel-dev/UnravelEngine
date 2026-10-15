#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <imgui/imgui.h>

#include <array>
#include <chrono>
#include <string>
#include <vector>

namespace unravel
{
class imgui_panels;
class mcp_manager;
struct mcp_activity_entry;

class mcp_panel
{
public:
    /// The kinds of activity the server logs, each with a filter toggle.
    static constexpr size_t k_category_count = 4;

    explicit mcp_panel(imgui_panels* parent);

    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

    void show(bool s);

private:
    void draw_ui(rtti::context& ctx);
    /// Start / stop, copy the endpoint, search; the activity filters and Clear on the right.
    void draw_toolbar(mcp_manager& mcp, const std::vector<mcp_activity_entry>& activity);
    void draw_category_filters(const std::vector<mcp_activity_entry>& activity);
    /// The state of the server, with its counters.
    void draw_status_card(const mcp_manager& mcp);
    /// Where clients connect, the profile, and a configuration to paste into a client.
    void draw_connection_section(const mcp_manager& mcp);
    /// The log of requests and calls, the newest at the bottom.
    void draw_activity_section(mcp_manager& mcp, const std::vector<mcp_activity_entry>& activity);
    void draw_activity_rows(const std::vector<const mcp_activity_entry*>& rows);
    auto is_entry_visible(const mcp_activity_entry& entry) const -> bool;

    imgui_panels* parent_{};
    bool show_request_{};
    bool show_{false};
    ImGuiTextFilter filter_;
    std::array<bool, k_category_count> shown_categories_{true, true, true, true};
    bool is_errors_only_{false};
    /// Time of the newest entry the list showed; a newer one is followed while the list is at its end.
    std::chrono::system_clock::time_point seen_last_entry_time_{};
};
} // namespace unravel
