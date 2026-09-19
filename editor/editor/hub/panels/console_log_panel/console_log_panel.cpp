#include "console_log_panel.h"
#include "../panel_toolbar.h"
#include "../panels_defs.h"

#include <editor/editing/editor_actions.h>
#include <editor/imgui/integration/imgui_context_menu_style.h>
#include <editor/system/project_manager.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/engine.h>


#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/utils.h>

#include <filesystem/filesystem.h>
#include <map>

namespace unravel
{
namespace
{
struct log_level_style
{
    const char* icon;
    const char* name;
    ImU32 color;
};

// Indexed by level::level_enum.
constexpr std::array<log_level_style, size_t(level::n_levels)> LOG_LEVEL_STYLES{{
    {ICON_MDI_DOTS_HORIZONTAL, "Trace", IM_COL32(150, 150, 150, 255)},
    {ICON_MDI_BUG_OUTLINE, "Debug", IM_COL32(120, 170, 230, 255)},
    {ICON_MDI_INFORMATION, "Info", IM_COL32(215, 215, 215, 255)},
    {ICON_MDI_ALERT, "Warning", IM_COL32(255, 190, 60, 255)},
    {ICON_MDI_ALERT_OCTAGON, "Error", IM_COL32(255, 95, 95, 255)},
    {ICON_MDI_ALERT_OCTAGON, "Critical", IM_COL32(255, 60, 60, 255)},
    {ICON_MDI_INFORMATION, "", IM_COL32(215, 215, 215, 255)},
}};

// The levels with a toggle on the toolbar, mildest first so the errors end up at the edge.
constexpr std::array<level::level_enum, 5> LOG_FILTERED_LEVELS{level::trace,
                                                               level::debug,
                                                               level::info,
                                                               level::warn,
                                                               level::err};
// Trace and debug are the chatty levels: their toggles stay small and keep the count for the tooltip.
constexpr level::level_enum LOG_FIRST_COUNTED_LEVEL = level::info;

constexpr float CONSOLE_SEARCH_FIELD_WIDTH = 220.0f;
constexpr float CONSOLE_SEARCH_FIELD_MIN_WIDTH = 70.0f;
// Share of the panel the list starts with; the splitter under it can be dragged.
constexpr float CONSOLE_LIST_HEIGHT_SHARE = 0.75f;
constexpr float CONSOLE_LIST_MIN_HEIGHT = 60.0f;
constexpr float CONSOLE_DETAILS_MIN_HEIGHT = 60.0f;
constexpr int CONSOLE_ERROR_TOAST_MILLISECONDS = 2000;

// A row, in units of the font size.
constexpr float LOG_ROW_PADDING_X = 0.45f;
constexpr float LOG_ROW_ICON_COLUMN = 1.75f;
// Narrower than this a row drops the source location and leaves the room to the message.
constexpr float LOG_ROW_MIN_WIDTH_WITH_SOURCE = 40.0f;
constexpr ImU32 LOG_ROW_STRIPE_COLOR = IM_COL32(255, 255, 255, 7);
constexpr float LOG_ROW_HOVERED_ALPHA = 0.45f;
// The selection wears the accent of the toolbars; the theme's header color is too close to a stripe.
constexpr float LOG_ROW_SELECTED_ALPHA = 0.55f;
constexpr float LOG_SOURCE_TEXT_ALPHA = 0.45f;

auto get_first_log_line(hpp::string_view text) -> hpp::string_view
{
    return text.substr(0, text.find('\n'));
}

auto get_log_message_color(level::level_enum log_level) -> ImU32
{
    // Only a problem is colored; the bulk of the log stays in the quiet text color.
    const bool is_problem = log_level >= level::warn && log_level <= level::critical;
    return is_problem ? LOG_LEVEL_STYLES[size_t(log_level)].color : ImGui::GetColorU32(ImGuiCol_Text);
}

auto make_log_source_label(const console_log_panel::log_source& source) -> std::string
{
    if(source.filename.empty())
    {
        return {};
    }
    return fmt::format("{}:{}", fs::path(source.filename).filename().string(), source.line);
}

/// One log on one line: level icon, the first line of the message, and the source location at
/// the right end when there is room for it.
void draw_log_line(const console_log_panel::log_entry& entry, const ImRect& rect)
{
    const log_level_style& style = LOG_LEVEL_STYLES[size_t(entry.level)];
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float font_size = ImGui::GetFontSize();
    const float padding_x = font_size * LOG_ROW_PADDING_X;
    const float text_y = ImFloor(rect.Min.y + (rect.GetHeight() - font_size) * 0.5f);
    draw_list->AddText(ImVec2(rect.Min.x + padding_x, text_y), style.color, style.icon);
    const bool has_room_for_source = rect.GetWidth() > font_size * LOG_ROW_MIN_WIDTH_WITH_SOURCE;
    const std::string source_label = has_room_for_source ? make_log_source_label(entry.source) : std::string{};
    const float source_width = source_label.empty() ? 0.0f : ImGui::CalcTextSize(source_label.c_str()).x + padding_x;
    const ImVec2 message_min(rect.Min.x + font_size * LOG_ROW_ICON_COLUMN, text_y);
    const ImVec2 message_max(rect.Max.x - padding_x - source_width, rect.Max.y);
    const hpp::string_view message = get_first_log_line({entry.formatted.data(), entry.formatted.size()});
    ImGui::PushStyleColor(ImGuiCol_Text, get_log_message_color(entry.level));
    ImGui::RenderTextEllipsis(draw_list,
                              message_min,
                              message_max,
                              message_max.x,
                              message.data(),
                              message.data() + message.size(),
                              nullptr);
    ImGui::PopStyleColor();
    if(!source_label.empty())
    {
        const ImVec2 source_pos(rect.Max.x - source_width, text_y);
        draw_list->AddText(source_pos, ImGui::GetColorU32(ImGuiCol_Text, LOG_SOURCE_TEXT_ALPHA), source_label.c_str());
    }
}

void draw_log_row_background(const ImRect& rect, int row_index, bool is_selected, bool is_hovered)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if(is_selected)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(ImGuiCol_TabSelected, LOG_ROW_SELECTED_ALPHA));
        return;
    }
    if(is_hovered)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(ImGuiCol_HeaderHovered, LOG_ROW_HOVERED_ALPHA));
        return;
    }
    const bool is_striped = row_index % 2 == 1;
    if(is_striped)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, LOG_ROW_STRIPE_COLOR);
    }
}

