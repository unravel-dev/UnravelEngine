#include "audit_panel.h"
#include "../packages_panel/packages_widgets.h"
#include "../panel_toolbar.h"
#include "audit_panel_widgets.h"

#include <editor/audit/audit_manager.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <engine/engine.h>
#include <logging/logging.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <map>

namespace unravel
{
namespace audit_issues_detail
{

using namespace audit_widgets;

// Sizes in font units.
constexpr float AUDIT_ISSUES_SEARCH_WIDTH = 16.0f;
constexpr float AUDIT_ISSUES_SEARCH_MIN_WIDTH = 6.0f;
/// Wider than this, the details show what to do beside where the issue is.
constexpr float AUDIT_ISSUES_TWO_COLUMNS = 60.0f;
constexpr float AUDIT_ISSUES_MIN_HEIGHT = 6.0f;
constexpr float AUDIT_ISSUES_MIN_DETAILS_HEIGHT = 10.0f;
constexpr float AUDIT_ISSUES_ICON_COLUMN = 1.75f;
constexpr float AUDIT_ISSUES_GROUP_INDENT = 1.2f;
constexpr float AUDIT_ISSUES_SUBJECT_SHARE = 0.38f;
constexpr float AUDIT_ISSUES_ROW_WITH_SUBJECT = 30.0f;
constexpr float AUDIT_ISSUES_LABEL_COLUMN = 6.5f;
constexpr float AUDIT_ISSUES_TITLE_SCALE = 1.25f;
constexpr float AUDIT_ISSUES_SECTION_GAP = 0.6f;

/// One row of the grouped issue list: a rule's header or one of its issues.
struct issue_list_row
{
    const audit::issue* item{nullptr};
    std::string rule;
    std::size_t count{};
    audit::severity worst{audit::severity::info};
};

/// The rows of the list: rules whose worst issue matters most first, each issue under its rule.
auto make_rows(const std::vector<const audit::issue*>& visible, const std::set<std::string>& collapsed) -> std::vector<issue_list_row>
{
    std::map<std::string, std::vector<const audit::issue*>> by_rule;
    for(const auto* item : visible)
    {
        by_rule[item->rule].push_back(item);
    }
    std::vector<const audit::rule*> ordered;
    for(const auto& owner : audit::get_rules())
    {
        if(by_rule.count(owner.id) > 0)
        {
            ordered.push_back(&owner);
        }
    }
    const auto worst_of = [&by_rule](const audit::rule* owner)
    {
        audit::severity worst = audit::severity::info;
        for(const auto* item : by_rule[owner->id])
        {
            worst = std::max(worst, item->level);
        }
        return worst;
    };
    std::stable_sort(ordered.begin(),
                     ordered.end(),
                     [&worst_of](const audit::rule* lhs, const audit::rule* rhs)
                     {
                         return worst_of(lhs) > worst_of(rhs);
                     });
    std::vector<issue_list_row> rows;
    for(const auto* owner : ordered)
    {
        const auto& items = by_rule[owner->id];
        issue_list_row header;
        header.rule = owner->id;
        header.count = items.size();
        header.worst = worst_of(owner);
        rows.push_back(header);
        if(collapsed.count(owner->id) > 0)
        {
            continue;
        }
        for(const auto* item : items)
        {
            rows.push_back({item, owner->id, 0, item->level});
        }
    }
    return rows;
}

/// A rule's header row: a chevron, its worst severity, its title and how many issues it has.
auto draw_group_row(const issue_list_row& row, bool is_collapsed, int row_index) -> bool
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    ImGui::PushID(row.rule.c_str());
    const bool is_clicked = ImGui::InvisibleButton("##audit_group", size);
    ImGui::PopID();
    const bool is_hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(min, min + size, ImGui::GetColorU32(ImGuiCol_Text, is_hovered ? 0.07f : 0.035f));
    const float text_y = ImFloor(min.y + (size.y - font_size) * 0.5f);
    const float padding = font_size * 0.45f;
    const float center_y = min.y + size.y * 0.5f;
    ImGui::RenderIconCentered(draw_list,
                              ImVec2(min.x + padding + font_size * 0.4f, center_y),
                              is_collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN,
                              imgui_style::get_muted_text_color_u32());
    const auto& style = get_severity_style(row.worst);
    ImGui::RenderIconCentered(draw_list, ImVec2(min.x + font_size * (AUDIT_ISSUES_ICON_COLUMN + 0.5f), center_y), style.icon, style.color);
    const auto* owner = audit::find_rule(row.rule);
    const std::string title = owner != nullptr ? owner->title : row.rule;
    ImFont* bold = ImGui::GetFont(ImGui::Font::SemiBold);
    const float title_left = min.x + font_size * (AUDIT_ISSUES_ICON_COLUMN * 2.0f);
    const std::string count = std::to_string(row.count);
    const float count_width = calc_chip_width(count.c_str());
    const float title_right = min.x + size.x - count_width - padding * 2.0f;
    const ImVec4 clip(title_left, min.y, title_right, min.y + size.y);
    draw_list->AddText(bold, font_size, ImVec2(title_left, text_y), ImGui::GetColorU32(ImGuiCol_Text), title.c_str(), nullptr, 0.0f, &clip);
    const float title_width = ImMin(bold->CalcTextSizeA(font_size, FLT_MAX, 0.0f, title.c_str()).x, title_right - title_left);
    // The count sits after the title, as a muted chip.
    draw_chip_at(ImVec2(title_left + title_width + padding, min.y), count.c_str(), imgui_style::get_muted_text_color_u32());
    (void)row_index;
    if(is_hovered)
    {
        ImGui::SetItemTooltipEx("%s", is_collapsed ? "Show this rule's issues." : "Hide this rule's issues.");
    }
    return is_clicked;
}

/// What an issue's row shows at its right end: the subject in a few words, or the profiles when the message already
/// names the subject (a setting's message starts with its place in Project Settings).
auto get_row_subject(const audit::issue& item) -> std::string
{
    auto subject = describe_subject_short(item.where);
    const bool is_named = item.where.kind == audit::subject_kind::setting || (!subject.empty() && item.message.rfind(subject, 0) == 0);
    if(!is_named)
    {
        return subject;
    }
    std::string profiles;
    for(const auto& profile : item.profiles)
    {
        profiles += profiles.empty() ? profile : ", " + profile;
    }
    return profiles;
}

/// An issue's row: its state icon, its message and, when there is room, its subject at the right.
void draw_issue_line(const audit::issue& item, const ImRect& rect, float indent, bool is_fixed)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float font_size = ImGui::GetFontSize();
    const float text_y = ImFloor(rect.Min.y + (rect.GetHeight() - font_size) * 0.5f);
    const float padding = font_size * 0.45f;
    const auto& style = get_severity_style(item.level);
    const char* icon = item.is_ignored ? ICON_MDI_EYE_OFF_OUTLINE : is_fixed ? ICON_MDI_CHECK_CIRCLE_OUTLINE : style.icon;
    const ImU32 icon_color = item.is_ignored ? IGNORED_COLOR : is_fixed ? FIXED_COLOR : style.color;
    const float left = rect.Min.x + indent;
    ImGui::RenderIconCentered(draw_list, ImVec2(left + padding + font_size * 0.5f, rect.GetCenter().y), icon, icon_color);
    const bool has_room_for_subject = rect.GetWidth() > font_size * AUDIT_ISSUES_ROW_WITH_SUBJECT;
    const std::string subject = has_room_for_subject ? get_row_subject(item) : std::string{};
    const float subject_width = subject.empty() ? 0.0f
                                                : std::min(ImGui::CalcTextSize(subject.c_str()).x + padding,
                                                           rect.GetWidth() * AUDIT_ISSUES_SUBJECT_SHARE);
    const ImVec2 message_min(left + font_size * AUDIT_ISSUES_ICON_COLUMN, text_y);
    const ImVec2 message_max(rect.Max.x - padding - subject_width - padding, rect.Max.y);
    const ImU32 text_color = item.is_ignored ? IGNORED_COLOR : ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::RenderTextEllipsis(draw_list, message_min, message_max, message_max.x, item.message.c_str(), nullptr, nullptr);
    ImGui::PopStyleColor();
    if(!subject.empty())
    {
        const ImVec2 subject_min(rect.Max.x - subject_width, text_y);
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
        ImGui::RenderTextEllipsis(draw_list,
                                  subject_min,
                                  ImVec2(rect.Max.x - padding, rect.Max.y),
                                  rect.Max.x - padding,
                                  subject.c_str(),
                                  nullptr,
                                  nullptr);
        ImGui::PopStyleColor();
    }
}

