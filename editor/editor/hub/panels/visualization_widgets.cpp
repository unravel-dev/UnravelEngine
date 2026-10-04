#include "visualization_widgets.h"

#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>

#include <array>
#include <cstdio>

namespace unravel::visualization_widgets
{
namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size.
constexpr float ROW_HEIGHT = 1.9f;
constexpr float LIST_ROW_HEIGHT = 1.7f;
constexpr float ROW_ROUNDING = 0.4f;
constexpr float ROW_PADDING_X = 0.6f;
constexpr float ROW_ICON_GAP = 0.55f;
constexpr float MARKER_RADIUS = 0.2f;
constexpr float MARKER_GAP = 0.4f;
constexpr float SWITCH_WIDTH = 2.0f;
constexpr float SWITCH_HEIGHT = 1.1f;
constexpr float SWITCH_KNOB_INSET = 0.17f;
constexpr float SECTION_GAP = 0.45f;
constexpr float OPTION_INDENT = 1.2f;
constexpr float OPTION_LABEL_WIDTH = 7.0f;
constexpr float SEGMENT_HEIGHT = 1.75f;
constexpr float SEGMENT_ROUNDING = 0.4f;
constexpr float SEGMENT_INSET = 0.15f;
constexpr float BADGE_PADDING_X = 0.4f;
constexpr float BADGE_PADDING_Y = 0.05f;
constexpr float BADGE_GAP = 0.4f;
/// The knob crosses the switch in this long.
constexpr float SWITCH_SLIDE_SECONDS = 0.1f;
constexpr float SELECTED_FILL_ALPHA = 0.35f;
constexpr float SEGMENT_SELECTED_ALPHA = 0.75f;
constexpr ImU32 ROW_HOVERED_COLOR = IM_COL32(255, 255, 255, 16);
constexpr ImU32 SWITCH_OFF_COLOR = IM_COL32(255, 255, 255, 46);
constexpr ImU32 SWITCH_OFF_HOVERED_COLOR = IM_COL32(255, 255, 255, 70);
constexpr ImU32 SWITCH_KNOB_COLOR = IM_COL32(245, 245, 245, 255);
constexpr ImU32 SEGMENT_BG_COLOR = IM_COL32(255, 255, 255, 12);
constexpr ImU32 SEGMENT_BORDER_COLOR = IM_COL32(255, 255, 255, 20);
constexpr ImU32 SEGMENT_HOVERED_COLOR = IM_COL32(255, 255, 255, 16);
constexpr ImU32 BADGE_COLOR = IM_COL32(255, 255, 255, 40);
constexpr ImU32 FIELD_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 FIELD_HOVERED_COLOR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 FIELD_ACTIVE_COLOR = IM_COL32(255, 255, 255, 30);
/// A combo's arrow button, a shade over its field.
constexpr ImU32 FIELD_BUTTON_COLOR = IM_COL32(255, 255, 255, 20);
constexpr ImU32 FIELD_BUTTON_HOVERED_COLOR = IM_COL32(255, 255, 255, 34);
constexpr ImU32 FIELD_BUTTON_ACTIVE_COLOR = IM_COL32(255, 255, 255, 44);
constexpr int FIELD_STYLE_COLORS = 6;

auto get_accent_color(float alpha) -> ImU32
{
    ImVec4 accent = imgui_style::get_accent_color();
    accent.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(accent);
}

auto get_switch_size() -> ImVec2
{
    return {to_pixels(SWITCH_WIDTH), to_pixels(SWITCH_HEIGHT)};
}

/// The pill and its knob. The knob slides between the ends; where it is lives in the window's state storage.
void draw_switch_shape(ImGuiID item_id, const ImRect& rect, bool is_on, bool is_hovered)
{
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID key = ImHashStr("##switch_position", 0, item_id);
    const float target = is_on ? 1.0f : 0.0f;
    const float step = ImGui::GetIO().DeltaTime / SWITCH_SLIDE_SECONDS;
    float position = storage->GetFloat(key, target);
    position = position < target ? ImMin(position + step, target) : ImMax(position - step, target);
    storage->SetFloat(key, position);
    const ImVec4 off_color = ImGui::ColorConvertU32ToFloat4(is_hovered ? SWITCH_OFF_HOVERED_COLOR : SWITCH_OFF_COLOR);
    const ImVec4 track_color = ImLerp(off_color, imgui_style::get_accent_color(), position);
    const float radius = rect.GetHeight() * 0.5f;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::ColorConvertFloat4ToU32(track_color), radius);
    const float knob_x = ImLerp(rect.Min.x + radius, rect.Max.x - radius, position);
    draw_list->AddCircleFilled(ImVec2(knob_x, rect.GetCenter().y),
                               radius - to_pixels(SWITCH_KNOB_INSET),
                               SWITCH_KNOB_COLOR);
}

