#include "audit_panel.h"
#include "../packages_panel/packages_widgets.h"
#include "../panel_toolbar.h"
#include "audit_panel_widgets.h"

#include <audit/audit_facts.h>
#include <editor/audit/audit_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <engine/engine.h>
#include <logging/logging.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace unravel
{
namespace audit_insights_detail
{

using namespace audit_widgets;

// Sizes in font units.
constexpr float AUDIT_INSIGHTS_MIN_NAV_WIDTH = 11.0f;
constexpr float AUDIT_INSIGHTS_MIN_TABLE_WIDTH = 30.0f;
constexpr float AUDIT_INSIGHTS_SEARCH_WIDTH = 14.0f;
constexpr float AUDIT_INSIGHTS_SEARCH_MIN_WIDTH = 6.0f;
constexpr float AUDIT_INSIGHTS_ROW_ICON = 1.6f;
constexpr float AUDIT_INSIGHTS_TITLE_SCALE = 1.25f;
constexpr float AUDIT_INSIGHTS_BAR_ALPHA = 0.22f;
constexpr float AUDIT_INSIGHTS_BAR_INSET = 0.18f;

auto get_table_icon(const std::string& id) -> const char*
{
    if(id == "largest_assets")
    {
        return ICON_MDI_CHART_BAR;
    }
    if(id == "assets_by_type")
    {
        return ICON_MDI_CHART_DONUT;
    }
    if(id == "unused_assets")
    {
        return ICON_MDI_DELETE_OUTLINE;
    }
    if(id == "textures")
    {
        return ICON_MDI_IMAGE_OUTLINE;
    }
    if(id == "audio")
    {
        return ICON_MDI_VOLUME_HIGH;
    }
    if(id == "meshes")
    {
        return ICON_MDI_CUBE_OUTLINE;
    }
    if(id == "profiles")
    {
        return ICON_MDI_LAYERS_OUTLINE;
    }
    if(id == "deploys")
    {
        return ICON_MDI_TRUCK_OUTLINE;
    }
    if(id == "deploy_contents")
    {
        return ICON_MDI_PACKAGE_VARIANT;
    }
    if(id == "update_methods")
    {
        return ICON_MDI_CODE_BRACES;
    }
    return ICON_MDI_TABLE;
}

auto get_group(const audit::insight_table& table) -> std::string
{
    return table.group.empty() ? std::string("Tables") : table.group;
}

auto is_right_aligned(audit::column_kind kind) -> bool
{
    return kind == audit::column_kind::number || kind == audit::column_kind::bytes || kind == audit::column_kind::change;
}

/// The row's cells as one line, for the search and for copying.
auto join_row(const std::vector<audit::insight_cell>& row, const char* separator) -> std::string
{
    std::string text;
    for(const auto& cell : row)
    {
        if(!text.empty())
        {
            text += separator;
        }
        text += cell.text;
    }
    return text;
}

auto compare_cells(const audit::insight_cell& left, const audit::insight_cell& right) -> int
{
    if(left.is_number && right.is_number)
    {
        return left.number < right.number ? -1 : right.number < left.number ? 1 : 0;
    }
    if(left.is_number != right.is_number)
    {
        // Numbers before text such as "No earlier deploy", whichever way the column sorts.
        return left.is_number ? -1 : 1;
    }
    return left.text < right.text ? -1 : right.text < left.text ? 1 : 0;
}

/// Text at the right end of the cell.
void draw_right_aligned(const char* text, ImU32 color)
{
    const float width = ImGui::CalcTextSize(text).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, avail - width));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

/// A size with a bar of its share of the column's largest behind it.
void draw_bytes_cell(const audit::insight_cell& cell, double largest)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight();
    if(cell.is_number && largest > 0.0)
    {
        const float inset = to_pixels(AUDIT_INSIGHTS_BAR_INSET);
        const float share = static_cast<float>(std::clamp(cell.number / largest, 0.0, 1.0));
        const ImVec4 accent = imgui_style::get_accent_color();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x, min.y + inset),
                                                  ImVec2(min.x + ImMax(1.0f, width * share), min.y + height - inset),
                                                  ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, AUDIT_INSIGHTS_BAR_ALPHA)),
                                                  ImGui::GetFontSize() * 0.2f);
    }
    ImGui::AlignTextToFramePadding();
    draw_right_aligned(cell.text.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
}

void draw_change_cell(const audit::insight_cell& cell)
{
    ImGui::AlignTextToFramePadding();
    if(!cell.is_number)
    {
        draw_right_aligned(cell.text.c_str(), imgui_style::get_muted_text_color_u32());
        return;
    }
    const ImU32 color = cell.number > 0.0   ? GROWTH_COLOR
                        : cell.number < 0.0 ? SHRINK_COLOR
                                            : imgui_style::get_muted_text_color_u32();
    draw_right_aligned(cell.text.c_str(), color);
}

