#include "settings_view.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/utils.h>

#include <algorithm>
#include <iterator>
#include <utility>

namespace unravel
{
namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size.
constexpr float SETTINGS_SIDEBAR_WIDTH = 12.5f;
// The sidebar never takes more than this share of the window.
constexpr float SETTINGS_SIDEBAR_MAX_SHARE = 0.4f;
constexpr float SETTINGS_SIDEBAR_PADDING = 0.5f;
constexpr float SETTINGS_SIDEBAR_SEARCH_GAP = 0.4f;
constexpr float SETTINGS_PANE_ROUNDING = 0.5f;
constexpr float SETTINGS_COLUMN_GAP = 0.25f;
constexpr float SETTINGS_ROW_HEIGHT = 1.9f;
constexpr float SETTINGS_ROW_ROUNDING = 0.4f;
constexpr float SETTINGS_ROW_PADDING_X = 0.6f;
constexpr float SETTINGS_ROW_ICON_GAP = 0.55f;
constexpr float SETTINGS_PAGE_PADDING_X = 1.2f;
constexpr float SETTINGS_PAGE_PADDING_Y = 0.9f;
constexpr float SETTINGS_TITLE_SCALE = 1.3f;
constexpr float SETTINGS_TITLE_ICON_GAP = 0.45f;
constexpr float SETTINGS_HEADER_GAP = 0.6f;
constexpr float SETTINGS_SELECTED_FILL_ALPHA = 0.35f;
constexpr float SETTINGS_MUTED_ALPHA = 0.55f;
constexpr ImU32 SETTINGS_SIDEBAR_COLOR = IM_COL32(0, 0, 0, 38);
constexpr ImU32 SETTINGS_ROW_HOVERED_COLOR = IM_COL32(255, 255, 255, 16);
// The theme's fields are darker than a window; on the darker sidebar they would vanish, so the
// search field is washed light, as on the start page.
constexpr ImU32 SETTINGS_FIELD_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 SETTINGS_FIELD_HOVERED_COLOR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 SETTINGS_FIELD_ACTIVE_COLOR = IM_COL32(255, 255, 255, 30);

auto get_muted_text_color() -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Text, SETTINGS_MUTED_ALPHA);
}

auto get_accent_color(float alpha = 1.0f) -> ImU32
{
    ImVec4 accent = imgui_style::get_accent_color();
    accent.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(accent);
}

/// The accent icon and the title of a page on one line, the description under them, then a line.
void draw_page_header(const settings_category& category)
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushWindowFontScale(SETTINGS_TITLE_SCALE);
    const ImVec2 title_min = ImGui::GetCursorScreenPos();
    const float icon_width = ImGui::GetFontSize();
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                              ImVec2(title_min.x + icon_width * 0.5f, title_min.y + ImGui::GetTextLineHeight() * 0.5f),
                              category.icon,
                              get_accent_color());
    ImGui::SetCursorScreenPos(ImVec2(title_min.x + icon_width + to_pixels(SETTINGS_TITLE_ICON_GAP), title_min.y));
    ImGui::TextUnformatted(category.name.c_str());
    ImGui::PopWindowFontScale();
    ImGui::PopFont();

    if(!category.description.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, get_muted_text_color());
        ImGui::TextWrapped("%s", category.description.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0.0f, to_pixels(SETTINGS_HEADER_GAP)));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, to_pixels(SETTINGS_HEADER_GAP)));
}
} // namespace

settings_view::settings_view(std::vector<settings_category> categories) : categories_(std::move(categories))
{
}

void settings_view::select(const std::string& name)
{
    const auto it = std::find_if(categories_.begin(),
                                 categories_.end(),
                                 [&](const settings_category& category)
                                 {
                                     return category.name == name;
                                 });
    if(it != categories_.end())
    {
        selected_ = static_cast<std::size_t>(std::distance(categories_.begin(), it));
    }
}

void settings_view::draw(rtti::context& ctx)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if(avail.x < 1.0f || avail.y < 1.0f || categories_.empty())
    {
        return;
    }
    draw_sidebar();
    ImGui::SameLine(0.0f, to_pixels(SETTINGS_COLUMN_GAP));
    draw_page(ctx);
}

auto settings_view::is_match(const settings_category& category) const -> bool
{
    if(!filter_.IsActive())
    {
        return true;
    }
    const std::string searched_text = category.name + " " + category.keywords;
    return filter_.PassFilter(searched_text.c_str());
}

