#include "audit_panel.h"
#include "../packages_panel/packages_widgets.h"
#include "audit_panel_widgets.h"

#include <editor/audit/audit_manager.h>
#include <engine/engine.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <logging/logging.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <array>
#include <map>

namespace unravel
{
namespace audit_summary_detail
{

using namespace audit_widgets;

// Sizes in font units.
constexpr float AUDIT_SUMMARY_WIDE = 58.0f;
constexpr float AUDIT_SUMMARY_CARDS_IN_ONE_ROW = 40.0f;
constexpr float AUDIT_SUMMARY_GAP = 0.75f;
constexpr float AUDIT_SUMMARY_HEADER_ICON = 2.6f;
constexpr float AUDIT_SUMMARY_PROFILE_ICON = 1.8f;
constexpr std::size_t AUDIT_SUMMARY_TOP_ISSUES = 6;
constexpr std::size_t AUDIT_SUMMARY_TOP_RULES = 10;

/// Counts by severity, indexed by audit::severity.
using severity_counts = std::array<std::size_t, 3>;

auto get_total(const severity_counts& counts) -> std::size_t
{
    return counts[0] + counts[1] + counts[2];
}

auto make_segments(const severity_counts& counts) -> std::vector<bar_segment>
{
    std::vector<bar_segment> segments;
    for(const auto level : SEVERITY_ORDER)
    {
        segments.push_back({float(counts[static_cast<std::size_t>(level)]), get_severity_style(level).color});
    }
    return segments;
}

auto get_worst(const severity_counts& counts) -> audit::severity
{
    for(const auto level : SEVERITY_ORDER)
    {
        if(counts[static_cast<std::size_t>(level)] > 0)
        {
            return level;
        }
    }
    return audit::severity::info;
}

/// The open issues counted by rule, area and profile.
struct summary_counts
{
    std::map<std::string, severity_counts> by_rule;
    std::map<audit::area, severity_counts> by_area;
    std::map<std::string, severity_counts> by_profile;
    std::size_t ignored{};
    std::size_t open{};
};

auto count_issues(const audit::report& report) -> summary_counts
{
    summary_counts counts;
    for(const auto& item : report.issues)
    {
        if(item.is_ignored)
        {
            ++counts.ignored;
            continue;
        }
        ++counts.open;
        const auto level = static_cast<std::size_t>(item.level);
        counts.by_rule[item.rule][level]++;
        for(const auto value : audit::ALL_AREAS)
        {
            if(audit::has_area(item.areas, value))
            {
                counts.by_area[value][level]++;
            }
        }
        // An issue of no profile is in every profile's builds, as the profile filter and the Deploy window count it.
        for(const auto& profile : item.profiles.empty() ? report.profiles : item.profiles)
        {
            counts.by_profile[profile][level]++;
        }
    }
    return counts;
}

/// The counts of the report, worked out again only when it or its ignores change: the summary's parts all read them.
auto get_counts(const audit::report& report) -> const summary_counts&
{
    static const audit::report* counted = nullptr;
    static std::uint64_t counted_revision = 0;
    static summary_counts counts;
    const auto revision = engine::context().get_cached<audit_manager>().get_revision();
    if(counted != &report || counted_revision != revision)
    {
        counted = &report;
        counted_revision = revision;
        counts = count_issues(report);
    }
    return counts;
}

/// Each profile's platform title, from the report's Profiles table; empty when the report has none.
auto find_platform(const audit::report& report, const std::string& profile) -> std::string
{
    for(const auto& table : report.insights)
    {
        if(table.id != "profiles")
        {
            continue;
        }
        for(const auto& row : table.rows)
        {
            if(row.size() > 1 && row[0].text == profile)
            {
                return row[1].text;
            }
        }
    }
    return {};
}

/// How many rules have issues of a severity.
auto count_rules_with(const summary_counts& counts, audit::severity level) -> std::size_t
{
    return static_cast<std::size_t>(std::count_if(counts.by_rule.begin(),
                                                  counts.by_rule.end(),
                                                  [level](const auto& entry)
                                                  {
                                                      return entry.second[static_cast<std::size_t>(level)] > 0;
                                                  }));
}

/// A row of the Top Issues list: the severity, the rule's title and the subject. @return True when clicked.
auto draw_top_issue_row(const audit::issue& item, int row_index) -> bool
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    const auto id = item.get_id();
    ImGui::PushID(id.c_str());
    const bool is_clicked = ImGui::InvisibleButton("##audit_top_issue", size);
    ImGui::PopID();
    const bool is_hovered = ImGui::IsItemHovered();
    packages_widgets::draw_row_background(ImRect(min, min + size), row_index, is_hovered, false);
    if(is_hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetItemTooltipEx("%s\n%s", item.message.c_str(), describe_subject(item.where).c_str());
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const auto& style = get_severity_style(item.level);
    const float padding = font_size * 0.45f;
    ImGui::RenderIconCentered(draw_list, ImVec2(min.x + padding + font_size * 0.5f, min.y + size.y * 0.5f), style.icon, style.color);
    const float text_y = ImFloor(min.y + (size.y - font_size) * 0.5f);
    const auto* owner = audit::find_rule(item.rule);
    const std::string title = owner != nullptr ? owner->title : item.rule;
    const std::string subject = describe_subject_short(item.where);
    const float left = min.x + padding + font_size * 1.6f;
    const float right = min.x + size.x - padding;
    ImFont* bold = ImGui::GetFont(ImGui::Font::SemiBold);
    const float title_width = ImMin(bold->CalcTextSizeA(font_size, FLT_MAX, 0.0f, title.c_str()).x, (right - left) * 0.6f);
    const ImVec4 title_clip(left, min.y, left + title_width, min.y + size.y);
    draw_list->AddText(bold, font_size, ImVec2(left, text_y), ImGui::GetColorU32(ImGuiCol_Text), title.c_str(), nullptr, 0.0f, &title_clip);
    const float subject_left = left + title_width + font_size * 0.8f;
    if(subject_left < right)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
        ImGui::RenderTextEllipsis(draw_list,
                                  ImVec2(subject_left, text_y),
                                  ImVec2(right, min.y + size.y),
                                  right,
                                  subject.c_str(),
                                  nullptr,
                                  nullptr);
        ImGui::PopStyleColor();
    }
    return is_clicked;
}

} // namespace audit_summary_detail

