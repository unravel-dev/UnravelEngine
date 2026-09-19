#include "screen_card.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace unravel::screen_card
{
namespace
{
constexpr float SCREEN_CARD_PADDING_X = 2.0f;
constexpr float SCREEN_CARD_PADDING_Y = 1.6f;
constexpr float SCREEN_CARD_ROUNDING = 0.75f;
constexpr float SCREEN_CARD_VIEWPORT_MARGIN = 1.5f;
constexpr ImVec4 SCREEN_CARD_BACKDROP_COLOR{0.075f, 0.075f, 0.08f, 1.0f};
// A soft drop shadow out of growing rings, each a little darker towards the card.
constexpr int SCREEN_CARD_SHADOW_RINGS = 12;
constexpr int SCREEN_CARD_SHADOW_RING_ALPHA = 9;
constexpr float SCREEN_CARD_SHADOW_RING_STEP = 2.0f;
constexpr int SCREEN_CARD_STYLE_VARS = 3;
constexpr int SCREEN_CARD_STYLE_COLORS = 1;

void draw_soft_shadow(ImDrawList* draw_list, const ImRect& rect, float rounding)
{
    for(int ring = 1; ring <= SCREEN_CARD_SHADOW_RINGS; ++ring)
    {
        const float grow = static_cast<float>(ring) * SCREEN_CARD_SHADOW_RING_STEP;
        const ImVec2 expand(grow, grow);
        draw_list->AddRectFilled(rect.Min - expand,
                                 rect.Max + expand,
                                 IM_COL32(0, 0, 0, SCREEN_CARD_SHADOW_RING_ALPHA),
                                 rounding + grow);
    }
}

void begin_backdrop(const char* id, const ImGuiViewport* viewport)
{
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, SCREEN_CARD_BACKDROP_COLOR);
    const ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                          ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar |
                                          ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin(id, nullptr, window_flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

auto calc_card_min(const ImGuiViewport* viewport, const card_layout& layout) -> ImVec2
{
    const float centered_height = layout.centered_height > 0.0f ? layout.centered_height : layout.size.y;
    // Never above the viewport: a card taller than the window shows its top.
    const float offset_y = ImMax((viewport->WorkSize.y - centered_height) * 0.5f, 0.0f);
    return ImVec2(ImFloor(viewport->WorkPos.x + (viewport->WorkSize.x - layout.size.x) * 0.5f),
                  ImFloor(viewport->WorkPos.y + offset_y));
}
} // namespace

auto to_pixels(float font_units) -> float
{
    return ImFloor(ImGui::GetFontSize() * font_units);
}

auto get_padding() -> ImVec2
{
    return ImVec2(to_pixels(SCREEN_CARD_PADDING_X), to_pixels(SCREEN_CARD_PADDING_Y));
}

auto fit_to_viewport(const ImVec2& wanted_size) -> ImVec2
{
    const ImVec2 viewport_size = ImGui::GetMainViewport()->WorkSize;
    const float margin = to_pixels(SCREEN_CARD_VIEWPORT_MARGIN);
    return ImVec2(ImMin(wanted_size.x, viewport_size.x - 2.0f * margin),
                  ImMin(wanted_size.y, viewport_size.y - 2.0f * margin));
}

auto begin(const char* id, const card_layout& layout) -> bool
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    begin_backdrop(id, viewport);
    const ImVec2 card_min = calc_card_min(viewport, layout);
    const float rounding = to_pixels(SCREEN_CARD_ROUNDING);
    draw_soft_shadow(ImGui::GetWindowDrawList(), ImRect(card_min, card_min + layout.size), rounding);
    ImGui::SetCursorScreenPos(card_min);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, get_padding());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
    const ImGuiChildFlags card_flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    const bool is_visible = ImGui::BeginChild("##card", layout.size, card_flags, ImGuiWindowFlags_NoScrollbar);
    // The card has taken its look by now. Left on the stack, it would also shape every tooltip,
    // popup and child opened from the content of the card.
    ImGui::PopStyleColor(SCREEN_CARD_STYLE_COLORS);
    ImGui::PopStyleVar(SCREEN_CARD_STYLE_VARS);
    return is_visible;
}

void end()
{
    ImGui::EndChild();
    ImGui::End();
}
} // namespace unravel::screen_card
