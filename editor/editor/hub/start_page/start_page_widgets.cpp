#include "start_page_widgets.h"

#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>
#include <imgui_widgets/tooltips.h>

#include <array>
#include <cstring>

namespace unravel::start_page_widgets
{
namespace
{
using screen_card::to_pixels;

constexpr float WIDGETS_BUTTON_HEIGHT = 2.3f;
constexpr float WIDGETS_BUTTON_PADDING_X = 1.0f;
constexpr float WIDGETS_ROUNDING = 0.45f;
constexpr float WIDGETS_TITLE_SCALE = 1.5f;
constexpr float WIDGETS_SUBTITLE_GAP = 0.25f;
constexpr float WIDGETS_PILL_PADDING_X = 0.55f;
constexpr float WIDGETS_PILL_PADDING_Y = 0.12f;
constexpr float WIDGETS_EMPTY_ICON_SCALE = 2.6f;
constexpr float WIDGETS_EMPTY_LINE_GAP = 0.5f;
// Where an empty state sits in the height left in the window.
constexpr float WIDGETS_EMPTY_STATE_HEIGHT_SHARE = 0.3f;
constexpr float WIDGETS_SELECTED_BORDER = 2.0f;
constexpr float WIDGETS_SELECTED_FILL_ALPHA = 0.12f;
constexpr ImU32 WIDGETS_FIELD_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 WIDGETS_FIELD_HOVERED_COLOR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 WIDGETS_FIELD_ACTIVE_COLOR = IM_COL32(255, 255, 255, 30);
constexpr ImU32 WIDGETS_CARD_COLOR = IM_COL32(255, 255, 255, 9);
constexpr ImU32 WIDGETS_CARD_HOVERED_COLOR = IM_COL32(255, 255, 255, 16);
constexpr ImU32 WIDGETS_CARD_BORDER_COLOR = IM_COL32(255, 255, 255, 16);
constexpr ImU32 WIDGETS_CARD_HOVERED_BORDER_COLOR = IM_COL32(255, 255, 255, 52);
constexpr ImVec4 WIDGETS_SECONDARY_COLOR{1.0f, 1.0f, 1.0f, 0.08f};
constexpr ImVec4 WIDGETS_SECONDARY_HOVERED_COLOR{1.0f, 1.0f, 1.0f, 0.14f};
constexpr ImVec4 WIDGETS_SECONDARY_ACTIVE_COLOR{1.0f, 1.0f, 1.0f, 0.2f};
constexpr ImVec4 WIDGETS_GHOST_COLOR{1.0f, 1.0f, 1.0f, 0.0f};
constexpr ImVec4 WIDGETS_DANGER_COLOR{0.70f, 0.24f, 0.24f, 1.0f};
constexpr ImVec4 WIDGETS_DANGER_HOVERED_COLOR{0.84f, 0.31f, 0.31f, 1.0f};
constexpr ImVec4 WIDGETS_DANGER_ACTIVE_COLOR{0.60f, 0.20f, 0.20f, 1.0f};
constexpr float WIDGETS_ACCENT_REST_SCALE = 0.92f;
constexpr float WIDGETS_ACCENT_ACTIVE_SCALE = 0.85f;
constexpr float WIDGETS_EMPTY_ICON_ALPHA = 0.35f;
constexpr float WIDGETS_EMPTY_HEADLINE_ALPHA = 0.9f;

auto scale_rgb(const ImVec4& color, float factor) -> ImVec4
{
    return {color.x * factor, color.y * factor, color.z * factor, color.w};
}

auto get_button_colors(button_kind kind) -> std::array<ImVec4, 3>
{
    switch(kind)
    {
        case button_kind::primary:
        {
            ImVec4 accent = imgui_style::get_accent_color();
            accent.w = 1.0f;
            return {scale_rgb(accent, WIDGETS_ACCENT_REST_SCALE), accent, scale_rgb(accent, WIDGETS_ACCENT_ACTIVE_SCALE)};
        }
        case button_kind::secondary:
            return {WIDGETS_SECONDARY_COLOR, WIDGETS_SECONDARY_HOVERED_COLOR, WIDGETS_SECONDARY_ACTIVE_COLOR};
        case button_kind::ghost:
            return {WIDGETS_GHOST_COLOR, WIDGETS_SECONDARY_COLOR, WIDGETS_SECONDARY_HOVERED_COLOR};
        case button_kind::danger:
            return {WIDGETS_DANGER_COLOR, WIDGETS_DANGER_HOVERED_COLOR, WIDGETS_DANGER_ACTIVE_COLOR};
    }
    return {WIDGETS_SECONDARY_COLOR, WIDGETS_SECONDARY_HOVERED_COLOR, WIDGETS_SECONDARY_ACTIVE_COLOR};
}

void push_button_style(button_kind kind)
{
    const std::array<ImVec4, 3> colors = get_button_colors(kind);
    ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[1]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[2]);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(WIDGETS_ROUNDING));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(to_pixels(WIDGETS_BUTTON_PADDING_X), ImGui::GetStyle().FramePadding.y));
    // The weights that carry the icon glyphs: bold for the main action, regular for the rest.
    ImGui::PushFont(kind == button_kind::primary || kind == button_kind::danger ? ImGui::Font::Bold : ImGui::Font::Regular);
}