void audit_panel::draw_summary(rtti::context& ctx, const audit::report& report)
{
    using namespace audit_summary_detail;
    if(!ImGui::BeginChild("##audit_summary", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
    {
        ImGui::EndChild();
        return;
    }
    draw_summary_header(ctx, report);
    ImGui::Dummy(ImVec2(0.0f, to_pixels(AUDIT_SUMMARY_GAP) * 0.5f));
    draw_summary_cards(report);
    ImGui::Dummy(ImVec2(0.0f, to_pixels(AUDIT_SUMMARY_GAP) * 0.5f));
    const bool is_wide = ImGui::GetContentRegionAvail().x >= to_pixels(AUDIT_SUMMARY_WIDE);
    if(is_wide && ImGui::BeginTable("##audit_summary_columns", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX))
    {
        ImGui::TableSetupColumn("##left", ImGuiTableColumnFlags_WidthStretch, 1.35f);
        ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        draw_top_issues(report);
        draw_rule_chart(report);
        ImGui::TableNextColumn();
        draw_profile_list(report);
        draw_area_chart(report);
        draw_notes(report);
        ImGui::EndTable();
    }
    else if(!is_wide)
    {
        draw_top_issues(report);
        draw_notes(report);
        draw_rule_chart(report);
        draw_profile_list(report);
        draw_area_chart(report);
    }
    ImGui::EndChild();
}

void audit_panel::draw_summary_header(rtti::context& ctx, const audit::report& report)
{
    using namespace audit_summary_detail;
    if(packages_widgets::begin_card("##audit_summary_header"))
    {
        const float font_size = ImGui::GetFontSize();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float icon_column = to_pixels(AUDIT_SUMMARY_HEADER_ICON);
        const auto errors = report.count(audit::severity::error);
        const auto warnings = report.count(audit::severity::warning);
        const char* icon = errors > 0 ? ICON_MDI_SHIELD_ALERT_OUTLINE : ICON_MDI_SHIELD_CHECK_OUTLINE;
        const ImU32 icon_color = errors > 0    ? get_severity_style(audit::severity::error).color
                                 : warnings > 0 ? get_severity_style(audit::severity::warning).color
                                                : FIXED_COLOR;
        ImGui::PushWindowFontScale(2.0f);
        ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                                  ImVec2(origin.x + icon_column * 0.4f, origin.y + font_size * 1.3f),
                                  icon,
                                  icon_color);
        ImGui::PopWindowFontScale();
        const float right = origin.x + ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + icon_column, origin.y));
        ImGui::BeginGroup();
        packages_widgets::draw_title(report.project.c_str());
        // The change since the run before, at the right end of the title's line.
        const auto& change = ctx.get_cached<audit_manager>().get_change();
        if(!change.added.empty() || !change.fixed.empty())
        {
            const std::string added = fmt::format(ICON_MDI_ARROW_UP " {} new", change.added.size());
            const std::string fixed = fmt::format(ICON_MDI_CHECK " {} gone", change.fixed.size());
            const float width = calc_chip_width(added.c_str()) + calc_chip_width(fixed.c_str()) + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine();
            const ImVec2 line = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(ImMax(line.x, right - width), line.y));
            draw_chip(added.c_str(),
                      change.added.empty() ? imgui_style::get_muted_text_color_u32() : get_severity_style(audit::severity::warning).color,
                      "Issues this run found that the run before did not.");
            ImGui::SameLine();
            draw_chip(fixed.c_str(),
                      change.fixed.empty() ? imgui_style::get_muted_text_color_u32() : FIXED_COLOR,
                      "Issues the run before found that this run no longer finds.");
        }
        packages_widgets::draw_muted_wrapped(fmt::format("Audited {} in {} by engine {}.",
                                                         format_time(report.finished_at),
                                                         format_duration(report.seconds),
                                                         report.engine_version)
                                                 .c_str());
        for(std::size_t index = 0; index < report.profiles.size(); ++index)
        {
            const auto& profile = report.profiles[index];
            const auto platform = find_platform(report, profile);
            const std::string text = fmt::format("{} {}", get_platform_icon(platform), profile);
            const std::string tooltip = platform.empty() ? fmt::format("The audit judged the builds of the profile {}.", profile)
                                                         : fmt::format("The audit judged the {} builds of the profile {}.", platform, profile);
            if(index > 0)
            {
                same_line_or_wrap(calc_chip_width(text.c_str()));
            }
            draw_chip(text.c_str(), PROFILE_COLOR, tooltip.c_str());
        }
        ImGui::EndGroup();
    }
    packages_widgets::end_card();
}

