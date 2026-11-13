#include "packages_widgets.h"

#include "../panel_toolbar.h"

#include <editor/assets/asset_icons.h>
#include <editor/format/format_bytes.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>

#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <logging/logging.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <numeric>

namespace unravel::packages_widgets
{
namespace
{
constexpr float TILE_ROUNDING = 0.32f;
constexpr float TILE_GLYPH_SCALE = 0.55f;
constexpr float TILE_SATURATION = 0.45f;
constexpr float TILE_VALUE = 0.62f;
constexpr float PILL_PADDING_X = 0.45f;
constexpr float PILL_PADDING_Y = 0.12f;
constexpr float PILL_TEXT_SCALE = 0.85f;
constexpr int PILL_FILL_ALPHA = 38;
constexpr float COUNTER_HEIGHT = 2.9f;
constexpr float COUNTER_GAP = 0.5f;
constexpr float COUNTER_PADDING = 0.6f;
constexpr float COUNTER_ROUNDING = 0.35f;
constexpr float COUNTER_VALUE_SCALE = 1.25f;
constexpr float COUNTER_LABEL_SCALE = 0.85f;
constexpr ImU32 COUNTER_COLOR = IM_COL32(255, 255, 255, 10);
constexpr ImU32 ROW_STRIPE_COLOR = IM_COL32(255, 255, 255, 7);
constexpr float ROW_HOVERED_ALPHA = 0.45f;
constexpr float ROW_SELECTED_ALPHA = 0.22f;
constexpr float ROW_EDGE_WIDTH = 0.18f;
constexpr ImU32 COUNTER_HOVERED_COLOR = IM_COL32(255, 255, 255, 20);
constexpr int FILTER_ACTIVE_FILL_ALPHA = 34;
constexpr float FILTER_BORDER_WIDTH = 0.09f;
/// How wide the description of a dropdown's choice runs before it wraps.
constexpr float CHOICE_WRAP_WIDTH = 22.0f;
constexpr float FILE_INDENT = 1.2f;
constexpr float FILE_ICON_GAP = 0.45f;
constexpr float FILE_SIZE_COLUMN = 5.5f;
constexpr float FILE_COLUMN_GAP = 0.6f;
constexpr float FOLDER_ARROW_SCALE = 0.7f;
constexpr ImU32 FOLDER_ICON_COLOR = IM_COL32(214, 178, 104, 255);
constexpr ImU32 FOLDER_ROW_COLOR = IM_COL32(255, 255, 255, 12);
constexpr float MUTED_ICON_ALPHA = 0.45f;
/// The height an empty list keeps for its hint, in rows.
constexpr int EMPTY_LIST_ROWS = 3;
constexpr const char* DATA_FOLDER_LABEL = "(data folder)";
constexpr const char* COLLAPSE_ALL_LABEL = ICON_MDI_COLLAPSE_ALL_OUTLINE " Collapse All";
constexpr const char* EXPAND_ALL_LABEL = ICON_MDI_EXPAND_ALL_OUTLINE " Expand All";

/// A folder of a file list and what its shown files add up to.
struct folder_group
{
    hpp::string_view folder;
    std::size_t file_count{};
    std::uint64_t size{};
    bool has_size{};
};

/// A line of a file list as drawn: a folder's header or a file.
struct list_line
{
    bool is_folder{};
    /// The folder's index in the groups, or the file's index in the rows.
    std::size_t index{};
};

auto get_scaled_font_size(float scale) -> float
{
    return ImGui::GetCurrentContext()->FontSizeBase * scale;
}

auto with_alpha(ImU32 color, int alpha) -> ImU32
{
    return (color & ~IM_COL32_A_MASK) | (ImU32(alpha) << IM_COL32_A_SHIFT);
}

/// The counter's value and label inside its box, clipped to it.
void draw_counter_text(ImDrawList* draw_list, const ImVec2& min, const ImVec2& size, const char* label, const std::string& value, ImU32 value_color)
{
    const float font_size = ImGui::GetFontSize();
    const float padding = font_size * COUNTER_PADDING;
    draw_list->PushClipRect(min, ImVec2(min.x + size.x, min.y + size.y), true);
    draw_list->AddText(ImGui::GetFont(ImGui::Font::Bold),
                       font_size * COUNTER_VALUE_SCALE,
                       ImVec2(min.x + padding, min.y + padding * 0.6f),
                       value_color,
                       value.c_str());
    draw_list->AddText(ImGui::GetFont(),
                       font_size * COUNTER_LABEL_SCALE,
                       ImVec2(min.x + padding, min.y + size.y - padding * 0.6f - font_size * COUNTER_LABEL_SCALE),
                       imgui_style::get_muted_text_color_u32(),
                       label);
    draw_list->PopClipRect();
}

auto get_folder(hpp::string_view path) -> hpp::string_view
{
    const auto slash = path.find_last_of('/');
    return slash == hpp::string_view::npos ? hpp::string_view{} : path.substr(0, slash);
}

/// The rows grouped by folder, the folders sorted and the rows of a closed folder left out.
auto build_lines(const std::vector<file_list_row>& rows, const file_list_state& state, std::vector<folder_group>& groups)
    -> std::vector<list_line>
{
    std::vector<std::size_t> order(rows.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(),
                     order.end(),
                     [&rows](std::size_t lhs, std::size_t rhs)
                     {
                         return get_folder(rows[lhs].path) < get_folder(rows[rhs].path);
                     });
    std::vector<list_line> lines;
    for(const std::size_t index : order)
    {
        const auto folder = get_folder(rows[index].path);
        if(groups.empty() || groups.back().folder != folder)
        {
            groups.push_back({folder});
            lines.push_back({true, groups.size() - 1});
        }
        auto& group = groups.back();
        ++group.file_count;
        if(rows[index].size)
        {
            group.size += *rows[index].size;
            group.has_size = true;
        }
        if(!state.is_collapsed(folder))
        {
            lines.push_back({false, index});
        }
    }
    return lines;
}

auto describe_folder(const folder_group& group) -> std::string
{
    const char* noun = group.file_count == 1 ? "file" : "files";
    return group.has_size ? fmt::format("{} {}, {}", group.file_count, noun, format_bytes(group.size, 1))
                          : fmt::format("{} {}", group.file_count, noun);
}

void draw_folder_line(const folder_group& group, file_list_state& state)
{
    const float font_size = ImGui::GetFontSize();
    const float row_height = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect rect(min, ImVec2(min.x + ImMax(1.0f, ImGui::GetContentRegionAvail().x), min.y + row_height));
    ImGui::PushID(group.folder.data(), group.folder.data() + group.folder.size());
    if(ImGui::InvisibleButton("##folder", rect.GetSize()))
    {
        state.toggle(group.folder);
    }
    const bool is_hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(rect.Min, rect.Max, is_hovered ? ImGui::GetColorU32(ImGuiCol_HeaderHovered, ROW_HOVERED_ALPHA) : FOLDER_ROW_COLOR);
    const bool is_open = !state.is_collapsed(group.folder);
    const float padding = font_size * ROW_PADDING_X;
    float x = rect.Min.x + padding;
    ImGui::RenderArrow(draw_list,
                       ImVec2(x + font_size * (1.0f - FOLDER_ARROW_SCALE) * 0.5f, rect.Min.y + (row_height - font_size * FOLDER_ARROW_SCALE) * 0.5f),
                       ImGui::GetColorU32(ImGuiCol_Text),
                       is_open ? ImGuiDir_Down : ImGuiDir_Right,
                       FOLDER_ARROW_SCALE);
    x += font_size;
    ImGui::RenderIconCentered(draw_list, ImVec2(x + font_size * 0.5f, rect.Min.y + row_height * 0.5f), is_open ? ICON_MDI_FOLDER_OPEN : ICON_MDI_FOLDER, FOLDER_ICON_COLOR);
    x += font_size + to_pixels(FILE_ICON_GAP);
    const float text_y = ImFloor(rect.Min.y + (row_height - font_size) * 0.5f);
    const auto summary = describe_folder(group);
    const float summary_width = ImGui::CalcTextSize(summary.c_str()).x;
    draw_list->AddText(ImVec2(rect.Max.x - padding - summary_width, text_y), imgui_style::get_muted_text_color_u32(), summary.c_str());
    const std::string folder = group.folder.empty() ? std::string(DATA_FOLDER_LABEL) : std::string(group.folder);
    draw_path_text(draw_list, ImVec2(x, text_y), rect.Max.x - padding * 2.0f - summary_width, folder, ImGui::GetColorU32(ImGuiCol_Text));
}

void draw_file_line(const std::vector<file_list_row>& rows, std::size_t index, const file_list_parts& parts, int line)
{
    const auto& row = rows[index];
    const float font_size = ImGui::GetFontSize();
    const float row_height = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect rect(min, ImVec2(min.x + ImGui::GetContentRegionAvail().x, min.y + row_height));
    ImGui::PushID(int(index));
    draw_row_background(rect, line, ImGui::IsMouseHoveringRect(rect.Min, rect.Max) && ImGui::IsWindowHovered(), false);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float padding = font_size * ROW_PADDING_X;
    const float gap = font_size * FILE_COLUMN_GAP;
    float x = rect.Min.x + padding + to_pixels(FILE_INDENT);
    if(parts.draw_leading)
    {
        ImGui::SetCursorScreenPos(ImVec2(x, rect.Min.y));
        parts.draw_leading(index);
        x += font_size * parts.leading_width;
    }
    // The columns from the right end: the trailing part, the label, the size; the name takes what is left.
    const float trailing_x = rect.Max.x - padding - font_size * parts.trailing_width;
    const float label_x = parts.label_width > 0.0f ? trailing_x - gap - font_size * parts.label_width : trailing_x;
    const float size_max_x = label_x - gap;
    const float name_max_x = size_max_x - font_size * FILE_SIZE_COLUMN;
    const float icon_x = x;
    const float text_y = ImFloor(rect.Min.y + (row_height - font_size) * 0.5f);
    const auto slash = row.path.find_last_of('/');
    const char* name = row.path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
    const char* dot = std::strrchr(name, '.');
    const ImU32 icon_color = imgui_style::get_muted_text_color_u32();
    ImGui::RenderIconCentered(draw_list,
                              ImVec2(x + font_size * 0.5f, rect.Min.y + row_height * 0.5f),
                              get_asset_icon_for_extension(dot != nullptr ? std::string(dot) : std::string{}),
                              row.is_muted ? ImGui::GetColorU32(icon_color, MUTED_ICON_ALPHA) : icon_color);
    x += font_size + to_pixels(FILE_ICON_GAP);
    ImGui::PushStyleColor(ImGuiCol_Text, row.is_muted ? imgui_style::get_muted_text_color_u32() : ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::RenderTextEllipsis(draw_list, ImVec2(x, text_y), ImVec2(name_max_x, text_y + font_size), name_max_x, name, nullptr, nullptr);
    ImGui::PopStyleColor();
    if(row.size)
    {
        const auto size_text = format_bytes(*row.size, 1);
        const float size_width = ImGui::CalcTextSize(size_text.c_str()).x;
        draw_list->AddText(ImVec2(size_max_x - size_width, text_y), imgui_style::get_muted_text_color_u32(), size_text.c_str());
    }
    if(row.label != nullptr && parts.label_width > 0.0f)
    {
        const ImVec2 pill_size = calc_pill_size(row.label);
        draw_pill(draw_list, ImVec2(label_x, rect.Min.y + (row_height - pill_size.y) * 0.5f), row.label, row.label_color);
    }
    // The name, the size and the label share one tooltip.
    ImGui::SetCursorScreenPos(ImVec2(icon_x, rect.Min.y));
    ImGui::InvisibleButton("##name", ImVec2(ImMax(1.0f, trailing_x - icon_x), row_height));
    ImGui::ItemTooltipEx(
        [&]()
        {
            if(parts.draw_tooltip)
            {
                parts.draw_tooltip(index);
                return;
            }
            ImGui::TextUnformatted(row.path.c_str());
        });
    if(parts.draw_trailing)
    {
        ImGui::SetCursorScreenPos(ImVec2(trailing_x, rect.Min.y));
        parts.draw_trailing(index);
    }
    ImGui::SetCursorScreenPos(ImVec2(rect.Min.x, rect.Max.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
}
} // namespace

auto to_pixels(float font_units) -> float
{
    return screen_card::to_pixels(font_units);
}

auto get_package_color(const std::string& name) -> ImU32
{
    const auto hash = std::hash<std::string>{}(name);
    const float hue = float(hash % 360) / 360.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, TILE_SATURATION, TILE_VALUE, red, green, blue);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(red, green, blue, 1.0f));
}

void draw_package_tile(ImDrawList* draw_list, const ImVec2& min, float size, const std::string& name, const char* icon)
{
    const ImVec2 max(min.x + size, min.y + size);
    draw_list->AddRectFilled(min, max, get_package_color(name), size * TILE_ROUNDING);
    const float glyph_size = size * TILE_GLYPH_SCALE;
    ImGui::PushFont(ImGui::GetFont(ImGui::Font::Regular), glyph_size);
    ImGui::RenderIconCentered(draw_list,
                              ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
                              icon != nullptr ? icon : ICON_MDI_PACKAGE_VARIANT_CLOSED,
                              IM_COL32(255, 255, 255, 235));
    ImGui::PopFont();
}

auto calc_pill_size(const char* text) -> ImVec2
{
    const float font_size = ImGui::GetFontSize();
    const float text_size = font_size * PILL_TEXT_SCALE;
    const ImVec2 text_extent = ImGui::GetFont()->CalcTextSizeA(text_size, FLT_MAX, 0.0f, text);
    return ImVec2(text_extent.x + 2.0f * font_size * PILL_PADDING_X, text_extent.y + 2.0f * font_size * PILL_PADDING_Y);
}

auto draw_pill(ImDrawList* draw_list, const ImVec2& min, const char* text, ImU32 color) -> ImVec2
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 size = calc_pill_size(text);
    const ImVec2 max(min.x + size.x, min.y + size.y);
    draw_list->AddRectFilled(min, max, with_alpha(color, PILL_FILL_ALPHA), size.y * 0.5f);
    draw_list->AddText(ImGui::GetFont(),
                       font_size * PILL_TEXT_SCALE,
                       ImVec2(min.x + font_size * PILL_PADDING_X, min.y + font_size * PILL_PADDING_Y),
                       color,
                       text);
    return size;
}

void pill_item(const char* text, ImU32 color, const char* tooltip)
{
    const ImVec2 size = calc_pill_size(text);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float offset_y = ImMax(0.0f, (ImGui::GetFrameHeight() - size.y) * 0.5f);
    draw_pill(ImGui::GetWindowDrawList(), ImVec2(min.x, min.y + offset_y), text, color);
    ImGui::Dummy(ImVec2(size.x, ImMax(size.y, ImGui::GetFrameHeight())));
    if(tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", tooltip);
    }
}

auto begin_card(const char* id) -> bool
{
    const float padding = to_pixels(CARD_PADDING);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, to_pixels(CARD_ROUNDING));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, CARD_COLOR);
    const bool is_open = ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    // Popped right away: tooltips and popups opened from the card must not inherit the card's padding.
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return is_open;
}