/// The text of a segment, centered in it, with its count pill after it.
void draw_segment_label(const ImRect& rect, const char* label, int badge, bool is_selected)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 text_color = is_selected ? ImGui::GetColorU32(ImGuiCol_Text) : imgui_style::get_muted_text_color_u32();
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    std::array<char, 16> badge_text{};
    float badge_width = 0.0f;
    float badge_gap = 0.0f;
    if(badge > 0)
    {
        std::snprintf(badge_text.data(), badge_text.size(), "%d", badge);
        badge_width = ImGui::CalcTextSize(badge_text.data()).x + to_pixels(BADGE_PADDING_X) * 2.0f;
        badge_gap = to_pixels(BADGE_GAP);
    }
    const float x = ImFloor(rect.GetCenter().x - (text_size.x + badge_gap + badge_width) * 0.5f);
    const float y = ImFloor(rect.GetCenter().y - text_size.y * 0.5f);
    draw_list->AddText(ImVec2(x, y), text_color, label);
    if(badge <= 0)
    {
        return;
    }
    const float padding_y = to_pixels(BADGE_PADDING_Y);
    const ImVec2 badge_min(x + text_size.x + badge_gap, y - padding_y);
    const ImVec2 badge_max(badge_min.x + badge_width, y + text_size.y + padding_y);
    draw_list->AddRectFilled(badge_min, badge_max, BADGE_COLOR, (badge_max.y - badge_min.y) * 0.5f);
    draw_list->AddText(ImVec2(badge_min.x + to_pixels(BADGE_PADDING_X), y), text_color, badge_text.data());
}

} // namespace

auto draw_switch(const char* id, bool& value) -> bool
{
    const ImVec2 size = get_switch_size();
    // Centered on a frame's height, as the label beside it is.
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImMax(ImGui::GetFrameHeight() - size.y, 0.0f) * 0.5f);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool is_pressed = ImGui::InvisibleButton(id, size);
    if(is_pressed)
    {
        value = !value;
    }
    draw_switch_shape(ImGui::GetItemID(), ImRect(min, min + size), value, ImGui::IsItemHovered());
    return is_pressed;
}

auto draw_switch_row(const char* label, const char* tooltip, bool& value) -> bool
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, to_pixels(ROW_HEIGHT));
    const bool is_pressed = ImGui::InvisibleButton(label, size);
    const bool is_hovered = ImGui::IsItemHovered();
    const ImGuiID item_id = ImGui::GetItemID();
    if(is_pressed)
    {
        value = !value;
    }
    if(tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", tooltip);
    }
    const ImRect rect(min, min + size);
    if(is_hovered)
    {
        ImGui::GetWindowDrawList()->AddRectFilled(rect.Min, rect.Max, ROW_HOVERED_COLOR, to_pixels(ROW_ROUNDING));
    }
    const float padding = to_pixels(ROW_PADDING_X);
    ImGui::RenderText(ImVec2(rect.Min.x + padding, ImFloor(rect.GetCenter().y - ImGui::GetTextLineHeight() * 0.5f)),
                      label);
    const ImVec2 switch_size = get_switch_size();
    const ImVec2 switch_min(rect.Max.x - padding - switch_size.x, ImFloor(rect.GetCenter().y - switch_size.y * 0.5f));
    draw_switch_shape(item_id, ImRect(switch_min, switch_min + switch_size), value, is_hovered);
    return is_pressed;
}

