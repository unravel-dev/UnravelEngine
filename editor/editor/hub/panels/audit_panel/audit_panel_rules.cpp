#include "audit_panel.h"
#include "../packages_panel/packages_widgets.h"
#include "../panel_toolbar.h"
#include "audit_panel_widgets.h"

#include <audit/audit_config.h>
#include <editor/audit/audit_manager.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <logging/logging.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <array>
#include <unordered_map>

namespace unravel
{
namespace audit_rules_detail
{

using namespace audit_widgets;

// Sizes in font units.
constexpr float AUDIT_RULES_SEARCH_WIDTH = 16.0f;
constexpr float AUDIT_RULES_SEARCH_MIN_WIDTH = 6.0f;
constexpr float AUDIT_RULES_SIDE_BY_SIDE = 58.0f;
constexpr float AUDIT_RULES_MIN_LIST_WIDTH = 20.0f;
constexpr float AUDIT_RULES_MIN_DETAILS_WIDTH = 26.0f;
constexpr float AUDIT_RULES_MIN_HEIGHT = 6.0f;
constexpr float AUDIT_RULES_MIN_DETAILS_HEIGHT = 10.0f;
constexpr float AUDIT_RULES_TITLE_SCALE = 1.25f;
constexpr float AUDIT_RULES_SECTION_GAP = 0.6f;
constexpr float AUDIT_RULES_CHECK_COLUMN = 1.9f;
constexpr float AUDIT_RULES_ICON_COLUMN = 1.5f;

/// The rule sources in the order the catalog first names them.
auto list_sources() -> std::vector<audit::rule_source>
{
    std::vector<audit::rule_source> sources;
    for(const auto& owner : audit::get_rules())
    {
        if(std::find(sources.begin(), sources.end(), owner.source) == sources.end())
        {
            sources.push_back(owner.source);
        }
    }
    return sources;
}

auto matches(const ImGuiTextFilter& filter, const audit::rule& owner) -> bool
{
    if(!filter.IsActive())
    {
        return true;
    }
    const std::string text = owner.title + " " + owner.id + " " + owner.description;
    return filter.PassFilter(text.c_str());
}

/// A group's header: a chevron, the source's icon and title, and how many of its rules run.
auto draw_source_row(audit::rule_source source, bool is_collapsed, std::size_t enabled, std::size_t total) -> bool
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    ImGui::PushID(static_cast<int>(source));
    const bool is_clicked = ImGui::InvisibleButton("##audit_source_row", size);
    ImGui::PopID();
    const bool is_hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(min, min + size, ImGui::GetColorU32(ImGuiCol_Text, is_hovered ? 0.07f : 0.035f));
    const float padding = font_size * 0.45f;
    const float center_y = min.y + size.y * 0.5f;
    ImGui::RenderIconCentered(draw_list,
                              ImVec2(min.x + padding + font_size * 0.4f, center_y),
                              is_collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN,
                              imgui_style::get_muted_text_color_u32());
    ImGui::RenderIconCentered(draw_list,
                              ImVec2(min.x + font_size * 2.2f, center_y),
                              get_rule_source_icon(source),
                              imgui_style::get_muted_text_color_u32());
    const float text_y = ImFloor(min.y + (size.y - font_size) * 0.5f);
    draw_list->AddText(ImGui::GetFont(ImGui::Font::SemiBold),
                       font_size,
                       ImVec2(min.x + font_size * 3.3f, text_y),
                       ImGui::GetColorU32(ImGuiCol_Text),
                       get_rule_source_title(source));
    const std::string count = fmt::format("{} of {} run", enabled, total);
    const float width = ImGui::CalcTextSize(count.c_str()).x;
    draw_list->AddText(ImVec2(min.x + size.x - padding - width, text_y), imgui_style::get_muted_text_color_u32(), count.c_str());
    return is_clicked;
}

/// A threshold's value for one platform: a field, accented when it differs from the rule's own value, and a reset.
/// @return True with `value` set when the user changed it.
auto draw_threshold_field(const char* id, std::int64_t current, std::int64_t own, std::int64_t& value) -> bool
{
    ImGui::PushID(id);
    bool is_changed = false;
    const bool differs = current != own;
    const float reset_width = differs ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    ImGui::SetNextItemWidth(ImMax(1.0f, ImGui::GetContentRegionAvail().x - reset_width));
    if(differs)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_accent_color());
    }
    // The typed value is kept while the field is active and applied when it is left: settings are written once, not
    // on every key.
    // Each field keeps its own typed value: focus moving to a field drawn earlier in the frame must not take the
    // value of the one being left before that one commits it.
    static std::unordered_map<ImGuiID, std::int64_t> typed;
    const ImGuiID field_id = ImGui::GetID("##value");
    const auto found = typed.find(field_id);
    std::int64_t edited = found != typed.end() ? found->second : current;
    ImGui::InputScalar("##value", ImGuiDataType_S64, &edited);
    if(ImGui::IsItemActive())
    {
        typed[field_id] = edited;
    }
    if(ImGui::IsItemDeactivatedAfterEdit())
    {
        value = edited;
        is_changed = edited != current;
    }
    if(!ImGui::IsItemActive())
    {
        typed.erase(field_id);
    }
    if(differs)
    {
        ImGui::PopStyleColor();
    }
    ImGui::SetItemTooltipEx("%s",
                            differs ? "The project's own value. A new value is kept in the project's audit settings when you leave the field."
                                    : "The rule's own value. A new value is kept in the project's audit settings when you leave the field.");
    if(differs)
    {
        ImGui::SameLine();
        if(draw_icon_button("##reset", ICON_MDI_HISTORY, fmt::format("Go back to the rule's own value, {}.", own).c_str()))
        {
            value = own;
            is_changed = true;
        }
    }
    ImGui::PopID();
    return is_changed;
}

} // namespace audit_rules_detail