/// The issue ids in the order the list shows them, for the arrow keys.
auto list_issue_ids(const std::vector<issue_list_row>& rows) -> std::vector<std::string>
{
    std::vector<std::string> ids;
    for(const auto& row : rows)
    {
        if(row.item != nullptr)
        {
            ids.push_back(row.item->get_id());
        }
    }
    return ids;
}

/// The subject's kind as the Where field's label says it.
auto get_where_label(audit::subject_kind kind) -> const char*
{
    switch(kind)
    {
        case audit::subject_kind::asset:
            return "Asset";
        case audit::subject_kind::entity:
            return "Entity";
        case audit::subject_kind::script:
            return "Script";
        case audit::subject_kind::setting:
            return "Setting";
        case audit::subject_kind::profile:
            return "Profile";
        case audit::subject_kind::project:
        default:
            return "Where";
    }
}

/// The call path: each method, an arrow between them, the last one bright. Wraps as a line of text does.
void draw_call_path(const std::vector<std::string>& path)
{
    for(std::size_t index = 0; index < path.size(); ++index)
    {
        const bool is_last = index + 1 == path.size();
        const std::string& step = path[index];
        const float width = ImGui::CalcTextSize(step.c_str()).x;
        if(index > 0)
        {
            const char* arrow = ICON_MDI_CHEVRON_RIGHT;
            same_line_or_wrap(ImGui::CalcTextSize(arrow).x + width);
            ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
            ImGui::TextUnformatted(arrow);
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, is_last ? ImGui::GetColorU32(ImGuiCol_Text) : imgui_style::get_muted_text_color_u32());
        ImGui::TextUnformatted(step.c_str());
        ImGui::PopStyleColor();
    }
}

} // namespace audit_issues_detail