void draw_empty_log_list_hint()
{
    const char* hint = "No logs to show";
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 hint_size = ImGui::CalcTextSize(hint);
    ImGui::SetCursorPos(ImGui::GetCursorPos() + (avail - hint_size) * 0.5f);
    ImGui::TextDisabled("%s", hint);
}

void open_log_in_environment(const fs::path& entry, int line)
{
    if(ex::is_format<script>(entry.extension().string()))
    {
        editor_actions::open_workspace_on_file(entry, line);
    }
    else
    {
        // fs::show_in_graphical_env(entry);
    }
}
} // namespace

console_log_panel::console_log_panel(const char* name) : panel_base(name)
{
    set_pattern("[%H:%M:%S] %v");

    enabled_categories_.fill(true);
    enabled_categories_[level::trace] = false;
    enabled_categories_[level::debug] = false;
}

void console_log_panel::sink_it_(const details::log_msg& msg)
{
    {
        std::lock_guard<std::recursive_mutex> lock(entries_mutex_);

        auto log_msg = msg;
        log_msg.color_range_start = 0;
        log_msg.color_range_end = 0;
        log_msg.source = {};
        memory_buf_t formatted;
        formatter_->format(log_msg, formatted);
        log_entry entry;
        entry.formatted.resize(formatted.size());
        std::memcpy(entry.formatted.data(), formatted.data(), formatted.size() * sizeof(char));

        if(msg.source.filename)
        {
            entry.source.filename = msg.source.filename;
        }
        if(msg.source.funcname)
        {
            entry.source.funcname = msg.source.funcname;
        }
        if(msg.source.line)
        {
            entry.source.line = msg.source.line;
        }

        entry.level = msg.level;

        entry.id = current_id_++;

        if(new_entries_begin_idx_ == -1)
        {
            new_entries_begin_idx_ = int64_t(entries_.size());
        }
        entries_.emplace_back(std::move(entry));
    }
    has_new_entries_ = true;
}

void console_log_panel::flush_()
{
}

auto console_log_panel::snapshot_logs(level::level_enum min_level, size_t max_count, uint64_t after_id) const
    -> std::vector<log_snapshot_entry>
{
    std::lock_guard<std::recursive_mutex> lock(entries_mutex_);
    std::vector<log_snapshot_entry> matched;
    matched.reserve(std::min(max_count, entries_.size()));
    for(size_t i = 0; i < entries_.size(); ++i)
    {
        const auto& entry = entries_[i];
        if(entry.id <= after_id)
        {
            continue;
        }
        if(entry.level < min_level)
        {
            continue;
        }
        log_snapshot_entry snap;
        snap.id = entry.id;
        snap.level = entry.level;
        snap.text.assign(entry.formatted.begin(), entry.formatted.end());
        snap.filename = entry.source.filename;
        snap.funcname = entry.source.funcname;
        snap.line = entry.source.line;
        matched.push_back(std::move(snap));
    }
    if(matched.size() > max_count)
    {
        matched.erase(matched.begin(), matched.end() - static_cast<std::ptrdiff_t>(max_count));
    }
    return matched;
}