void reveal_asset(const std::string& key)
{
    auto& ctx = engine::context();
    if(editor_actions::reveal_in_content_browser(ctx, key))
    {
        editor_actions::select_asset(ctx, key);
    }
}

/// The key a row names, from its first cell that links to one; empty when none does.
auto find_row_link(const std::vector<audit::insight_cell>& row) -> std::string
{
    for(const auto& cell : row)
    {
        if(!cell.link.empty())
        {
            return cell.link;
        }
    }
    return {};
}

} // namespace audit_insights_detail

void audit_panel::draw_insights(const audit::report& report)
{
    using namespace audit_insights_detail;
    if(report.insights.empty())
    {
        packages_widgets::draw_centered_hint("No insights", "The run stopped before it made its tables. Run the audit again.");
        return;
    }
    const auto found = std::find_if(report.insights.begin(),
                                    report.insights.end(),
                                    [this](const audit::insight_table& table)
                                    {
                                        return table.id == selected_insight_;
                                    });
    if(found == report.insights.end())
    {
        selected_insight_ = report.insights.front().id;
        insight_filter_.Clear();
    }
    pane_split split("##audit_insight_split",
                     true,
                     insights_split_,
                     to_pixels(AUDIT_INSIGHTS_MIN_NAV_WIDTH),
                     to_pixels(AUDIT_INSIGHTS_MIN_TABLE_WIDTH));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 40));
    const bool is_nav_open = ImGui::BeginChild("##audit_insight_nav_pane", split.get_first_size(), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    if(is_nav_open)
    {
        draw_insight_navigation(report);
    }
    ImGui::EndChild();
    split.draw_handle();
    if(ImGui::BeginChild("##audit_insight_view", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
    {
        for(const auto& table : report.insights)
        {
            if(table.id == selected_insight_)
            {
                draw_insight_table(table);
                break;
            }
        }
    }
    ImGui::EndChild();
}

void audit_panel::draw_insight_navigation(const audit::report& report)
{
    using namespace audit_insights_detail;
    std::vector<std::string> groups;
    for(const auto& table : report.insights)
    {
        const auto group = get_group(table);
        if(std::find(groups.begin(), groups.end(), group) == groups.end())
        {
            groups.push_back(group);
        }
    }
    const float font_size = ImGui::GetFontSize();
    int row_index = 0;
    for(const auto& group : groups)
    {
        if(row_index > 0)
        {
            ImGui::Spacing();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
        ImGui::PushFont(ImGui::Font::SemiBold);
        ImGui::TextUnformatted(group.c_str());
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        for(const auto& table : report.insights)
        {
            if(get_group(table) != group)
            {
                continue;
            }
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
            ImGui::PushID(table.id.c_str());
            const bool is_clicked = ImGui::InvisibleButton("##audit_insight_nav_row", size);
            ImGui::PopID();
            const bool is_selected = table.id == selected_insight_;
            const bool is_hovered = ImGui::IsItemHovered();
            packages_widgets::draw_row_background(ImRect(min, min + size), 0, is_hovered, is_selected);
            ImGui::SetItemTooltipEx("%s", table.description.c_str());
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const float padding = font_size * 0.5f;
            const bool is_empty = table.rows.empty();
            const ImU32 text_color = is_empty ? imgui_style::get_muted_text_color_u32() : ImGui::GetColorU32(ImGuiCol_Text);
            ImGui::RenderIconCentered(draw_list,
                                      ImVec2(min.x + padding + font_size * 0.5f, min.y + size.y * 0.5f),
                                      get_table_icon(table.id),
                                      is_selected ? ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()) : imgui_style::get_muted_text_color_u32());
            const std::string count = std::to_string(table.rows.size());
            const float count_width = ImGui::CalcTextSize(count.c_str()).x;
            const float text_y = ImFloor(min.y + (size.y - font_size) * 0.5f);
            const float title_left = min.x + padding + to_pixels(AUDIT_INSIGHTS_ROW_ICON);
            const float title_right = min.x + size.x - padding - count_width - font_size * 0.5f;
            ImGui::PushStyleColor(ImGuiCol_Text, text_color);
            ImGui::RenderTextEllipsis(draw_list,
                                      ImVec2(title_left, text_y),
                                      ImVec2(title_right, min.y + size.y),
                                      title_right,
                                      table.title.c_str(),
                                      nullptr,
                                      nullptr);
            ImGui::PopStyleColor();
            draw_list->AddText(ImVec2(min.x + size.x - padding - count_width, text_y), imgui_style::get_muted_text_color_u32(), count.c_str());
            if(is_clicked && !is_selected)
            {
                selected_insight_ = table.id;
                insight_filter_.Clear();
            }
            ++row_index;
        }
        ImGui::PopStyleVar();
    }
}

void audit_panel::draw_insight_table(const audit::insight_table& table)
{
    using namespace audit_insights_detail;
    const float font_size = ImGui::GetFontSize();

    // The title, what the table lists, and its totals.
    const ImVec2 title_min = ImGui::GetCursorScreenPos();
    ImGui::PushWindowFontScale(AUDIT_INSIGHTS_TITLE_SCALE);
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                              ImVec2(title_min.x + font_size * 0.6f, title_min.y + font_size * AUDIT_INSIGHTS_TITLE_SCALE * 0.6f),
                              get_table_icon(table.id),
                              ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()));
    ImGui::PopWindowFontScale();
    ImGui::SetCursorScreenPos(ImVec2(title_min.x + font_size * 1.9f, title_min.y));
    ImGui::PushFont(ImGui::GetFont(ImGui::Font::Bold), ImGui::GetCurrentContext()->FontSizeBase * AUDIT_INSIGHTS_TITLE_SCALE);
    ImGui::TextUnformatted(table.title.c_str());
    ImGui::PopFont();
    packages_widgets::draw_muted_wrapped(table.description.c_str());
    ImGui::Spacing();

    if(table.rows.empty())
    {
        packages_widgets::draw_centered_hint("Nothing to list",
                                             table.empty_text.empty() ? "The table has no rows." : table.empty_text.c_str());
        return;
    }

    // The rows the search keeps, worked out again only when the report, the table or the search changes.
    const auto revision = engine::context().get_cached<audit_manager>().get_revision();
    const std::string filter_key = fmt::format("{}|{}|{}", revision, table.id, insight_filter_.InputBuf);
    if(filter_key != insight_filter_key_)
    {
        insight_filter_key_ = filter_key;
        insight_sort_key_.clear();
        insight_order_.clear();
        for(std::size_t index = 0; index < table.rows.size(); ++index)
        {
            if(!insight_filter_.IsActive() || insight_filter_.PassFilter(join_row(table.rows[index], " ").c_str()))
            {
                insight_order_.push_back(index);
            }
        }
    }
    auto& order = insight_order_;
    const auto columns = static_cast<int>(table.columns.size());

    // A strip with the counts and the search.
    if(panel_toolbar::begin_strip("##audit_insight_strip"))
    {
        const std::string rows_text = order.size() == table.rows.size()
                                          ? fmt::format("{} row{}", table.rows.size(), table.rows.size() == 1 ? "" : "s")
                                          : fmt::format("{} of {} rows", order.size(), table.rows.size());
        panel_toolbar::label(rows_text.c_str());
        // The total of the first size column, over the rows shown.
        for(int column = 0; column < columns; ++column)
        {
            if(table.get_kind(static_cast<std::size_t>(column)) != audit::column_kind::bytes)
            {
                continue;
            }
            double total = 0.0;
            for(const auto index : order)
            {
                const auto& row = table.rows[index];
                if(static_cast<std::size_t>(column) < row.size() && row[static_cast<std::size_t>(column)].is_number)
                {
                    total += row[static_cast<std::size_t>(column)].number;
                }
            }
            panel_toolbar::separator();
            const std::string total_text = fmt::format("{}: {} in these rows",
                                                       table.columns[static_cast<std::size_t>(column)],
                                                       audit::format_bytes(static_cast<std::uint64_t>(total)));
            panel_toolbar::label(total_text.c_str());
            break;
        }
        panel_toolbar::align_right();
        panel_toolbar::search_field(insight_filter_,
                                    ICON_MDI_MAGNIFY " Filter rows...",
                                    "Show the rows whose cells hold this text.",
                                    to_pixels(AUDIT_INSIGHTS_SEARCH_MIN_WIDTH),
                                    to_pixels(AUDIT_INSIGHTS_SEARCH_WIDTH));
    }
    panel_toolbar::end_strip();

    if(columns == 0)
    {
        return;
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Hideable;
    if(!ImGui::BeginTable(fmt::format("##audit_insight_{}", table.id).c_str(), columns, flags))
    {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    for(int column = 0; column < columns; ++column)
    {
        const auto kind = table.get_kind(static_cast<std::size_t>(column));
        ImGuiTableColumnFlags column_flags = ImGuiTableColumnFlags_WidthStretch;
        if(column == table.sort_column)
        {
            column_flags |= ImGuiTableColumnFlags_DefaultSort;
        }
        if(is_right_aligned(kind) || kind == audit::column_kind::time)
        {
            // A first click on a measure puts the largest first.
            column_flags |= ImGuiTableColumnFlags_PreferSortDescending;
        }
        if(column == 0)
        {
            column_flags |= ImGuiTableColumnFlags_NoHide;
        }
        const float weight = kind == audit::column_kind::path ? 3.5f : kind == audit::column_kind::bytes ? 1.6f : column == 0 ? 2.5f : 1.0f;
        ImGui::TableSetupColumn(table.columns[static_cast<std::size_t>(column)].c_str(), column_flags, weight);
    }
    ImGui::TableHeadersRow();

    // Sorted, and each column's largest value over the rows shown found for the bars, only when the rows or the
    // sort changed.
    const auto* specs = ImGui::TableGetSortSpecs();
    const bool has_sort = specs != nullptr && specs->SpecsCount > 0;
    const std::string sort_key = has_sort ? fmt::format("{}|{}", specs->Specs[0].ColumnIndex, static_cast<int>(specs->Specs[0].SortDirection))
                                          : std::string("none");
    if(sort_key != insight_sort_key_)
    {
        insight_sort_key_ = sort_key;
        if(has_sort)
        {
            const auto column = static_cast<std::size_t>(specs->Specs[0].ColumnIndex);
            const bool is_ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            std::stable_sort(order.begin(),
                             order.end(),
                             [&](std::size_t lhs, std::size_t rhs)
                             {
                                 const auto& left_row = table.rows[lhs];
                                 const auto& right_row = table.rows[rhs];
                                 if(column >= left_row.size() || column >= right_row.size())
                                 {
                                     return false;
                                 }
                                 const int order_value = compare_cells(left_row[column], right_row[column]);
                                 return is_ascending ? order_value < 0 : order_value > 0;
                             });
        }
        else
        {
            std::sort(order.begin(), order.end());
        }
        insight_largest_.assign(static_cast<std::size_t>(columns), 0.0);
        for(const auto index : order)
        {
            const auto& row = table.rows[index];
            for(std::size_t column = 0; column < row.size() && column < insight_largest_.size(); ++column)
            {
                if(row[column].is_number)
                {
                    insight_largest_[column] = std::max(insight_largest_[column], std::abs(row[column].number));
                }
            }
        }
    }
    const auto& largest = insight_largest_;

    const float row_height = ImGui::GetFrameHeight();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(order.size()), row_height);
    while(clipper.Step())
    {
        for(int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index)
        {
            const auto& row = table.rows[order[static_cast<std::size_t>(row_index)]];
            const auto link = find_row_link(row);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
            ImGui::PushID(row_index);
            for(int column = 0; column < columns && column < static_cast<int>(row.size()); ++column)
            {
                ImGui::TableNextColumn();
                const auto& cell = row[static_cast<std::size_t>(column)];
                if(column == 0)
                {
                    // The whole row reacts: hover, a double-click to show its asset, a menu to copy it.
                    const ImVec2 start = ImGui::GetCursorScreenPos();
                    ImGui::Selectable("##audit_insight_row",
                                      false,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                          ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(0.0f, row_height));
                    if(!link.empty() && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        reveal_asset(link);
                    }
                    if(ImGui::BeginPopupContextItem("##audit_insight_row_menu"))
                    {
                        if(!link.empty() && ImGui::MenuItem(ICON_MDI_OPEN_IN_NEW " Show in Content Browser"))
                        {
                            reveal_asset(link);
                        }
                        if(!link.empty() && ImGui::MenuItem(ICON_MDI_CONTENT_COPY " Copy Path"))
                        {
                            ImGui::SetClipboardText(link.c_str());
                        }
                        if(ImGui::MenuItem(ICON_MDI_CONTENT_COPY " Copy Row"))
                        {
                            ImGui::SetClipboardText(join_row(row, "\t").c_str());
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::SetCursorScreenPos(start);
                }
                switch(table.get_kind(static_cast<std::size_t>(column)))
                {
                    case audit::column_kind::path:
                        if(draw_path_item("##audit_insight_path", cell.text, !cell.link.empty(), "Click to show it in the Content Browser."))
                        {
                            reveal_asset(cell.link);
                        }
                        break;
                    case audit::column_kind::bytes:
                        draw_bytes_cell(cell, largest[static_cast<std::size_t>(column)]);
                        break;
                    case audit::column_kind::change:
                        draw_change_cell(cell);
                        break;
                    case audit::column_kind::number:
                        ImGui::AlignTextToFramePadding();
                        draw_right_aligned(cell.text.c_str(),
                                           cell.is_number ? ImGui::GetColorU32(ImGuiCol_Text) : imgui_style::get_muted_text_color_u32());
                        break;
                    case audit::column_kind::time:
                    case audit::column_kind::text:
                    default:
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted(cell.text.c_str());
                        break;
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

} // namespace unravel