void end_card()
{
    ImGui::EndChild();
}

void draw_tile(const char* label, const std::string& value, ImU32 value_color, float width)
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, font_size * COUNTER_HEIGHT);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), COUNTER_COLOR, font_size * COUNTER_ROUNDING);
    draw_counter_text(draw_list, min, size, label, value, value_color);
    ImGui::Dummy(size);
}

auto calc_tile_width(int count) -> float
{
    const float gap = ImGui::GetFontSize() * COUNTER_GAP;
    return ImFloor((ImGui::GetContentRegionAvail().x - gap * float(count - 1)) / float(count));
}

auto draw_filter_tile(const char* id, const filter_tile& tile, float width) -> bool
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImMax(1.0f, width), font_size * COUNTER_HEIGHT);
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const bool is_clickable = tile.count > 0 || tile.is_active;
    const bool is_pressed = ImGui::InvisibleButton(id, size) && is_clickable;
    const bool is_hovered = is_clickable && ImGui::IsItemHovered();
    if(tile.tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", tile.tooltip);
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float rounding = font_size * COUNTER_ROUNDING;
    if(tile.is_active)
    {
        draw_list->AddRectFilled(min, max, with_alpha(tile.color, FILTER_ACTIVE_FILL_ALPHA), rounding);
        draw_list->AddRect(min, max, tile.color, rounding, ImDrawFlags_None, ImMax(1.0f, font_size * FILTER_BORDER_WIDTH));
    }
    else
    {
        draw_list->AddRectFilled(min, max, is_hovered ? COUNTER_HOVERED_COLOR : COUNTER_COLOR, rounding);
    }
    const ImU32 value_color = tile.count > 0 ? tile.color : imgui_style::get_muted_text_color_u32();
    draw_counter_text(draw_list, min, size, tile.label, std::to_string(tile.count), value_color);
    return is_pressed;
}

