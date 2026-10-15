#include "mcp_panel.h"
#include "mcp_status.h"

#include "../inspector_panel/inspectors/inspector.h"
#include "../panel_section.h"
#include "../panel_toolbar.h"
#include "../panels_defs.h"

#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_context_menu_style.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/system/mcp_manager.h>

#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>
#include <logging/logging.h>

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace unravel
{
// A unity build merges the anonymous namespaces of the editor's sources; the window's own namespace
// keeps its helper names from clashing.
namespace mcp_window
{
namespace
{
/// A kind of activity the server logs, with its filter toggle and its mark in the list.
struct activity_category
{
    /// The category the server logs it under.
    const char* key;
    const char* icon;
    const char* name;
    ImU32 color;
};

constexpr std::array<activity_category, mcp_panel::k_category_count> ACTIVITY_CATEGORIES{{
    {"tool", ICON_MDI_TOOLS, "Tools", IM_COL32(120, 170, 230, 255)},
    {"rpc", ICON_MDI_SWAP_HORIZONTAL, "Protocol", IM_COL32(176, 150, 226, 255)},
    {"http", ICON_MDI_WEB, "HTTP", IM_COL32(230, 160, 90, 255)},
    {"lifecycle", ICON_MDI_POWER, "Server", IM_COL32(110, 190, 130, 255)},
}};
/// Activity under a category the table does not know counts as the server's.
constexpr size_t FALLBACK_CATEGORY = 3;
constexpr ImU32 ERROR_COLOR = IM_COL32(255, 95, 95, 255);
/// The name the configuration offered to clients gives the server.
constexpr const char* CLIENT_SERVER_NAME = "unravel-editor";

// Sizes in units of the font size.
constexpr float SEARCH_FIELD_WIDTH = 14.0f;
constexpr float SEARCH_FIELD_MIN_WIDTH = 5.0f;
constexpr float CARD_ROUNDING = 0.5f;
constexpr float CARD_PADDING = 0.75f;
constexpr ImU32 CARD_COLOR = IM_COL32(0, 0, 0, 46);
constexpr float STATUS_DOT_RADIUS = 0.42f;
constexpr float STATUS_DOT_GAP = 0.7f;
constexpr float STATUS_TITLE_SCALE = 1.3f;
constexpr float TILE_HEIGHT = 2.9f;
constexpr float TILE_GAP = 0.5f;
constexpr float TILE_PADDING = 0.6f;
constexpr float TILE_ROUNDING = 0.35f;
constexpr float TILE_VALUE_SCALE = 1.25f;
constexpr float TILE_LABEL_SCALE = 0.85f;
constexpr ImU32 TILE_COLOR = IM_COL32(255, 255, 255, 10);
constexpr int TILE_COUNT = 4;
constexpr float ROW_PADDING_X = 0.45f;
constexpr float ROW_TIME_COLUMN = 4.6f;
constexpr float ROW_CATEGORY_COLUMN = 5.6f;
constexpr ImU32 ROW_STRIPE_COLOR = IM_COL32(255, 255, 255, 7);
constexpr float ROW_HOVERED_ALPHA = 0.45f;
constexpr float LIST_MIN_HEIGHT = 6.0f;

auto format_timestamp(std::chrono::system_clock::time_point tp) -> std::string
{
    const auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%H:%M:%S");
    return oss.str();
}

auto find_category(const std::string& key) -> size_t
{
    for(size_t index = 0; index < ACTIVITY_CATEGORIES.size(); ++index)
    {
        if(key == ACTIVITY_CATEGORIES[index].key)
        {
            return index;
        }
    }
    return FALLBACK_CATEGORY;
}

/// The configuration an MCP client takes to connect to this editor.
auto make_client_config(const std::string& endpoint) -> std::string
{
    return fmt::format("{{\n  \"mcpServers\": {{\n    \"{}\": {{\n      \"type\": \"http\",\n      \"url\": \"{}\"\n    }}\n  }}\n}}",
                       CLIENT_SERVER_NAME,
                       endpoint);
}

auto get_scaled_font_size(float scale) -> float
{
    return ImGui::GetCurrentContext()->FontSizeBase * scale;
}

/// A flat icon button that copies @p text.
void draw_copy_button(const char* id, const std::string& text, const char* tooltip)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if(ImGui::SmallButton(id))
    {
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltipEx("%s", tooltip);
}

/// A label and a value in the layout of the inspector, with a copy button after the value when asked.
void draw_value_row(const char* label, const char* tooltip, const std::string& value, const char* copy_id = nullptr)
{
    property_layout layout(label, tooltip);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(value.c_str());
    if(copy_id != nullptr)
    {
        ImGui::SameLine();
        draw_copy_button(copy_id, value, "Copy");
    }
}

/// A counter: its value large, its label small and muted under it.
void draw_tile(const char* label, const std::string& value, ImU32 value_color, float width)
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, font_size * TILE_HEIGHT);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(min, min + size, TILE_COLOR, font_size * TILE_ROUNDING);
    const float padding = font_size * TILE_PADDING;
    draw_list->PushClipRect(min, min + size, true);
    const float value_size = font_size * TILE_VALUE_SCALE;
    draw_list->AddText(ImGui::GetFont(ImGui::Font::Bold), value_size, min + ImVec2(padding, padding * 0.6f), value_color, value.c_str());
    draw_list->AddText(ImGui::GetFont(),
                       font_size * TILE_LABEL_SCALE,
                       ImVec2(min.x + padding, min.y + size.y - padding * 0.6f - font_size * TILE_LABEL_SCALE),
                       imgui_style::get_muted_text_color_u32(),
                       label);
    draw_list->PopClipRect();
    ImGui::Dummy(size);
}

void draw_row_background(const ImRect& rect, int row_index, bool is_hovered)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if(is_hovered)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(ImGuiCol_HeaderHovered, ROW_HOVERED_ALPHA));
        return;
    }
    if(row_index % 2 == 1)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, ROW_STRIPE_COLOR);
    }
}