void audit_panel::draw_summary_cards(const audit::report& report)
{
    using namespace audit_summary_detail;
    const auto& counts = get_counts(report);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int per_row = avail >= to_pixels(AUDIT_SUMMARY_CARDS_IN_ONE_ROW) ? 4 : 2;
    const float width = ImFloor((avail - gap * float(per_row - 1)) / float(per_row));
    int index = 0;
    for(const auto level : SEVERITY_ORDER)
    {
        const auto& style = get_severity_style(level);
        const auto count = report.count(level);
        const auto rules = count_rules_with(counts, level);
        const std::string caption = count == 0 ? std::string("none open") : fmt::format("in {} rule{}", rules, rules == 1 ? "" : "s");
        const std::string tooltip = fmt::format("Show the {} issues that are not ignored.", style.name);
        const stat_card card{style.icon, style.plural, std::to_string(count), caption, style.color, count == 0, tooltip.c_str()};
        if(index % per_row != 0)
        {
            ImGui::SameLine();
        }
        if(draw_stat_card(fmt::format("##audit_card_{}", style.name).c_str(), card, width))
        {
            show_issues_of({}, static_cast<int>(level));
            shows_ignored_ = false;
        }
        ++index;
    }
    if(index % per_row != 0)
    {
        ImGui::SameLine();
    }
    const stat_card ignored{ICON_MDI_EYE_OFF_OUTLINE,
                            "Ignored",
                            std::to_string(counts.ignored),
                            counts.ignored == 0 ? std::string("none") : std::string("kept on purpose"),
                            IGNORED_COLOR,
                            counts.ignored == 0,
                            "Show the issues the project's audit settings ignore, with the others."};
    if(draw_stat_card("##audit_card_ignored", ignored, width))
    {
        show_issues_of({}, -1);
        shows_ignored_ = true;
    }
}