auto begin_row_choice(const char* id, const char* preview, float width) -> bool
{
    // No frame and no arrow fill until hovered: the hover colors stay, so the dropdown shows itself under the mouse.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32_BLACK_TRANS);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK_TRANS);
    ImGui::SetNextItemWidth(width);
    // The choices carry a description each: a popup sized for one-line items would scroll.
    const bool is_open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLargest);
    ImGui::PopStyleColor(2);
    return is_open;
}

auto draw_choice(const char* name, const char* description, bool is_selected) -> bool
{
    const float wrap_width = to_pixels(CHOICE_WRAP_WIDTH);
    const ImVec2 name_size = ImGui::CalcTextSize(name);
    const ImVec2 description_size = ImGui::CalcTextSize(description, nullptr, false, wrap_width);
    const float line_gap = ImGui::GetStyle().ItemInnerSpacing.y;
    const float padding_y = ImGui::GetStyle().FramePadding.y;
    const ImVec2 size(ImMax(name_size.x, description_size.x), padding_y * 2.0f + name_size.y + line_gap + description_size.y);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::PushID(name);
    const bool is_pressed = ImGui::Selectable("##choice", is_selected, ImGuiSelectableFlags_None, size);
    ImGui::PopID();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddText(ImVec2(min.x, min.y + padding_y), ImGui::GetColorU32(ImGuiCol_Text), name);
    draw_list->AddText(ImGui::GetFont(),
                       ImGui::GetFontSize(),
                       ImVec2(min.x, min.y + padding_y + name_size.y + line_gap),
                       imgui_style::get_muted_text_color_u32(),
                       description,
                       nullptr,
                       wrap_width);
    return is_pressed;
}