/// One entry on one line: the time, the mark of its category, and the message, red for an error.
void draw_row_content(const mcp_activity_entry& entry, const ImRect& rect)
{
    const activity_category& category = ACTIVITY_CATEGORIES[find_category(entry.category)];
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float font_size = ImGui::GetFontSize();
    const float text_y = ImFloor(rect.Min.y + (rect.GetHeight() - font_size) * 0.5f);
    float x = rect.Min.x + font_size * ROW_PADDING_X;
    draw_list->AddText(ImVec2(x, text_y), imgui_style::get_muted_text_color_u32(), format_timestamp(entry.timestamp).c_str());
    x += font_size * ROW_TIME_COLUMN;
    const char* icon = entry.is_error ? ICON_MDI_ALERT_OCTAGON : category.icon;
    draw_list->AddText(ImVec2(x, text_y), entry.is_error ? ERROR_COLOR : category.color, icon);
    draw_list->AddText(ImVec2(x + font_size * 1.4f, text_y), imgui_style::get_muted_text_color_u32(), category.name);
    x += font_size * ROW_CATEGORY_COLUMN;
    const ImVec2 message_min(x, text_y);
    const ImVec2 message_max(rect.Max.x - font_size * ROW_PADDING_X, rect.Max.y);
    ImGui::PushStyleColor(ImGuiCol_Text, entry.is_error ? ERROR_COLOR : ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::RenderTextEllipsis(draw_list, message_min, message_max, message_max.x, entry.message.c_str(), nullptr, nullptr);
    ImGui::PopStyleColor();
}

void draw_centered_hint(const char* title, const std::string& detail)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float line_height = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 origin = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(origin.x + (avail.x - ImGui::CalcTextSize(title).x) * 0.5f, origin.y + avail.y * 0.5f - line_height));
    ImGui::TextUnformatted(title);
    ImGui::SetCursorPosX(origin.x + (avail.x - ImGui::CalcTextSize(detail.c_str()).x) * 0.5f);
    ImGui::TextDisabled("%s", detail.c_str());
}
} // namespace
} // namespace mcp_window