auto draw_list_row(const char* id, const char* icon, const char* label, bool is_selected, bool has_marker) -> bool
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, to_pixels(LIST_ROW_HEIGHT));
    const bool is_pressed = ImGui::InvisibleButton(id, size);
    const ImRect rect(min, min + size);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float rounding = to_pixels(ROW_ROUNDING);
    if(is_selected)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, get_accent_color(SELECTED_FILL_ALPHA), rounding);
    }
    else if(ImGui::IsItemHovered())
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, ROW_HOVERED_COLOR, rounding);
    }
    const float padding = to_pixels(ROW_PADDING_X);
    float text_x = rect.Min.x + padding;
    if(icon != nullptr)
    {
        const float icon_width = ImGui::GetFontSize();
        const ImU32 icon_color =
            is_selected ? ImGui::GetColorU32(ImGuiCol_Text) : imgui_style::get_muted_text_color_u32();
        ImGui::RenderIconCentered(draw_list, ImVec2(text_x + icon_width * 0.5f, rect.GetCenter().y), icon, icon_color);
        text_x += icon_width + to_pixels(ROW_ICON_GAP);
    }
    float text_max_x = rect.Max.x - padding;
    if(has_marker)
    {
        const float radius = to_pixels(MARKER_RADIUS);
        draw_list->AddCircleFilled(ImVec2(text_max_x - radius, rect.GetCenter().y), radius, get_accent_color(1.0f));
        text_max_x -= radius * 2.0f + to_pixels(MARKER_GAP);
    }
    const ImVec2 text_pos(text_x, ImFloor(rect.GetCenter().y - ImGui::GetTextLineHeight() * 0.5f));
    draw_list->PushClipRect(rect.Min, ImVec2(text_max_x, rect.Max.y), true);
    draw_list->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), label);
    draw_list->PopClipRect();
    return is_pressed;
}

void draw_section_header(const char* label)
{
    ImGui::Dummy(ImVec2(0.0f, to_pixels(SECTION_GAP)));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + to_pixels(ROW_PADDING_X));
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color());
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void draw_option_label(const char* label, const char* tooltip)
{
    const float start_x = ImGui::GetCursorPosX() + to_pixels(OPTION_INDENT);
    ImGui::SetCursorPosX(start_x);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color());
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if(tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", tooltip);
    }
    ImGui::SameLine(start_x + to_pixels(OPTION_LABEL_WIDTH));
    ImGui::SetNextItemWidth(-to_pixels(ROW_PADDING_X));
}

auto draw_segmented(const char* id, const char* const* labels, const int* badges, int count, float width, int& index)
    -> bool
{
    if(count <= 0)
    {
        return false;
    }
    ImGui::PushID(id);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, to_pixels(SEGMENT_HEIGHT));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float rounding = to_pixels(SEGMENT_ROUNDING);
    draw_list->AddRectFilled(min, min + size, SEGMENT_BG_COLOR, rounding);
    draw_list->AddRect(min, min + size, SEGMENT_BORDER_COLOR, rounding);
    const float inset = to_pixels(SEGMENT_INSET);
    const float segment_width = (size.x - inset * 2.0f) / float(count);
    const float segment_rounding = ImMax(rounding - inset, 0.0f);
    bool is_changed = false;
    for(int i = 0; i < count; ++i)
    {
        const ImVec2 segment_min(ImFloor(min.x + inset + segment_width * float(i)), min.y + inset);
        const ImVec2 segment_max(ImFloor(segment_min.x + segment_width), min.y + size.y - inset);
        ImGui::SetCursorScreenPos(segment_min);
        ImGui::PushID(i);
        if(ImGui::InvisibleButton("##segment", segment_max - segment_min) && index != i)
        {
            index = i;
            is_changed = true;
        }
        const bool is_hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool is_selected = index == i;
        if(is_selected)
        {
            draw_list->AddRectFilled(segment_min, segment_max, get_accent_color(SEGMENT_SELECTED_ALPHA), segment_rounding);
        }
        else if(is_hovered)
        {
            draw_list->AddRectFilled(segment_min, segment_max, SEGMENT_HOVERED_COLOR, segment_rounding);
        }
        draw_segment_label(ImRect(segment_min, segment_max), labels[i], badges != nullptr ? badges[i] : 0, is_selected);
    }
    // The control as one item of its size, for the layout after it.
    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(size);
    ImGui::PopID();
    return is_changed;
}

void push_field_style()
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, FIELD_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, FIELD_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, FIELD_ACTIVE_COLOR);
    ImGui::PushStyleColor(ImGuiCol_Button, FIELD_BUTTON_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, FIELD_BUTTON_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, FIELD_BUTTON_ACTIVE_COLOR);
}

void pop_field_style()
{
    ImGui::PopStyleColor(FIELD_STYLE_COLORS);
}

} // namespace unravel::visualization_widgets