auto audit_panel::is_visible(const audit::issue& item) const -> bool
{
    if(item.is_ignored && !shows_ignored_)
    {
        return false;
    }
    if(!shows_severity_[static_cast<std::size_t>(item.level)])
    {
        return false;
    }
    if(!rule_filter_.empty() && item.rule != rule_filter_)
    {
        return false;
    }
    if(area_filter_ != 0 && (item.areas & area_filter_) == 0)
    {
        return false;
    }
    if(!profile_filter_.empty() && !item.profiles.empty() &&
       std::find(item.profiles.begin(), item.profiles.end(), profile_filter_) == item.profiles.end())
    {
        return false;
    }
    if(!filter_.IsActive())
    {
        return true;
    }
    const auto* owner = audit::find_rule(item.rule);
    const std::string haystack = item.message + " " + audit_widgets::describe_subject(item.where) + " " + item.rule + " " +
                                 (owner != nullptr ? owner->title : std::string{});
    return filter_.PassFilter(haystack.c_str());
}

auto audit_panel::collect_visible(const audit::report& report) const -> std::vector<const audit::issue*>
{
    std::vector<const audit::issue*> visible;
    for(const auto& item : report.issues)
    {
        if(is_visible(item))
        {
            visible.push_back(&item);
        }
    }
    return visible;
}

void audit_panel::draw_issues(rtti::context& ctx, const audit::report& report)
{
    using namespace audit_issues_detail;
    draw_issue_filters(report);
    const auto visible = collect_visible(report);
    // The details always show an issue while the list has one: the first, until another is picked.
    const bool is_selection_shown = std::any_of(visible.begin(),
                                                visible.end(),
                                                [this](const audit::issue* item)
                                                {
                                                    return item->get_id() == selected_issue_;
                                                });
    if(!is_selection_shown && !visible.empty())
    {
        const auto rows = make_rows(visible, collapsed_rules_);
        const auto ids = list_issue_ids(rows);
        if(!ids.empty())
        {
            selected_issue_ = ids.front();
        }
    }
    // The list across the window, so messages read in full, over the details; the split keeps both in sight.
    pane_split split("##audit_issue_split",
                     false,
                     issues_split_,
                     to_pixels(AUDIT_ISSUES_MIN_HEIGHT),
                     to_pixels(AUDIT_ISSUES_MIN_DETAILS_HEIGHT));
    if(ImGui::BeginChild("##audit_issue_list_pane", split.get_first_size()))
    {
        draw_issue_list(report, visible);
    }
    ImGui::EndChild();
    split.draw_handle();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 40));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(to_pixels(1.0f), to_pixels(0.8f)));
    const bool is_open = ImGui::BeginChild("##audit_issue_details_pane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if(is_open)
    {
        draw_issue_details(ctx, report);
    }
    ImGui::EndChild();
}