void audit_panel::draw_rules(rtti::context& ctx)
{
    using namespace audit_rules_detail;
    auto& manager = ctx.get_cached<audit_manager>();
    const auto* report = manager.get_report();
    std::map<std::string, std::size_t> counts;
    if(report != nullptr)
    {
        for(const auto& item : report->issues)
        {
            counts[item.rule] += item.is_ignored ? 0 : 1;
        }
    }
    const auto& rules = audit::get_rules();
    const auto enabled = static_cast<std::size_t>(std::count_if(rules.begin(),
                                                               rules.end(),
                                                               [&manager](const audit::rule& owner)
                                                               {
                                                                   return manager.get_config().is_rule_enabled(owner.id);
                                                               }));
    if(panel_toolbar::begin_strip("##audit_rules_strip"))
    {
        panel_toolbar::search_field(rule_filter_text_,
                                    ICON_MDI_MAGNIFY " Search rules...",
                                    "Search the rules' titles, ids and descriptions.",
                                    to_pixels(AUDIT_RULES_SEARCH_MIN_WIDTH),
                                    to_pixels(AUDIT_RULES_SEARCH_WIDTH));
        panel_toolbar::align_right();
        const std::string readout = fmt::format("{} of {} rules run", enabled, rules.size());
        panel_toolbar::label(readout.c_str());
    }
    panel_toolbar::end_strip();

    if(audit::find_rule(selected_rule_) == nullptr && !rules.empty())
    {
        selected_rule_ = rules.front().id;
    }
    const bool is_side_by_side = ImGui::GetContentRegionAvail().x >= to_pixels(AUDIT_RULES_SIDE_BY_SIDE);
    pane_split split(is_side_by_side ? "##audit_rule_split" : "##audit_rule_split_stacked",
                     is_side_by_side,
                     is_side_by_side ? rules_split_ : rules_stacked_split_,
                     to_pixels(is_side_by_side ? AUDIT_RULES_MIN_LIST_WIDTH : AUDIT_RULES_MIN_HEIGHT),
                     to_pixels(is_side_by_side ? AUDIT_RULES_MIN_DETAILS_WIDTH : AUDIT_RULES_MIN_DETAILS_HEIGHT));
    if(ImGui::BeginChild(is_side_by_side ? "##audit_rule_list_side" : "##audit_rule_list_stacked", split.get_first_size()))
    {
        draw_rule_list(ctx, counts);
    }
    ImGui::EndChild();
    split.draw_handle();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 40));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(to_pixels(1.0f), to_pixels(0.8f)));
    const bool is_open = ImGui::BeginChild("##audit_rule_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if(is_open)
    {
        const auto* owner = audit::find_rule(selected_rule_);
        if(owner != nullptr)
        {
            const auto found = counts.find(owner->id);
            draw_rule_details(ctx, *owner, found != counts.end() ? found->second : 0);
        }
    }
    ImGui::EndChild();
}

