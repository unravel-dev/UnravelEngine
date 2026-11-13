#include "packages_widgets.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>

#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>

#include <functional>

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

auto get_scaled_font_size(float scale) -> float
{
    return ImGui::GetCurrentContext()->FontSizeBase * scale;
}

auto with_alpha(ImU32 color, int alpha) -> ImU32
{
    return (color & ~IM_COL32_A_MASK) | (ImU32(alpha) << IM_COL32_A_SHIFT);
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
    ImGui::Dummy(size);
}

auto calc_tile_width(int count) -> float
{
    const float gap = ImGui::GetFontSize() * COUNTER_GAP;
    return ImFloor((ImGui::GetContentRegionAvail().x - gap * float(count - 1)) / float(count));
}

void draw_title(const char* text)
{
    ImGui::PushFont(ImGui::GetFont(ImGui::Font::Bold), get_scaled_font_size(TITLE_SCALE));
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void draw_muted_wrapped(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
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

} // namespace unravel::packages_widgets