void audit_panel::draw_issue_filters(const audit::report& report)
{
    using namespace audit_issues_detail;
    if(panel_toolbar::begin_strip("##audit_issue_filters"))
    {
        panel_toolbar::search_field(filter_,
                                    ICON_MDI_MAGNIFY " Search...",
                                    "Search the issues' messages, subjects and rules.",
                                    to_pixels(AUDIT_ISSUES_SEARCH_MIN_WIDTH),
                                    to_pixels(AUDIT_ISSUES_SEARCH_WIDTH));
        const std::string profile_text = profile_filter_.empty() ? std::string(ICON_MDI_LAYERS_OUTLINE " All Profiles")
                                                                 : fmt::format(ICON_MDI_LAYERS_OUTLINE " {}", profile_filter_);
        if(panel_toolbar::begin_dropdown("##audit_profile_filter", profile_text.c_str(), "Show the issues of one profile's builds."))
        {
            if(ImGui::MenuItem("All Profiles", nullptr, profile_filter_.empty()))
            {
                profile_filter_.clear();
            }
            ImGui::Separator();
            for(const auto& profile : report.profiles)
            {
                if(ImGui::MenuItem(profile.c_str(), nullptr, profile_filter_ == profile))
                {
                    profile_filter_ = profile;
                }
            }
            panel_toolbar::end_dropdown();
        }
        const std::string area_text = area_filter_ == 0
                                          ? std::string(ICON_MDI_CHART_DONUT " All Areas")
                                          : fmt::format("{} {}",
                                                        get_area_icon(static_cast<audit::area>(area_filter_)),
                                                        get_area_title(static_cast<audit::area>(area_filter_)));
        if(panel_toolbar::begin_dropdown("##audit_area_filter", area_text.c_str(), "Show the issues that cost one thing."))
        {
            if(ImGui::MenuItem("All Areas", nullptr, area_filter_ == 0))
            {
                area_filter_ = 0;
            }
            ImGui::Separator();
            for(const auto value : audit::ALL_AREAS)
            {
                const std::string label = fmt::format("{} {}", get_area_icon(value), get_area_title(value));
                if(ImGui::MenuItem(label.c_str(), nullptr, area_filter_ == static_cast<std::uint16_t>(value)))
                {
                    area_filter_ = static_cast<std::uint16_t>(value);
                }
            }
            panel_toolbar::end_dropdown();
        }
        if(!rule_filter_.empty())
        {
            const auto* owner = audit::find_rule(rule_filter_);
            const std::string text = fmt::format("{} {}", ICON_MDI_CLOSE, owner != nullptr ? owner->title : rule_filter_);
            if(panel_toolbar::button("##audit_rule_filter", text.c_str(), "Showing one rule's issues. Click to show every rule's."))
            {
                rule_filter_.clear();
            }
        }
        panel_toolbar::align_right();
        for(const auto level : SEVERITY_ORDER)
        {
            const auto& style = get_severity_style(level);
            const std::string id = fmt::format("##audit_severity_{}", style.name);
            const std::string text = fmt::format("{} {}", style.icon, report.count(level));
            const std::string tooltip = fmt::format("Show or hide the {} issues ({} not ignored).", style.name, report.count(level));
            bool& is_shown = shows_severity_[static_cast<std::size_t>(level)];
            if(panel_toolbar::filter_toggle(id.c_str(), text.c_str(), is_shown, style.color, tooltip.c_str()))
            {
                is_shown = !is_shown;
            }
        }
        std::size_t ignored = 0;
        for(const auto& item : report.issues)
        {
            ignored += item.is_ignored ? 1 : 0;
        }
        const std::string ignored_text = fmt::format(ICON_MDI_EYE_OFF_OUTLINE " {}", ignored);
        if(panel_toolbar::filter_toggle("##audit_show_ignored",
                                        ignored_text.c_str(),
                                        shows_ignored_,
                                        IGNORED_COLOR,
                                        "Show or hide the issues the project's audit settings ignore."))
        {
            shows_ignored_ = !shows_ignored_;
        }
    }
    panel_toolbar::end_strip();
}