void draw_title(const char* text)
{
    ImGui::PushFont(ImGui::GetFont(ImGui::Font::Bold), get_scaled_font_size(TITLE_SCALE));
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void draw_muted_wrapped(const char* text)
{
    // TextWrapped keeps a wrap width set around it, as a tooltip's; it wraps at the window's edge only without one.
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void draw_row_background(const ImRect& rect, int row_index, bool is_hovered, bool is_selected)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if(is_selected)
    {
        const ImVec4 accent = imgui_style::get_accent_color();
        draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, ROW_SELECTED_ALPHA)));
        draw_list->AddRectFilled(rect.Min,
                                 ImVec2(rect.Min.x + ImGui::GetFontSize() * ROW_EDGE_WIDTH, rect.Max.y),
                                 ImGui::ColorConvertFloat4ToU32(accent));
        return;
    }
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

void draw_centered_hint(const char* title, const char* detail)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float line_height = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 origin = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(origin.x + ImMax(0.0f, (avail.x - ImGui::CalcTextSize(title).x) * 0.5f),
                               origin.y + ImMax(0.0f, avail.y * 0.5f - line_height)));
    ImGui::TextUnformatted(title);
    ImGui::SetCursorPosX(origin.x + ImMax(0.0f, (avail.x - ImGui::CalcTextSize(detail).x) * 0.5f));
    ImGui::TextDisabled("%s", detail);
}