void pop_button_style()
{
    ImGui::PopFont();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
}
} // namespace

auto get_button_height() -> float
{
    return to_pixels(WIDGETS_BUTTON_HEIGHT);
}

auto calc_button_width(const char* label) -> float
{
    ImGui::PushFont(ImGui::Font::Bold);
    const float width = ImGui::CalcTextSize(label, nullptr, true).x + 2.0f * to_pixels(WIDGETS_BUTTON_PADDING_X);
    ImGui::PopFont();
    return ImCeil(width);
}

auto draw_button(const char* label, button_kind kind, float width, bool is_enabled) -> bool
{
    push_button_style(kind);
    ImGui::BeginDisabled(!is_enabled);
    const float button_width = width > 0.0f ? width : calc_button_width(label);
    const bool is_pressed = ImGui::Button(label, ImVec2(button_width, get_button_height()));
    ImGui::EndDisabled();
    pop_button_style();
    return is_pressed;
}

auto draw_icon_toggle(const char* id, const char* icon, bool is_on, const char* tooltip) -> bool
{
    push_button_style(is_on ? button_kind::secondary : button_kind::ghost);
    // No side padding: the padding of a text button leaves a square one less room than the icon
    // takes, and ImGui then starts the icon at the left of that room instead of centering it.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
    ImGui::PushID(id);
    const float size = get_button_height();
    const bool is_pressed = ImGui::Button(icon, ImVec2(size, size));
    ImGui::PopID();
    ImGui::PopStyleVar();
    pop_button_style();
    ImGui::SetItemTooltipEx("%s", tooltip);
    return is_pressed;
}

void push_field_style()
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, WIDGETS_FIELD_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, WIDGETS_FIELD_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, WIDGETS_FIELD_ACTIVE_COLOR);
    // The arrow of a combo is a button: washed like the field, it reads as part of it.
    ImGui::PushStyleColor(ImGuiCol_Button, WIDGETS_FIELD_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WIDGETS_FIELD_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, WIDGETS_FIELD_ACTIVE_COLOR);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(WIDGETS_ROUNDING));
    const float padding_y = (get_button_height() - ImGui::GetFontSize()) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(to_pixels(WIDGETS_BUTTON_PADDING_X) * 0.75f, padding_y));
}

void pop_field_style()
{
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(6);
}

void draw_page_title(const char* title, const char* subtitle, float wrap_width)
{
    ImGui::PushFont(ImGui::Font::Black);
    ImGui::PushWindowFontScale(WIDGETS_TITLE_SCALE);
    ImGui::TextUnformatted(title);
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + to_pixels(WIDGETS_SUBTITLE_GAP));
    draw_muted_text(subtitle, wrap_width);
}

void draw_muted_text(const char* text, float wrap_width)
{
    ImGui::PushStyleColor(ImGuiCol_Text, get_muted_color());
    ImGui::PushTextWrapPos(wrap_width > 0.0f ? ImGui::GetCursorPosX() + wrap_width : 0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void draw_empty_state(const char* icon, const char* headline, const char* text)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float start_x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * WIDGETS_EMPTY_STATE_HEIGHT_SHARE);
    const auto centered_line = [&](const char* line, ImU32 color)
    {
        const float line_width = ImGui::CalcTextSize(line).x;
        ImGui::SetCursorPosX(start_x + ImMax(0.0f, (avail.x - line_width) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(line);
        ImGui::PopStyleColor();
    };
    ImGui::PushFont(ImGui::Font::Regular);
    ImGui::PushWindowFontScale(WIDGETS_EMPTY_ICON_SCALE);
    centered_line(icon, get_text_color(WIDGETS_EMPTY_ICON_ALPHA));
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, to_pixels(WIDGETS_EMPTY_LINE_GAP)));
    ImGui::PushFont(ImGui::Font::SemiBold);
    centered_line(headline, get_text_color(WIDGETS_EMPTY_HEADLINE_ALPHA));
    ImGui::PopFont();
    centered_line(text, get_muted_color());
}