void audit_panel::draw_issue_list(const audit::report& report, const std::vector<const audit::issue*>& visible)
{
    using namespace audit_issues_detail;
    if(visible.empty())
    {
        packages_widgets::draw_centered_hint("No issues to show",
                                             report.issues.empty() ? "The audit found nothing to report."
                                                                   : "Change the filters to see the other issues.");
        return;
    }
    const auto rows = make_rows(visible, collapsed_rules_);

    // The arrow keys move the selection while the list has the focus.
    if(ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput)
    {
        const int step = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 0;
        if(step != 0)
        {
            const auto ids = list_issue_ids(rows);
            const auto found = std::find(ids.begin(), ids.end(), selected_issue_);
            if(!ids.empty())
            {
                const auto index = found == ids.end() ? 0 : std::clamp<std::ptrdiff_t>((found - ids.begin()) + step, 0, std::ptrdiff_t(ids.size()) - 1);
                selected_issue_ = ids[static_cast<std::size_t>(index)];
                scroll_to_selected_ = true;
                ignore_reason_[0] = '\0';
            }
        }
    }

    const float row_height = ImGui::GetFrameHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    if(scroll_to_selected_)
    {
        scroll_to_selected_ = false;
        for(std::size_t index = 0; index < rows.size(); ++index)
        {
            if(rows[index].item != nullptr && rows[index].item->get_id() == selected_issue_)
            {
                const float top = float(index) * row_height;
                const float view_top = ImGui::GetScrollY();
                const float view_bottom = view_top + ImGui::GetWindowHeight() - row_height;
                if(top < view_top || top > view_bottom)
                {
                    ImGui::SetScrollY(top - ImGui::GetWindowHeight() * 0.4f);
                }
                break;
            }
        }
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()), row_height);
    while(clipper.Step())
    {
        for(int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
        {
            const auto& row = rows[static_cast<std::size_t>(index)];
            if(row.item != nullptr)
            {
                draw_issue_row(*row.item, index);
                continue;
            }
            const bool is_collapsed = collapsed_rules_.count(row.rule) > 0;
            if(draw_group_row(row, is_collapsed, index))
            {
                if(is_collapsed)
                {
                    collapsed_rules_.erase(row.rule);
                }
                else
                {
                    collapsed_rules_.insert(row.rule);
                }
            }
        }
    }
    ImGui::PopStyleVar();
}

void audit_panel::draw_issue_row(const audit::issue& item, int row_index)
{
    using namespace audit_issues_detail;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    const ImRect rect(min, min + size);
    const auto id = item.get_id();
    ImGui::PushID(id.c_str());
    ImGui::InvisibleButton("##audit_issue", size);
    ImGui::PopID();
    const bool is_selected = id == selected_issue_;
    packages_widgets::draw_row_background(rect, row_index, ImGui::IsItemHovered(), is_selected);
    auto& manager = engine::context().get_cached<audit_manager>();
    draw_issue_line(item, rect, ImGui::GetFontSize() * AUDIT_ISSUES_GROUP_INDENT, manager.is_fixed(id));
    if(ImGui::IsItemClicked())
    {
        selected_issue_ = id;
        ignore_reason_[0] = '\0';
    }
    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        open_subject(engine::context(), item);
    }
    if(ImGui::IsItemHovered())
    {
        ImGui::SetItemTooltipEx("%s\n%s\nDouble-click to show it.", item.message.c_str(), describe_subject(item.where).c_str());
    }
}