void draw_path_text(ImDrawList* draw_list, const ImVec2& pos, float max_x, const std::string& path, ImU32 name_color)
{
    const auto slash = path.find_last_of('/');
    const std::string folder = slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const ImVec4 clip(pos.x, pos.y - 1.0f, max_x, pos.y + ImGui::GetFontSize() + 1.0f);
    draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, imgui_style::get_muted_text_color_u32(), folder.c_str(), nullptr, 0.0f, &clip);
    const float folder_width = ImGui::CalcTextSize(folder.c_str()).x;
    draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + folder_width, pos.y), name_color, name.c_str(), nullptr, 0.0f, &clip);
}

auto file_list_state::is_collapsed(hpp::string_view folder) const -> bool
{
    return collapsed_.find(folder) != collapsed_.end();
}

void file_list_state::toggle(hpp::string_view folder)
{
    const auto it = collapsed_.find(folder);
    if(it != collapsed_.end())
    {
        collapsed_.erase(it);
        return;
    }
    collapsed_.emplace(folder);
}

auto file_list_state::is_all_collapsed() const -> bool
{
    return !folders_.empty() && std::all_of(folders_.begin(),
                                            folders_.end(),
                                            [this](const std::string& folder)
                                            {
                                                return is_collapsed(folder);
                                            });
}