void console_log_panel::clear_log()
{
    {
        std::lock_guard<std::recursive_mutex> lock(entries_mutex_);
        entries_.clear();
        selected_log_ = {};
    }
    has_new_entries_ = false;
}

auto console_log_panel::has_new_entries() const -> bool
{
    return has_new_entries_;
}

void console_log_panel::set_has_new_entries(bool val)
{
    has_new_entries_ = val;
}

auto console_log_panel::is_entry_visible(const log_entry& entry) const -> bool
{
    if(!enabled_categories_[entry.level])
    {
        return false;
    }
    return filter_.PassFilter(entry.formatted.data(), entry.formatted.data() + entry.formatted.size());
}

auto console_log_panel::collect_visible_entries() -> display_entries_t
{
    display_entries_t visible_entries;
    level_counts_.fill(0);
    std::lock_guard<std::recursive_mutex> lock(entries_mutex_);
    for(size_t i = 0; i < entries_.size(); ++i)
    {
        const log_entry& entry = entries_[i];
        ++level_counts_[entry.level];
        if(is_entry_visible(entry))
        {
            visible_entries.emplace_back(entry);
        }
    }
    return visible_entries;
}

auto console_log_panel::find_last_visible_entry() const -> hpp::optional<log_entry>
{
    std::lock_guard<std::recursive_mutex> lock(entries_mutex_);
    for(auto it = std::rbegin(entries_); it != std::rend(entries_); ++it)
    {
        if(is_entry_visible(*it))
        {
            return *it;
        }
    }
    return {};
}

void console_log_panel::notify_new_errors()
{
    if(new_entries_begin_idx_ == -1)
    {
        return;
    }
    uint64_t errors_count = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(entries_mutex_);
        for(size_t i = new_entries_begin_idx_; i < entries_.size(); ++i)
        {
            if(entries_[i].level == level::err)
            {
                errors_count++;
            }
        }
        new_entries_begin_idx_ = -1;
    }
    if(errors_count > 0)
    {
        const std::string message = fmt::format("{} Error(s)...", errors_count);
        ImGui::PushNotification({ImGuiToastType_Error, CONSOLE_ERROR_TOAST_MILLISECONDS, message.c_str()});
    }
}

auto console_log_panel::get_window_flags() const -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar;
}

void console_log_panel::draw_ui(rtti::context& ctx)
{
    (void)ctx;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if(avail.x < 1.0f || avail.y < 1.0f)
    {
        return;
    }
    // Collected first: the toolbar shows the counts of this very frame.
    const display_entries_t entries = collect_visible_entries();
    draw_toolbar();
    const ImVec2 list_size(0.0f, ImMax(ImGui::GetContentRegionAvail().y * CONSOLE_LIST_HEIGHT_SHARE, CONSOLE_LIST_MIN_HEIGHT));
    if(ImGui::BeginChild("##log_list", list_size, ImGuiChildFlags_ResizeY))
    {
        draw_list_context_menu();
        draw_log_list(entries);
    }
    ImGui::EndChild();
    const ImVec2 details_size(0.0f, ImMax(ImGui::GetContentRegionAvail().y, CONSOLE_DETAILS_MIN_HEIGHT));
    if(ImGui::BeginChild("##log_details", details_size, ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        draw_details();
    }
    ImGui::EndChild();
}

void console_log_panel::draw_toolbar()
{
    if(panel_toolbar::begin_strip("##console_toolbar"))
    {
        draw_clear_controls();
        panel_toolbar::separator();
        draw_search_field();
        panel_toolbar::align_right();
        draw_level_filters();
    }
    panel_toolbar::end_strip();
}

void console_log_panel::draw_clear_controls()
{
    if(panel_toolbar::button("##clear", ICON_MDI_DELETE_SWEEP " Clear", "Clear the console"))
    {
        clear_log();
    }
    if(!panel_toolbar::begin_dropdown("##clear_options", nullptr, "When the console clears on its own"))
    {
        return;
    }
    ImGui::SeparatorText("Clear Automatically");
    ImGui::Checkbox("On Play", &clear_on_play_);
    ImGui::Checkbox("On Recompile", &clear_on_recompile_);
    panel_toolbar::end_dropdown();
}

void console_log_panel::draw_search_field()
{
    const float width =
        panel_toolbar::calc_flexible_width(CONSOLE_SEARCH_FIELD_MIN_WIDTH, CONSOLE_SEARCH_FIELD_WIDTH);
    panel_toolbar::begin_field(width);
    ImGui::DrawFilterWithHint(filter_, ICON_MDI_MAGNIFY " Search...", width);
    ImGui::DrawItemActivityOutline();
    panel_toolbar::end_field();
}

void console_log_panel::draw_level_filters()
{
    for(const level::level_enum log_level : LOG_FILTERED_LEVELS)
    {
        const log_level_style& style = LOG_LEVEL_STYLES[size_t(log_level)];
        const size_t count = level_counts_[log_level];
        const bool shows_count = log_level >= LOG_FIRST_COUNTED_LEVEL;
        const std::string id = fmt::format("##level_{}", style.name);
        const std::string text = shows_count ? fmt::format("{} {}", style.icon, count) : std::string(style.icon);
        const std::string tooltip = fmt::format("Show / hide {} logs ({})", style.name, count);
        bool& is_enabled = enabled_categories_[log_level];
        if(panel_toolbar::filter_toggle(id.c_str(), text.c_str(), is_enabled, style.color, tooltip.c_str()))
        {
            is_enabled = !is_enabled;
        }
    }
}

void console_log_panel::draw_list_context_menu()
{
    if(!ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight))
    {
        return;
    }
    {
        ImGui::ContextMenuStyleScope style_scope;
        if(ImGui::MenuItemIcon(ICON_MDI_DELETE_SWEEP, "Clear"))
        {
            clear_log();
        }
    }
    ImGui::EndPopup();
}