void audit_panel::draw_issue_details(rtti::context& ctx, const audit::report& report)
{
    using namespace audit_issues_detail;
    const auto* item = report.find(selected_issue_);
    if(item == nullptr)
    {
        packages_widgets::draw_centered_hint("No issue selected", "Select an issue to see what it found and what to do about it.");
        return;
    }
    draw_issue_header(ctx, *item);
    ImGui::Dummy(ImVec2(0.0f, to_pixels(AUDIT_ISSUES_SECTION_GAP)));
    const auto* owner = audit::find_rule(item->rule);
    // What to do and why on the left, where the issue is on the right; one column in a narrow window.
    const bool is_wide = ImGui::GetContentRegionAvail().x >= to_pixels(AUDIT_ISSUES_TWO_COLUMNS);
    if(is_wide && ImGui::BeginTable("##audit_issue_columns", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX))
    {
        ImGui::TableSetupColumn("##advice", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("##facts", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        draw_issue_advice(ctx, *item);
        if(owner != nullptr)
        {
            ImGui::Dummy(ImVec2(0.0f, to_pixels(AUDIT_ISSUES_SECTION_GAP)));
            draw_issue_why(*owner);
        }
        ImGui::TableNextColumn();
        ImGui::Indent(to_pixels(AUDIT_ISSUES_SECTION_GAP));
        draw_issue_facts(ctx, *item);
        ImGui::Unindent(to_pixels(AUDIT_ISSUES_SECTION_GAP));
        ImGui::EndTable();
    }
    else if(!is_wide)
    {
        draw_issue_advice(ctx, *item);
        ImGui::Dummy(ImVec2(0.0f, to_pixels(AUDIT_ISSUES_SECTION_GAP)));
        draw_issue_facts(ctx, *item);
        if(owner != nullptr)
        {
            ImGui::Dummy(ImVec2(0.0f, to_pixels(AUDIT_ISSUES_SECTION_GAP)));
            draw_issue_why(*owner);
        }
    }
    ImGui::Dummy(ImVec2(0.0f, to_pixels(AUDIT_ISSUES_SECTION_GAP)));
    ImGui::PushStyleColor(ImGuiCol_Text, with_alpha(imgui_style::get_muted_text_color_u32(), 0.75f));
    ImGui::TextUnformatted(fmt::format("Rule {}", item->rule).c_str());
    ImGui::PopStyleColor();
    ImGui::SetItemTooltipEx("Issue id: %s", item->get_id().c_str());
}

void audit_panel::draw_issue_why(const audit::rule& owner)
{
    using namespace audit_issues_detail;
    // A quiet toggle: the advice above is what stands out.
    const std::string label = fmt::format("{}  Why It Matters", shows_why_ ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT);
    ImFont* font = ImGui::GetFont(ImGui::Font::SemiBold);
    const float font_size = ImGui::GetFontSize();
    const ImVec2 size(font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label.c_str()).x, ImGui::GetFrameHeight());
    const ImVec2 min = ImGui::GetCursorScreenPos();
    if(ImGui::InvisibleButton("##audit_why", size))
    {
        shows_why_ = !shows_why_;
    }
    const bool is_hovered = ImGui::IsItemHovered();
    if(is_hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImGui::SetItemTooltipEx("%s", shows_why_ ? "Hide why the rule matters." : "Show why the rule matters.");
    ImGui::GetWindowDrawList()->AddText(font,
                                        font_size,
                                        ImVec2(min.x, ImFloor(min.y + (size.y - font_size) * 0.5f)),
                                        is_hovered ? ImGui::GetColorU32(ImGuiCol_Text) : imgui_style::get_muted_text_color_u32(),
                                        label.c_str());
    if(shows_why_)
    {
        packages_widgets::draw_muted_wrapped(owner.description.c_str());
    }
}

void audit_panel::draw_issue_header(rtti::context& ctx, const audit::issue& item)
{
    using namespace audit_issues_detail;
    const auto* owner = audit::find_rule(item.rule);
    const auto& style = get_severity_style(item.level);

    // The severity and the rule's source, with the issue's actions at the right end of the line.
    const std::string severity = fmt::format("{} {}", style.icon, style.name);
    draw_chip(severity.c_str(), style.color, "How much the issue matters.");
    if(owner != nullptr)
    {
        ImGui::SameLine();
        const std::string source = fmt::format("{} {}", get_rule_source_icon(owner->source), get_rule_source_title(owner->source));
        draw_chip(source.c_str(), imgui_style::get_muted_text_color_u32(), "What the rule reads.");
    }
    const float button = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImMax(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - button * 3.0f - spacing * 2.0f));
    if(draw_icon_button("##audit_show", ICON_MDI_OPEN_IN_NEW, "Show what the issue is about: the asset, the entity, the script line or the setting."))
    {
        open_subject(ctx, item);
    }
    ImGui::SameLine();
    auto& manager = ctx.get_cached<audit_manager>();
    if(item.is_ignored)
    {
        if(draw_icon_button("##audit_unignore", ICON_MDI_EYE_OUTLINE, "Stop ignoring the issue: remove it from the project's audit settings."))
        {
            std::string error;
            const bool is_done = manager.unignore(item.get_id(), error);
            set_message(error, !is_done);
        }
    }
    else
    {
        if(draw_icon_button("##audit_ignore_button",
                            ICON_MDI_EYE_OFF_OUTLINE,
                            "Ignore the issue on purpose. The reason goes into settings/audit.cfg, which the project shares "
                            "in version control."))
        {
            ImGui::OpenPopup("##audit_ignore");
        }
        draw_ignore_popup(ctx, item);
    }
    ImGui::SameLine();
    if(draw_icon_button("##audit_copy", ICON_MDI_CONTENT_COPY, "Copy the issue as text, with what to do, its rule and its id."))
    {
        ImGui::SetClipboardText(make_issue_text(item).c_str());
    }

    // The rule's title, then the finding.
    ImGui::Spacing();
    ImGui::PushFont(ImGui::GetFont(ImGui::Font::Bold), ImGui::GetCurrentContext()->FontSizeBase * AUDIT_ISSUES_TITLE_SCALE);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(owner != nullptr ? owner->title.c_str() : item.rule.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(item.message.c_str());
    ImGui::PopTextWrapPos();
    if(item.is_ignored)
    {
        ImGui::Spacing();
        const std::string text = fmt::format(ICON_MDI_EYE_OFF_OUTLINE " Ignored: {}", item.ignore_reason);
        ImGui::PushStyleColor(ImGuiCol_Text, IGNORED_COLOR);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

void audit_panel::draw_issue_advice(rtti::context& ctx, const audit::issue& item)
{
    using namespace audit_issues_detail;
    const auto* owner = audit::find_rule(item.rule);
    if(owner == nullptr)
    {
        return;
    }
    auto& manager = ctx.get_cached<audit_manager>();
    const auto id = item.get_id();
    const bool is_fixed = manager.is_fixed(id);
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color());
    begin_callout("##audit_advice", ICON_MDI_LIGHTBULB_ON_OUTLINE, "What to Do", owner->recommendation.c_str(), accent);
    if(is_fixed)
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, FIXED_COLOR);
        draw_callout_text(ICON_MDI_CHECK_CIRCLE " The fix was applied. Run the audit again to confirm it.");
        ImGui::PopStyleColor();
    }
    else if(!item.fix.empty())
    {
        ImGui::Spacing();
        const std::string label = fmt::format("{} {}", ICON_MDI_WRENCH, audit::get_fix_label(item.fix));
        if(draw_primary_button(label.c_str(),
                               "Change the project as the rule recommends. Import settings change as the inspector's "
                               "Apply changes them, and the asset compiles again.",
                               !manager.is_running()))
        {
            std::string error;
            const bool is_applied = manager.apply_fix(ctx, id, error);
            set_message(is_applied ? std::string("The fix was applied. Run the audit again to confirm it.") : error, !is_applied);
        }
    }
    draw_fix_all_button(ctx, item, !is_fixed && !item.fix.empty());
    end_callout();
}

void audit_panel::draw_fix_all_button(rtti::context& ctx, const audit::issue& item, bool is_beside_own_fix)
{
    using namespace audit_issues_detail;
    auto& manager = ctx.get_cached<audit_manager>();
    const auto fixable = manager.get_fixable_issues(item.rule).size();
    // The button is for the rule's other issues: one whose only fixable issue is this one has its own button.
    const bool is_own_fix_counted = is_beside_own_fix && !item.is_ignored;
    if(fixable <= (is_own_fix_counted ? 1u : 0u))
    {
        return;
    }
    if(is_beside_own_fix)
    {
        ImGui::SameLine();
    }
    else
    {
        ImGui::Spacing();
    }
    const std::string label = fmt::format("{} Fix All {}", ICON_MDI_WRENCH_OUTLINE, fixable);
    if(draw_primary_button(label.c_str(),
                           "Apply the fix of every issue of this rule that has one. Each issue gets its own fix, as its "
                           "own button applies it.",
                           !manager.is_running()))
    {
        std::string error;
        const auto applied = manager.apply_rule_fixes(ctx, item.rule, error);
        const bool is_complete = applied == fixable;
        set_message(is_complete ? fmt::format("{} fixes were applied. Run the audit again to confirm them.", applied)
                                : fmt::format("{} of {} fixes were applied. {}", applied, fixable, error),
                    !is_complete);
    }
}

void audit_panel::draw_issue_facts(rtti::context& ctx, const audit::issue& item)
{
    using namespace audit_issues_detail;
    const float label_column = to_pixels(AUDIT_ISSUES_LABEL_COLUMN);
    const auto& where = item.where;

    draw_field_label(get_where_label(where.kind), label_column);
    switch(where.kind)
    {
        case audit::subject_kind::asset:
        case audit::subject_kind::script:
            if(draw_path_item("##audit_where", where.key, true, "Click to show it."))
            {
                open_subject(ctx, item);
            }
            break;
        case audit::subject_kind::entity:
        {
            const std::string name = where.label.empty() ? where.detail : where.label;
            ImGui::TextUnformatted(name.c_str());
            draw_field_label("Document", label_column);
            if(draw_path_item("##audit_document", where.key, true, "Click to show the entity, or the document when it is not open."))
            {
                open_subject(ctx, item);
            }
            break;
        }
        case audit::subject_kind::setting:
        {
            const std::string label = get_setting_label(where.key);
            if(draw_chip_button("##audit_setting", label.c_str(), ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()), "Open the setting in Project Settings."))
            {
                open_subject(ctx, item);
            }
            break;
        }
        case audit::subject_kind::profile:
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(where.detail.empty() ? where.key.c_str() : fmt::format("{}: {}", where.key, where.detail).c_str());
            break;
        case audit::subject_kind::project:
        default:
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(describe_subject(where).c_str());
            break;
    }

    if(where.kind == audit::subject_kind::script && !where.detail.empty())
    {
        draw_field_label("Method", label_column);
        ImGui::TextUnformatted(where.detail.c_str());
    }

    std::vector<std::uint32_t> lines = item.lines;
    if(lines.empty() && where.line > 0)
    {
        lines.push_back(where.line);
    }
    if(!lines.empty())
    {
        draw_field_label(lines.size() == 1 ? "Line" : "Lines", label_column);
        for(std::size_t index = 0; index < lines.size(); ++index)
        {
            const std::string text = std::to_string(lines[index]);
            if(index > 0)
            {
                same_line_or_wrap(calc_chip_width(text.c_str()));
            }
            ImGui::PushID(static_cast<int>(index));
            if(draw_chip_button("##audit_line", text.c_str(), ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()), "Open the script at this line."))
            {
                open_script_line(where.key, lines[index]);
            }
            ImGui::PopID();
        }
    }

    if(item.call_path.size() > 1)
    {
        draw_field_label("Reached", label_column);
        ImGui::BeginGroup();
        draw_call_path(item.call_path);
        ImGui::EndGroup();
        ImGui::SetItemTooltipEx("%s", "How the engine's per-frame calls reach the method: from the update method or coroutine, through the project's own calls.");
    }

    const bool is_only_its_profile = where.kind == audit::subject_kind::profile && item.profiles.size() == 1 &&
                                     item.profiles.front() == where.key;
    if(!item.profiles.empty() && !is_only_its_profile)
    {
        draw_field_label("Profiles", label_column);
        ImGui::BeginGroup();
        for(std::size_t index = 0; index < item.profiles.size(); ++index)
        {
            const auto& profile = item.profiles[index];
            if(index > 0)
            {
                same_line_or_wrap(calc_chip_width(profile.c_str()));
            }
            draw_chip(profile.c_str(), PROFILE_COLOR, "A profile whose builds have the issue.");
        }
        ImGui::EndGroup();
    }

    draw_field_label("Costs", label_column);
    ImGui::BeginGroup();
    bool is_first = true;
    for(const auto value : audit::ALL_AREAS)
    {
        if(!audit::has_area(item.areas, value))
        {
            continue;
        }
        const std::string text = fmt::format("{} {}", get_area_icon(value), get_area_title(value));
        if(!is_first)
        {
            same_line_or_wrap(calc_chip_width(text.c_str()));
        }
        is_first = false;
        draw_chip(text.c_str(), AREA_COLOR, "What the issue costs.");
    }
    ImGui::EndGroup();

    if(!item.details.empty())
    {
        draw_field_label("Details", label_column);
        ImGui::BeginGroup();
        for(const auto& line : item.details)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndGroup();
    }
}

void audit_panel::draw_ignore_popup(rtti::context& ctx, const audit::issue& item)
{
    if(!ImGui::BeginPopup("##audit_ignore"))
    {
        return;
    }
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::TextUnformatted("Why is this fine?");
    ImGui::PopFont();
    packages_widgets::draw_muted_wrapped("The reason goes into settings/audit.cfg beside the issue, for whoever reads it next.");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24.0f);
    if(ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    const bool is_entered = ImGui::InputTextWithHint("##audit_ignore_reason",
                                                     "Pixel art: compression blurs it.",
                                                     ignore_reason_,
                                                     sizeof(ignore_reason_),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
    const std::string reason = ignore_reason_;
    ImGui::BeginDisabled(reason.empty());
    if(ImGui::Button("Ignore") || (is_entered && !reason.empty()))
    {
        std::string error;
        const bool is_done = ctx.get_cached<audit_manager>().ignore(item.get_id(), reason, error);
        set_message(error, !is_done);
        ignore_reason_[0] = '\0';
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace unravel