void audit_panel::draw_top_issues(const audit::report& report)
{
    using namespace audit_summary_detail;
    std::vector<const audit::issue*> top;
    std::size_t open = 0;
    for(const auto& item : report.issues)
    {
        if(item.is_ignored)
        {
            continue;
        }
        ++open;
        if(top.size() < AUDIT_SUMMARY_TOP_ISSUES)
        {
            top.push_back(&item);
        }
    }
    if(!packages_widgets::begin_card("##audit_top_issues"))
    {
        packages_widgets::end_card();
        return;
    }
    if(open == 0)
    {
        draw_section_header("Top Issues");
        ImGui::Spacing();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                                  ImVec2(min.x + ImGui::GetFontSize() * 0.6f, min.y + ImGui::GetTextLineHeight() * 0.5f),
                                  ICON_MDI_CHECK_CIRCLE,
                                  FIXED_COLOR);
        ImGui::SetCursorScreenPos(ImVec2(min.x + ImGui::GetFontSize() * 1.6f, min.y));
        packages_widgets::draw_muted_wrapped("No issue is open: every rule that ran passed, or its issues are ignored.");
    }
    else
    {
        const std::string link = fmt::format("All {} " ICON_MDI_CHEVRON_RIGHT, open);
        if(draw_section_header_link("Top Issues", "##audit_all_issues", link.c_str(), "Show every open issue."))
        {
            show_issues_of({}, -1);
        }
        ImGui::Spacing();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        for(std::size_t index = 0; index < top.size(); ++index)
        {
            if(draw_top_issue_row(*top[index], static_cast<int>(index)))
            {
                select_issue(top[index]->get_id());
            }
        }
        ImGui::PopStyleVar();
    }
    packages_widgets::end_card();
}

