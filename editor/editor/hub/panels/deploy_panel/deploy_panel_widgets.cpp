#include "deploy_panel_widgets.h"

#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>

#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>

namespace unravel
{
namespace deploy_widgets
{
namespace
{
constexpr float BUTTON_HEIGHT = 2.2f;
constexpr float ACCENT_ACTIVE_SHADE = 0.85f;
constexpr ImU32 FIELD_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 FIELD_HOVERED_COLOR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 FIELD_ACTIVE_COLOR = IM_COL32(255, 255, 255, 30);
constexpr int FIELD_STYLE_COLORS = 3;
} // namespace

auto to_pixels(float font_units) -> float
{
    return screen_card::to_pixels(font_units);
}

auto get_accent_u32() -> ImU32
{
    return ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color());
}

auto get_muted_color() -> ImU32
{
    return imgui_style::get_muted_text_color_u32();
}

void draw_muted_text(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void draw_gap(float font_units)
{
    ImGui::Dummy(ImVec2(0.0f, to_pixels(font_units)));
}

void draw_icon_line(const char* icon, ImU32 icon_color, const char* text, ImU32 text_color)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float icon_width = ImGui::GetFontSize();
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                              ImVec2(min.x + icon_width * 0.5f, min.y + ImGui::GetTextLineHeight() * 0.5f),
                              icon,
                              icon_color);
    ImGui::SetCursorScreenPos(ImVec2(min.x + icon_width + to_pixels(ICON_GAP), min.y));
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void draw_section_title(const char* title)
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    draw_gap(LINE_GAP);
}

auto calc_button_width(const char* label) -> float
{
    return ImGui::CalcTextSize(label, nullptr, true).x + 2.0f * to_pixels(BUTTON_PADDING_X);
}

auto draw_section_title_button(const char* title, const char* label, const char* tooltip, bool is_enabled) -> bool
{
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::SameLine();
    const float width = calc_button_width(label);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - width));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(BUTTON_ROUNDING));
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(label, ImVec2(width, 0.0f));
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::SetItemTooltipEx("%s", tooltip);
    draw_gap(LINE_GAP);
    return is_pressed;
}

auto draw_button(const char* label, bool is_primary, bool is_enabled) -> bool
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(BUTTON_ROUNDING));
    if(is_primary)
    {
        const ImVec4 accent = imgui_style::get_accent_color();
        const ImVec4 active(accent.x * ACCENT_ACTIVE_SHADE, accent.y * ACCENT_ACTIVE_SHADE, accent.z * ACCENT_ACTIVE_SHADE, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    }
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(label, ImVec2(calc_button_width(label), to_pixels(BUTTON_HEIGHT)));
    ImGui::EndDisabled();
    if(is_primary)
    {
        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar();
    return is_pressed;
}

void push_field_colors()
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, FIELD_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, FIELD_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, FIELD_ACTIVE_COLOR);
}

void pop_field_colors()
{
    ImGui::PopStyleColor(FIELD_STYLE_COLORS);
}

} // namespace deploy_widgets
} // namespace unravel