void file_list_state::toggle_all()
{
    if(is_all_collapsed())
    {
        collapsed_.clear();
        return;
    }
    collapsed_.insert(folders_.begin(), folders_.end());
}

void file_list_state::set_folders(std::vector<std::string> folders)
{
    folders_ = std::move(folders);
}

void file_list_state::clear()
{
    collapsed_.clear();
    folders_.clear();
}

void draw_collapse_button(const char* id, file_list_state& state)
{
    const bool is_all_collapsed = state.is_all_collapsed();
    if(panel_toolbar::button(id,
                             is_all_collapsed ? EXPAND_ALL_LABEL : COLLAPSE_ALL_LABEL,
                             is_all_collapsed ? "Open every folder of the list." : "Close every folder of the list, to see the folders alone.",
                             false,
                             COLLAPSE_ALL_LABEL))
    {
        state.toggle_all();
    }
}

void draw_file_list(const char* id,
                    const std::vector<file_list_row>& rows,
                    file_list_state& state,
                    const file_list_parts& parts,
                    float max_height,
                    const char* empty_hint)
{
    std::vector<folder_group> groups;
    const auto lines = build_lines(rows, state, groups);
    std::vector<std::string> folders;
    folders.reserve(groups.size());
    for(const auto& group : groups)
    {
        folders.emplace_back(group.folder);
    }
    state.set_folders(std::move(folders));
    const float row_height = ImGui::GetFrameHeight();
    const float wanted = row_height * float(lines.empty() ? std::size_t(EMPTY_LIST_ROWS) : lines.size()) + ImGui::GetStyle().WindowPadding.y;
    if(ImGui::BeginChild(id, ImVec2(0.0f, ImMin(wanted, max_height))))
    {
        if(lines.empty())
        {
            draw_centered_hint("Nothing to show", empty_hint);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGuiListClipper clipper;
        clipper.Begin(int(lines.size()), row_height);
        while(clipper.Step())
        {
            for(int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line)
            {
                const auto& entry = lines[std::size_t(line)];
                if(entry.is_folder)
                {
                    draw_folder_line(groups[entry.index], state);
                }
                else
                {
                    draw_file_line(rows, entry.index, parts, line);
                }
            }
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

} // namespace unravel::packages_widgets