mcp_panel::mcp_panel(imgui_panels* parent) : parent_(parent)
{
}

void mcp_panel::init(rtti::context& ctx)
{
}

void mcp_panel::deinit(rtti::context& ctx)
{
}

void mcp_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(show_request_)
    {
        show_request_ = false;
        show_ = true;
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * 0.45f, ImGuiCond_Once);
        ImGui::SetWindowFocus(name);
    }

    if(!show_)
    {
        return;
    }

    if(ImGui::Begin(name, &show_))
    {
        draw_ui(ctx);
    }
    ImGui::End();
}

void mcp_panel::show(bool s)
{
    show_request_ = s;
}

void mcp_panel::draw_ui(rtti::context& ctx)
{
    auto& mcp = ctx.get_cached<mcp_manager>();
    const auto activity = mcp.snapshot_activity();
    draw_toolbar(mcp, activity);
    draw_status_card(mcp);
    draw_connection_section(mcp);
    draw_activity_section(mcp, activity);
}

void mcp_panel::draw_toolbar(mcp_manager& mcp, const std::vector<mcp_activity_entry>& activity)
{
    if(panel_toolbar::begin_strip("##mcp_toolbar"))
    {
        const bool is_running = mcp.is_running();
        const char* start_stop = is_running ? ICON_MDI_STOP " Stop" : ICON_MDI_PLAY " Start";
        const char* start_stop_tooltip =
            is_running ? "Stop the server; connected clients lose it" : "Start listening for MCP clients";
        if(panel_toolbar::button("##start_stop", start_stop, start_stop_tooltip, !is_running, ICON_MDI_PLAY " Start"))
        {
            if(is_running)
            {
                mcp.stop();
            }
            else
            {
                mcp.start();
            }
        }
        if(panel_toolbar::button("##copy_endpoint", ICON_MDI_CONTENT_COPY " Copy URL", "Copy the endpoint clients connect to"))
        {
            ImGui::SetClipboardText(mcp.get_endpoint_url().c_str());
        }
        panel_toolbar::separator();
        const float font_size = ImGui::GetFontSize();
        const float width =
            panel_toolbar::calc_flexible_width(font_size * mcp_window::SEARCH_FIELD_MIN_WIDTH, font_size * mcp_window::SEARCH_FIELD_WIDTH);
        panel_toolbar::begin_field(width);
        ImGui::DrawFilterWithHint(filter_, ICON_MDI_MAGNIFY " Search activity...", width);
        ImGui::DrawItemActivityOutline();
        panel_toolbar::end_field();
        panel_toolbar::align_right();
        draw_category_filters(activity);
        panel_toolbar::separator();
        if(panel_toolbar::button("##clear", ICON_MDI_DELETE_SWEEP, "Clear the activity"))
        {
            mcp.clear_activity();
        }
    }
    panel_toolbar::end_strip();
}

void mcp_panel::draw_category_filters(const std::vector<mcp_activity_entry>& activity)
{
    std::array<size_t, k_category_count> counts{};
    size_t error_count = 0;
    for(const auto& entry : activity)
    {
        ++counts[mcp_window::find_category(entry.category)];
        error_count += entry.is_error ? 1 : 0;
    }
    for(size_t index = 0; index < mcp_window::ACTIVITY_CATEGORIES.size(); ++index)
    {
        const mcp_window::activity_category& category = mcp_window::ACTIVITY_CATEGORIES[index];
        const std::string id = fmt::format("##category_{}", category.key);
        const std::string text = fmt::format("{} {}", category.icon, counts[index]);
        const std::string tooltip = fmt::format("Show / hide {} activity ({})", category.name, counts[index]);
        if(panel_toolbar::filter_toggle(id.c_str(), text.c_str(), shown_categories_[index], category.color, tooltip.c_str()))
        {
            shown_categories_[index] = !shown_categories_[index];
        }
    }
    panel_toolbar::separator();
    const std::string errors_text = fmt::format("{} {}", ICON_MDI_ALERT_OCTAGON, error_count);
    if(panel_toolbar::filter_toggle("##errors_only", errors_text.c_str(), is_errors_only_, mcp_window::ERROR_COLOR, "Show errors only"))
    {
        is_errors_only_ = !is_errors_only_;
    }
}