void settings_view::draw_sidebar()
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 size(ImMin(to_pixels(SETTINGS_SIDEBAR_WIDTH), avail.x * SETTINGS_SIDEBAR_MAX_SHARE), avail.y);
    const float padding = to_pixels(SETTINGS_SIDEBAR_PADDING);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, SETTINGS_SIDEBAR_COLOR);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, to_pixels(SETTINGS_PANE_ROUNDING));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    const bool is_visible = ImGui::BeginChild("##settings_sidebar", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    // Only the child itself: what opens from it keeps the theme's look.
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if(is_visible)
    {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, SETTINGS_FIELD_COLOR);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, SETTINGS_FIELD_HOVERED_COLOR);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, SETTINGS_FIELD_ACTIVE_COLOR);
        ImGui::DrawFilterWithHint(filter_, ICON_MDI_MAGNIFY " Search", ImGui::GetContentRegionAvail().x);
        ImGui::PopStyleColor(3);
        ImGui::Dummy(ImVec2(0.0f, to_pixels(SETTINGS_SIDEBAR_SEARCH_GAP)));

        // While searching, the page follows the matches: a selected category the search hides
        // gives way to the first one it shows.
        if(!is_match(categories_[selected_]))
        {
            const auto first_match = std::find_if(categories_.begin(),
                                                  categories_.end(),
                                                  [&](const settings_category& category)
                                                  {
                                                      return is_match(category);
                                                  });
            if(first_match != categories_.end())
            {
                selected_ = static_cast<std::size_t>(std::distance(categories_.begin(), first_match));
            }
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        bool has_match = false;
        for(std::size_t i = 0; i < categories_.size(); ++i)
        {
            if(is_match(categories_[i]))
            {
                draw_category_row(i);
                has_match = true;
            }
        }
        ImGui::PopStyleVar();
        if(!has_match)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, get_muted_text_color());
            ImGui::TextUnformatted("No results");
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
}

void settings_view::draw_category_row(std::size_t index)
{
    const settings_category& category = categories_[index];
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const ImVec2 row_size(ImGui::GetContentRegionAvail().x, to_pixels(SETTINGS_ROW_HEIGHT));
    ImGui::PushID(static_cast<int>(index));
    if(ImGui::InvisibleButton("##category", row_size))
    {
        selected_ = index;
    }
    ImGui::PopID();

    const bool is_selected = index == selected_;
    const ImRect rect(row_min, row_min + row_size);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float rounding = to_pixels(SETTINGS_ROW_ROUNDING);
    if(is_selected)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, get_accent_color(SETTINGS_SELECTED_FILL_ALPHA), rounding);
    }
    else if(ImGui::IsItemHovered())
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, SETTINGS_ROW_HOVERED_COLOR, rounding);
    }

    const float icon_width = ImGui::GetFontSize();
    const float icon_x = rect.Min.x + to_pixels(SETTINGS_ROW_PADDING_X);
    const ImU32 icon_color = is_selected ? ImGui::GetColorU32(ImGuiCol_Text) : get_muted_text_color();
    ImGui::RenderIconCentered(draw_list, ImVec2(icon_x + icon_width * 0.5f, rect.GetCenter().y), category.icon, icon_color);
    const ImVec2 text_pos(icon_x + icon_width + to_pixels(SETTINGS_ROW_ICON_GAP),
                          ImFloor(rect.GetCenter().y - ImGui::GetTextLineHeight() * 0.5f));
    draw_list->PushClipRect(rect.Min, rect.Max, true);
    draw_list->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), category.name.c_str());
    draw_list->PopClipRect();
}

void settings_view::draw_page(rtti::context& ctx)
{
    selected_ = std::min(selected_, categories_.size() - 1);
    const settings_category& category = categories_[selected_];
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(to_pixels(SETTINGS_PAGE_PADDING_X), to_pixels(SETTINGS_PAGE_PADDING_Y)));
    const bool is_visible =
        ImGui::BeginChild("##settings_page", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if(is_visible)
    {
        draw_page_header(category);
        // The header stays while the settings scroll under it.
        if(ImGui::BeginChild("##settings_page_content"))
        {
            ImGui::PushID(category.name.c_str());
            if(category.draw)
            {
                category.draw(ctx);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

} // namespace unravel