void audit_panel::draw_rule_list(rtti::context& ctx, const std::map<std::string, std::size_t>& counts)
{
    using namespace audit_rules_detail;
    auto& manager = ctx.get_cached<audit_manager>();
    const auto& config = manager.get_config();
    const float font_size = ImGui::GetFontSize();
    bool has_any = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    int row_index = 0;
    for(const auto source : list_sources())
    {
        std::vector<const audit::rule*> shown;
        std::size_t enabled = 0;
        std::size_t total = 0;
        for(const auto& owner : audit::get_rules())
        {
            if(owner.source != source)
            {
                continue;
            }
            ++total;
            enabled += config.is_rule_enabled(owner.id) ? 1 : 0;
            if(matches(rule_filter_text_, owner))
            {
                shown.push_back(&owner);
            }
        }
        if(shown.empty())
        {
            continue;
        }
        has_any = true;
        const int source_key = static_cast<int>(source);
        const bool is_collapsed = collapsed_rule_sources_.count(source_key) > 0 && !rule_filter_text_.IsActive();
        if(draw_source_row(source, is_collapsed, enabled, total))
        {
            if(collapsed_rule_sources_.count(source_key) > 0)
            {
                collapsed_rule_sources_.erase(source_key);
            }
            else
            {
                collapsed_rule_sources_.insert(source_key);
            }
        }
        if(is_collapsed)
        {
            continue;
        }
        for(const auto* owner : shown)
        {
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
            ImGui::PushID(owner->id.c_str());
            const bool is_clicked = ImGui::InvisibleButton("##audit_rule_row", size, ImGuiButtonFlags_AllowOverlap);
            const bool is_hovered = ImGui::IsItemHovered();
            const bool is_selected = owner->id == selected_rule_;
            packages_widgets::draw_row_background(ImRect(min, min + size), row_index++, is_hovered, is_selected);
            ImGui::SetItemTooltipEx("%s", owner->description.c_str());

            // The switch that turns the rule on and off, over the row.
            const bool is_enabled = config.is_rule_enabled(owner->id);
            ImGui::SetCursorScreenPos(ImVec2(min.x + font_size * 0.3f, min.y));
            bool checked = is_enabled;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.y, ImGui::GetStyle().FramePadding.y));
            if(ImGui::Checkbox("##enabled", &checked))
            {
                std::string error;
                const bool is_done = manager.set_rule_enabled(owner->id, checked, error);
                set_message(is_done ? std::string("The rule's change applies from the next run.") : error, !is_done);
            }
            ImGui::PopStyleVar();
            ImGui::SetItemTooltipEx("%s", is_enabled ? "The rule runs. Click to turn it off in the project's audit settings."
                                                     : "The rule is off. Click to turn it on in the project's audit settings.");

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const auto& style = get_severity_style(owner->default_severity);
            const float icon_x = min.x + to_pixels(AUDIT_RULES_CHECK_COLUMN) + font_size * 0.5f;
            ImGui::RenderIconCentered(draw_list,
                                      ImVec2(icon_x, min.y + size.y * 0.5f),
                                      style.icon,
                                      is_enabled ? style.color : with_alpha(style.color, 0.4f));
            const float text_left = min.x + to_pixels(AUDIT_RULES_CHECK_COLUMN + AUDIT_RULES_ICON_COLUMN);
            const auto found = counts.find(owner->id);
            const std::size_t count = found != counts.end() ? found->second : 0;
            const std::string count_text = is_enabled ? std::to_string(count) : std::string("Off");
            const float chip_width = calc_chip_width(count_text.c_str());
            const float text_right = min.x + size.x - chip_width - font_size * 0.6f;
            const float text_y = ImFloor(min.y + (size.y - font_size) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, is_enabled ? ImGui::GetColorU32(ImGuiCol_Text) : imgui_style::get_muted_text_color_u32());
            ImGui::RenderTextEllipsis(draw_list, ImVec2(text_left, text_y), ImVec2(text_right, min.y + size.y), text_right, owner->title.c_str(), nullptr, nullptr);
            ImGui::PopStyleColor();
            if(!is_enabled || count > 0)
            {
                draw_chip_at(ImVec2(min.x + size.x - chip_width - font_size * 0.3f, min.y),
                             count_text.c_str(),
                             is_enabled ? style.color : imgui_style::get_muted_text_color_u32());
            }
            ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + size.y));
            ImGui::PopID();
            if(is_clicked)
            {
                selected_rule_ = owner->id;
            }
        }
    }
    ImGui::PopStyleVar();
    if(!has_any)
    {
        packages_widgets::draw_centered_hint("No rule matches", "Change the search to see the other rules.");
    }
}