void mcp_panel::draw_status_card(const mcp_manager& mcp)
{
    const mcp_status::status state = mcp_status::get_status(mcp);
    const float font_size = ImGui::GetFontSize();
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, font_size * mcp_window::CARD_ROUNDING);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(font_size * mcp_window::CARD_PADDING, font_size * mcp_window::CARD_PADDING));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, mcp_window::CARD_COLOR);
    if(ImGui::BeginChild("##mcp_status", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY))
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float title_size = mcp_window::get_scaled_font_size(mcp_window::STATUS_TITLE_SCALE);
        const float dot_radius = font_size * mcp_window::STATUS_DOT_RADIUS;
        mcp_status::draw_dot(ImGui::GetWindowDrawList(),
                             ImVec2(origin.x + dot_radius, origin.y + ImGui::GetFontSize() * mcp_window::STATUS_TITLE_SCALE * 0.5f),
                             dot_radius,
                             state);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + dot_radius * 2.0f + font_size * mcp_window::STATUS_DOT_GAP, origin.y));
        ImGui::BeginGroup();
        ImGui::PushFont(ImGui::GetFont(ImGui::Font::Bold), title_size);
        ImGui::TextUnformatted(state.title);
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(mcp_status::get_summary(mcp, state).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::Spacing();
        const float gap = font_size * mcp_window::TILE_GAP;
        const float tile_width = ImFloor((ImGui::GetContentRegionAvail().x - gap * float(mcp_window::TILE_COUNT - 1)) / float(mcp_window::TILE_COUNT));
        const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
        mcp_window::draw_tile("Requests", std::to_string(mcp.get_request_count()), text_color, tile_width);
        ImGui::SameLine(0.0f, gap);
        mcp_window::draw_tile("Tool Calls", std::to_string(mcp.get_tool_call_count()), text_color, tile_width);
        ImGui::SameLine(0.0f, gap);
        const uint64_t errors = mcp.get_error_count();
        mcp_window::draw_tile("Errors", std::to_string(errors), errors > 0 ? mcp_window::ERROR_COLOR : text_color, tile_width);
        ImGui::SameLine(0.0f, gap);
        mcp_window::draw_tile("Last Request", mcp_status::format_last_request(mcp), text_color, tile_width);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void mcp_panel::draw_connection_section(const mcp_manager& mcp)
{
    if(!panel_section::draw_header("##mcp_connection", ICON_MDI_LAN_CONNECT, "Connection", true).is_open)
    {
        return;
    }
    const std::string endpoint = mcp.get_endpoint_url();
    mcp_window::draw_value_row("Endpoint", "Where clients send their requests (POST, JSON-RPC)", endpoint, ICON_MDI_CONTENT_COPY "##copy_endpoint");
    mcp_window::draw_value_row("Health", "Answers a GET with ok while the server runs", mcp.get_health_url(), ICON_MDI_CONTENT_COPY "##copy_health");
    mcp_window::draw_value_row("Access",
                   "Bound to the loopback address; requests that come from a web page are refused",
                   fmt::format("{}:{}, this computer only", mcp.get_host(), mcp.get_port()));
    mcp_window::draw_value_row("Profile",
                   "The tools clients see. Start the editor with --mcp-profile to pick another set",
                   fmt::format("{}, {} of {} tools", mcp.get_profile_name(), mcp.get_visible_tool_count(), mcp.get_tool_count()));
    const std::string config = mcp_window::make_client_config(endpoint);
    property_layout layout("Client Config", "Add this to the MCP configuration of a client to connect it to this editor");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(fmt::format("\"{}\": {{ \"type\": \"http\", ... }}", mcp_window::CLIENT_SERVER_NAME).c_str());
    if(ImGui::BeginItemTooltip())
    {
        ImGui::PushFont(ImGui::GetFont(ImGui::Font::Mono), 0.0f);
        ImGui::TextUnformatted(config.c_str());
        ImGui::PopFont();
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    mcp_window::draw_copy_button(ICON_MDI_CONTENT_COPY "##copy_config", config, "Copy the configuration");
}

void mcp_panel::draw_activity_section(mcp_manager& mcp, const std::vector<mcp_activity_entry>& activity)
{
    if(!panel_section::draw_header("##mcp_activity", ICON_MDI_HISTORY, "Activity", true).is_open)
    {
        return;
    }
    std::vector<const mcp_activity_entry*> rows;
    rows.reserve(activity.size());
    for(const auto& entry : activity)
    {
        if(is_entry_visible(entry))
        {
            rows.push_back(&entry);
        }
    }
    const float height = ImMax(ImGui::GetContentRegionAvail().y, ImGui::GetFontSize() * mcp_window::LIST_MIN_HEIGHT);
    if(ImGui::BeginChild("##mcp_activity_list", ImVec2(0.0f, height)))
    {
        if(ImGui::BeginPopupContextWindow("##activity_menu", ImGuiPopupFlags_MouseButtonRight))
        {
            ImGui::ContextMenuStyleScope style_scope;
            if(ImGui::MenuItemIcon(ICON_MDI_DELETE_SWEEP, "Clear"))
            {
                mcp.clear_activity();
            }
            ImGui::EndPopup();
        }
        if(activity.empty())
        {
            mcp_window::draw_centered_hint("No activity yet", "Connect a client to " + mcp.get_endpoint_url());
        }
        else if(rows.empty())
        {
            mcp_window::draw_centered_hint("Nothing to show", "No entry passes the search and the filters");
        }
        else
        {
            draw_activity_rows(rows);
        }
    }
    ImGui::EndChild();
    seen_last_entry_time_ = activity.empty() ? std::chrono::system_clock::time_point{} : activity.back().timestamp;
}

void mcp_panel::draw_activity_rows(const std::vector<const mcp_activity_entry*>& rows)
{
    const bool has_new_entries = rows.back()->timestamp != seen_last_entry_time_;
    // Rows tile without a gap, so the stripes read as one surface.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGuiListClipper clipper;
    clipper.Begin(int(rows.size()), ImGui::GetFrameHeight());
    while(clipper.Step())
    {
        for(int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
        {
            const mcp_activity_entry& entry = *rows[size_t(index)];
            const ImVec2 row_min = ImGui::GetCursorScreenPos();
            const ImRect rect(row_min, row_min + ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
            ImGui::PushID(index);
            ImGui::InvisibleButton("##row", rect.GetSize());
            mcp_window::draw_row_background(rect, index, ImGui::IsItemHovered());
            mcp_window::draw_row_content(entry, rect);
            ImGui::SetItemTooltipEx("%s", entry.message.c_str());
            if(ImGui::BeginPopupContextItem("##row_menu"))
            {
                ImGui::ContextMenuStyleScope style_scope;
                if(ImGui::MenuItemIcon(ICON_MDI_CONTENT_COPY, "Copy Message"))
                {
                    ImGui::SetClipboardText(entry.message.c_str());
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::PopStyleVar();
    // Follow the log only while the view already sits at its end.
    const bool is_at_end = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
    if(has_new_entries && is_at_end)
    {
        ImGui::SetScrollHereY(1.0f);
    }
}

auto mcp_panel::is_entry_visible(const mcp_activity_entry& entry) const -> bool
{
    if(is_errors_only_ && !entry.is_error)
    {
        return false;
    }
    if(!shown_categories_[mcp_window::find_category(entry.category)])
    {
        return false;
    }
    return filter_.PassFilter(entry.message.c_str()) || filter_.PassFilter(entry.category.c_str());
}

} // namespace unravel