void audit_panel::draw_rule_chart(const audit::report& report)
{
    using namespace audit_summary_detail;
    const auto& counts = get_counts(report);
    if(counts.by_rule.empty())
    {
        return;
    }
    std::vector<std::pair<std::string, severity_counts>> rows(counts.by_rule.begin(), counts.by_rule.end());
    std::sort(rows.begin(),
              rows.end(),
              [](const auto& lhs, const auto& rhs)
              {
                  const auto lhs_worst = get_worst(lhs.second);
                  const auto rhs_worst = get_worst(rhs.second);
                  if(lhs_worst != rhs_worst)
                  {
                      return lhs_worst > rhs_worst;
                  }
                  return get_total(lhs.second) > get_total(rhs.second);
              });
    std::size_t largest = 0;
    for(const auto& row : rows)
    {
        largest = std::max(largest, get_total(row.second));
    }
    if(!packages_widgets::begin_card("##audit_rule_chart"))
    {
        packages_widgets::end_card();
        return;
    }
    const std::string right = rows.size() > AUDIT_SUMMARY_TOP_RULES ? fmt::format("Top {} of {} rules", AUDIT_SUMMARY_TOP_RULES, rows.size())
                                                                     : fmt::format("{} rule{}", rows.size(), rows.size() == 1 ? "" : "s");
    draw_section_header("Issues by Rule", right.c_str());
    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    for(std::size_t index = 0; index < rows.size() && index < AUDIT_SUMMARY_TOP_RULES; ++index)
    {
        const auto& [rule_id, rule_counts] = rows[index];
        const auto* owner = audit::find_rule(rule_id);
        const auto& style = get_severity_style(get_worst(rule_counts));
        const std::string count = std::to_string(get_total(rule_counts));
        ImGui::PushID(rule_id.c_str());
        if(draw_bar_row("##audit_rule_bar",
                        style.icon,
                        style.color,
                        owner != nullptr ? owner->title.c_str() : rule_id.c_str(),
                        make_segments(rule_counts),
                        float(largest),
                        count.c_str(),
                        "Show this rule's issues."))
        {
            show_issues_of(rule_id, -1);
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    packages_widgets::end_card();
}

void audit_panel::draw_area_chart(const audit::report& report)
{
    using namespace audit_summary_detail;
    const auto& counts = get_counts(report);
    if(counts.open == 0)
    {
        return;
    }
    std::size_t largest = 0;
    for(const auto& [value, area_counts] : counts.by_area)
    {
        largest = std::max(largest, get_total(area_counts));
    }
    if(!packages_widgets::begin_card("##audit_area_chart"))
    {
        packages_widgets::end_card();
        return;
    }
    draw_section_header("What They Cost", "Issues by area");
    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    for(const auto value : audit::ALL_AREAS)
    {
        const auto found = counts.by_area.find(value);
        const auto area_counts = found != counts.by_area.end() ? found->second : severity_counts{};
        const auto total = get_total(area_counts);
        const std::string count = std::to_string(total);
        ImGui::PushID(static_cast<int>(value));
        ImGui::BeginDisabled(total == 0);
        if(draw_bar_row("##audit_area_bar",
                        get_area_icon(value),
                        imgui_style::get_muted_text_color_u32(),
                        get_area_title(value),
                        make_segments(area_counts),
                        float(largest),
                        count.c_str(),
                        "Show the issues that cost this."))
        {
            show_issues_of({}, -1);
            area_filter_ = static_cast<std::uint16_t>(value);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    packages_widgets::end_card();
}

void audit_panel::draw_profile_list(const audit::report& report)
{
    using namespace audit_summary_detail;
    if(report.profiles.empty())
    {
        return;
    }
    const auto& counts = get_counts(report);
    if(!packages_widgets::begin_card("##audit_profile_list"))
    {
        packages_widgets::end_card();
        return;
    }
    draw_section_header("Profiles", "Open issues in each build");
    ImGui::Spacing();
    const float font_size = ImGui::GetFontSize();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    for(std::size_t index = 0; index < report.profiles.size(); ++index)
    {
        const auto& profile = report.profiles[index];
        const auto platform = find_platform(report, profile);
        const auto found = counts.by_profile.find(profile);
        const auto profile_counts = found != counts.by_profile.end() ? found->second : severity_counts{};
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 1.6f);
        ImGui::PushID(profile.c_str());
        const bool is_clicked = ImGui::InvisibleButton("##audit_profile_row", size);
        ImGui::PopID();
        const bool is_hovered = ImGui::IsItemHovered();
        packages_widgets::draw_row_background(ImRect(min, min + size), static_cast<int>(index), is_hovered, false);
        if(is_hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetItemTooltipEx("%s", "Show the issues of this profile's builds.");
        }
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const float icon_column = to_pixels(AUDIT_SUMMARY_PROFILE_ICON);
        ImGui::PushWindowFontScale(1.3f);
        ImGui::RenderIconCentered(draw_list,
                                  ImVec2(min.x + icon_column * 0.5f, min.y + size.y * 0.5f),
                                  get_platform_icon(platform),
                                  PROFILE_COLOR);
        ImGui::PopWindowFontScale();
        const float text_left = min.x + icon_column;
        const float line_gap = font_size * 0.1f;
        const float top = min.y + (size.y - font_size * 2.0f - line_gap) * 0.5f;
        draw_list->AddText(ImGui::GetFont(ImGui::Font::SemiBold), font_size, ImVec2(text_left, top), ImGui::GetColorU32(ImGuiCol_Text), profile.c_str());
        const auto total = get_total(profile_counts);
        const std::string issues = fmt::format("{} open issue{}", total, total == 1 ? "" : "s");
        const std::string subtitle = platform.empty() || platform == profile ? issues : fmt::format("{} build, {}", platform, issues);
        draw_list->AddText(ImVec2(text_left, top + font_size + line_gap), imgui_style::get_muted_text_color_u32(), subtitle.c_str());
        // The counts at the right end, worst first, each in its severity's color.
        float right = min.x + size.x - font_size * 0.6f;
        for(auto level_it = SEVERITY_ORDER.rbegin(); level_it != SEVERITY_ORDER.rend(); ++level_it)
        {
            const auto& style = get_severity_style(*level_it);
            const auto count = profile_counts[static_cast<std::size_t>(*level_it)];
            const std::string text = fmt::format("{} {}", style.icon, count);
            const float width = ImGui::CalcTextSize(text.c_str()).x;
            right -= width;
            draw_list->AddText(ImVec2(right, min.y + (size.y - font_size) * 0.5f),
                               count > 0 ? style.color : with_alpha(imgui_style::get_muted_text_color_u32(), 0.6f),
                               text.c_str());
            right -= font_size * 0.9f;
        }
        if(is_clicked)
        {
            show_issues_of({}, -1);
            profile_filter_ = profile;
        }
    }
    ImGui::PopStyleVar();
    packages_widgets::end_card();
}

void audit_panel::draw_notes(const audit::report& report)
{
    using namespace audit_summary_detail;
    if(report.notes.empty())
    {
        return;
    }
    begin_callout("##audit_notes", ICON_MDI_ALERT_OUTLINE, "Not Checked", nullptr, get_severity_style(audit::severity::warning).color);
    for(const auto& note : report.notes)
    {
        draw_callout_text(note.c_str(), true);
    }
    end_callout();
    ImGui::Spacing();
}

} // namespace unravel