void audit_panel::draw_rule_details(rtti::context& ctx, const audit::rule& owner, std::size_t issue_count)
{
    using namespace audit_rules_detail;
    auto& manager = ctx.get_cached<audit_manager>();
    const auto& config = manager.get_config();
    const bool is_enabled = config.is_rule_enabled(owner.id);

    // What the rule is: its severity, what it reads and what its issues cost, and whether it runs.
    const auto& style = get_severity_style(owner.default_severity);
    const std::string severity = fmt::format("{} {}", style.icon, style.name);
    draw_chip(severity.c_str(), style.color, "How much the rule's issues matter.");
    ImGui::SameLine();
    const std::string source = fmt::format("{} {}", get_rule_source_icon(owner.source), get_rule_source_title(owner.source));
    draw_chip(source.c_str(), imgui_style::get_muted_text_color_u32(), "What the rule reads.");
    for(const auto value : audit::ALL_AREAS)
    {
        if(audit::has_area(owner.areas, value))
        {
            const std::string text = fmt::format("{} {}", get_area_icon(value), get_area_title(value));
            same_line_or_wrap(calc_chip_width(text.c_str()));
            draw_chip(text.c_str(), AREA_COLOR, "What the rule's issues cost.");
        }
    }
    ImGui::Spacing();
    ImGui::PushFont(ImGui::GetFont(ImGui::Font::Bold), ImGui::GetCurrentContext()->FontSizeBase * AUDIT_RULES_TITLE_SCALE);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(owner.title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    bool checked = is_enabled;
    if(ImGui::Checkbox("Run this rule", &checked))
    {
        std::string error;
        const bool is_done = manager.set_rule_enabled(owner.id, checked, error);
        set_message(is_done ? std::string("The rule's change applies from the next run.") : error, !is_done);
    }
    ImGui::SetItemTooltipEx("%s", "Whether the rule runs, in the project's audit settings (settings/audit.cfg).");

    ImGui::Dummy(ImVec2(0.0f, to_pixels(AUDIT_RULES_SECTION_GAP)));
    draw_section_header("What It Checks");
    packages_widgets::draw_muted_wrapped(owner.description.c_str());
    if(owner.per_profile)
    {
        packages_widgets::draw_muted_wrapped("It judges each profile's builds by the values of that profile's platform.");
    }

    ImGui::Dummy(ImVec2(0.0f, to_pixels(AUDIT_RULES_SECTION_GAP)));
    begin_callout("##audit_rule_advice",
                  ICON_MDI_LIGHTBULB_ON_OUTLINE,
                  "What to Do",
                  owner.recommendation.c_str(),
                  ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()));
    end_callout();

    if(!owner.thresholds.empty())
    {
        ImGui::Dummy(ImVec2(0.0f, to_pixels(AUDIT_RULES_SECTION_GAP)));
        draw_section_header("Thresholds", "Each platform's value");
        constexpr std::array<target_platform, audit::PLATFORM_COUNT> PLATFORMS{target_platform::desktop,
                                                                               target_platform::android,
                                                                               target_platform::web};
        constexpr std::array<const char*, audit::PLATFORM_COUNT> PLATFORM_TITLES{ICON_MDI_MONITOR " Desktop",
                                                                                 ICON_MDI_ANDROID " Android",
                                                                                 ICON_MDI_WEB " Web"};
        if(ImGui::BeginTable("##audit_thresholds",
                             1 + static_cast<int>(PLATFORMS.size()),
                             ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Threshold", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            for(const auto* title : PLATFORM_TITLES)
            {
                ImGui::TableSetupColumn(title, ImGuiTableColumnFlags_WidthStretch, 1.0f);
            }
            ImGui::TableHeadersRow();
            for(const auto& item : owner.thresholds)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(fmt::format("{} ({})", item.label, item.unit).c_str());
                ImGui::SetItemTooltipEx("%s", item.description.c_str());
                for(std::size_t index = 0; index < PLATFORMS.size(); ++index)
                {
                    ImGui::TableNextColumn();
                    const auto platform = PLATFORMS[index];
                    const auto current = config.get_threshold(owner, item.name, platform);
                    std::int64_t value = current;
                    const std::string id = fmt::format("{}_{}", item.name, index);
                    if(draw_threshold_field(id.c_str(), current, item.values[index], value))
                    {
                        std::string error;
                        const bool is_done = manager.set_threshold(owner.id, item.name, platform, value, error);
                        set_message(is_done ? std::string("The threshold applies from the next run.") : error, !is_done);
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    ImGui::Dummy(ImVec2(0.0f, to_pixels(AUDIT_RULES_SECTION_GAP)));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, with_alpha(imgui_style::get_muted_text_color_u32(), 0.75f));
    ImGui::TextUnformatted(fmt::format("Rule {}", owner.id).c_str());
    ImGui::PopStyleColor();
    if(issue_count > 0)
    {
        ImGui::SameLine();
        const std::string label = fmt::format("Show {} Issue{} " ICON_MDI_CHEVRON_RIGHT, issue_count, issue_count == 1 ? "" : "s");
        if(ImGui::SmallButton(label.c_str()))
        {
            show_issues_of(owner.id, -1);
        }
    }
}

} // namespace unravel