auto get_accent_color(float alpha) -> ImU32
{
    ImVec4 accent = imgui_style::get_accent_color();
    accent.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(accent);
}

auto get_text_color(float alpha) -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Text, alpha);
}

auto get_muted_color() -> ImU32
{
    return imgui_style::get_muted_text_color_u32();
}

auto calc_pill_size(const char* text) -> ImVec2
{
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    return ImVec2(ImCeil(text_size.x + 2.0f * to_pixels(WIDGETS_PILL_PADDING_X)),
                  ImCeil(text_size.y + 2.0f * to_pixels(WIDGETS_PILL_PADDING_Y)));
}

void draw_pill(ImDrawList* draw_list, const ImVec2& pos, const char* text, ImU32 fill_color, ImU32 text_color)
{
    const ImVec2 size = calc_pill_size(text);
    draw_list->AddRectFilled(pos, pos + size, fill_color, size.y * 0.5f);
    const ImVec2 text_pos(ImFloor(pos.x + to_pixels(WIDGETS_PILL_PADDING_X)), ImFloor(pos.y + to_pixels(WIDGETS_PILL_PADDING_Y)));
    draw_list->AddText(text_pos, text_color, text);
}

void draw_card_frame(ImDrawList* draw_list, const ImRect& rect, float rounding, bool is_hovered, bool is_selected)
{
    draw_list->AddRectFilled(rect.Min, rect.Max, is_hovered ? WIDGETS_CARD_HOVERED_COLOR : WIDGETS_CARD_COLOR, rounding);
    if(is_selected)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, get_accent_color(WIDGETS_SELECTED_FILL_ALPHA), rounding);
        draw_list->AddRect(rect.Min, rect.Max, get_accent_color(1.0f), rounding, ImDrawFlags_None, WIDGETS_SELECTED_BORDER);
        return;
    }
    draw_list->AddRect(rect.Min, rect.Max, is_hovered ? WIDGETS_CARD_HOVERED_BORDER_COLOR : WIDGETS_CARD_BORDER_COLOR, rounding);
}

void draw_text_wrapped(ImDrawList* draw_list, const ImVec2& pos, float max_x, ImU32 color, const char* text, int max_lines)
{
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();
    const float wrap_width = max_x - pos.x;
    const char* line_start = text;
    const char* text_end = text + std::strlen(text);
    for(int line = 0; line < max_lines && line_start < text_end; ++line)
    {
        const ImVec2 line_pos(pos.x, pos.y + static_cast<float>(line) * font_size);
        if(line + 1 == max_lines)
        {
            // The last line takes the rest, cut with an ellipsis where it does not fit.
            draw_text_clipped(draw_list, line_pos, max_x, color, line_start);
            return;
        }
        const char* line_end = font->CalcWordWrapPosition(font_size, line_start, text_end, wrap_width);
        if(line_end == line_start)
        {
            line_end = line_start + 1;
        }
        draw_list->AddText(font, font_size, ImVec2(ImFloor(line_pos.x), ImFloor(line_pos.y)), color, line_start, line_end);
        line_start = line_end;
        while(line_start < text_end && (*line_start == ' ' || *line_start == '\n'))
        {
            ++line_start;
        }
    }
}

void draw_text_clipped(ImDrawList* draw_list, const ImVec2& pos, float max_x, ImU32 color, const char* text)
{
    if(max_x <= pos.x)
    {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const ImVec2 pos_min(ImFloor(pos.x), ImFloor(pos.y));
    ImGui::RenderTextEllipsis(draw_list, pos_min, ImVec2(max_x, pos.y + ImGui::GetFontSize()), max_x, text, nullptr, nullptr);
    ImGui::PopStyleColor();
}
} // namespace unravel::start_page_widgets