void console_log_panel::draw_log_list(const display_entries_t& entries)
{
    if(entries.empty())
    {
        draw_empty_log_list_hint();
        set_has_new_entries(false);
        return;
    }
    // Rows tile without a gap, so the stripes and the selection read as one surface.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGuiListClipper clipper;
    clipper.Begin(int(entries.size()), ImGui::GetFrameHeight());
    while(clipper.Step())
    {
        for(int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
        {
            draw_log_row(entries[i], i);
        }
    }
    ImGui::PopStyleVar();
    // Follow the log only while the view already sits at its end.
    const bool is_at_end = ImGui::GetScrollY() > (ImGui::GetScrollMaxY() - 0.01f);
    if(has_new_entries() && is_at_end)
    {
        ImGui::SetScrollHereY(1.0f);
    }
    set_has_new_entries(false);
}

void console_log_panel::draw_log_row(const log_entry& entry, int row_index)
{
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const ImVec2 row_size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    const ImRect row_rect(row_min, row_min + row_size);
    ImGui::PushID(row_index);
    ImGui::InvisibleButton("##log_row", row_size);
    ImGui::PopID();
    const bool is_selected = selected_log_ && selected_log_->id == entry.id;
    draw_log_row_background(row_rect, row_index, is_selected, ImGui::IsItemHovered());
    draw_log_line(entry, row_rect);
    if(ImGui::IsItemClicked())
    {
        select_log(entry);
    }
    if(ImGui::IsItemDoubleClicked())
    {
        open_log(entry);
    }
}

void console_log_panel::draw_last_log_button()
{
    const hpp::optional<log_entry> last_entry = find_last_visible_entry();
    if(last_entry)
    {
        const ImVec2 line_min = ImGui::GetCursorScreenPos();
        const ImVec2 line_size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
        if(ImGui::InvisibleButton("##last_log", line_size))
        {
            ImGui::FocusWindow(ImGui::FindWindowByName(get_name().c_str()));
        }
        draw_log_line(*last_entry, ImRect(line_min, line_min + line_size));
    }
    notify_new_errors();
}

void console_log_panel::draw_details()
{
    std::lock_guard<std::recursive_mutex> lock(entries_mutex_);
    if(!selected_log_)
    {
        ImGui::TextDisabled("%s", "Select a log to see it in full.");
        return;
    }
    const log_entry& entry = *selected_log_;
    const string_view_t message(entry.formatted.data(), entry.formatted.size());
    // The source is a link: a script opens at its line in the workspace.
    const bool has_source = !entry.source.filename.empty();
    const std::string source = has_source ? fmt::format("{0}() (at [{1}:{2}]({1}:{2}))",
                                                        entry.source.funcname,
                                                        entry.source.filename,
                                                        entry.source.line)
                                          : std::string{};
    const std::string description = fmt::format("{}{}", message, source);
    ImGui::MarkdownConfig config{};
    config.linkCallback = [&](const char* link, uint32_t link_length)
    {
        open_log_in_environment(fs::path(entry.source.filename), entry.source.line);
    };
    ImGui::Markdown(description.data(), int32_t(description.size()), config);
}

void console_log_panel::select_log(const log_entry& entry)
{
    selected_log_ = entry;
}

void console_log_panel::open_log(const log_entry& entry)
{
    open_log_in_environment(entry.source.filename, entry.source.line);
}

void console_log_panel::on_play()
{
    if(clear_on_play_)
    {
        clear_log();
    }
}

void console_log_panel::on_recompile()
{
    if(clear_on_recompile_)
    {
        clear_log();
    }
}

} // namespace unravel
