#include "panel_section.h"

#include "editor/hub/panels/inspector_panel/inspectors/inspector_container_widgets.h"
#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"
#include "editor/imgui/integration/imgui.h"
#include "editor/imgui/integration/imgui_style.h"

#include <imgui/imgui.h>

namespace unravel::panel_section
{
namespace
{
using container_widgets::to_pixels;

// Sizes are in units of the font size, so the header follows the UI scale of the editor.
constexpr float SECTION_GAP = 0.35f;
constexpr float CHEVRON_WIDTH = 1.0f;
constexpr float ICON_WIDTH = 1.3f;
constexpr float ROUNDING = 0.3f;

constexpr ImU32 DIVIDER_COLOR = IM_COL32(255, 255, 255, 22);
constexpr ImU32 HOVERED_COLOR = IM_COL32(255, 255, 255, 18);

auto get_accent_color() -> ImU32
{
    return ImGui::GetColorU32(imgui_style::get_accent_color());
}

} // namespace

auto draw_header(const char* id, const char* icon, const char* title, bool is_default_open, float trailing_width)
    -> header
{
    ImGui::Dummy(ImVec2(0.0f, to_pixels(SECTION_GAP)));
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddLine(min, ImVec2(min.x + width, min.y), DIVIDER_COLOR);
    ImGui::Dummy(ImVec2(width, height));

    header result{};
    result.row = ImRect(min, min + ImVec2(width, height));
    const ImGuiID item_id = ImGui::GetID(id);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    result.is_open = storage->GetBool(item_id, is_default_open);
    const ImRect press_rect(result.row.Min, ImVec2(result.row.Max.x - trailing_width, result.row.Max.y));
    bool is_hovered = false;
    if(ImGui::ItemAdd(press_rect, item_id))
    {
        bool is_held = false;
        if(ImGui::ButtonBehavior(press_rect, item_id, &is_hovered, &is_held))
        {
            result.is_open = !result.is_open;
            storage->SetBool(item_id, result.is_open);
        }
    }
    if(is_hovered)
    {
        draw_list->AddRectFilled(press_rect.Min, press_rect.Max, HOVERED_COLOR, to_pixels(ROUNDING));
    }

    const float center_y = result.row.GetCenter().y;
    const float chevron_width = to_pixels(CHEVRON_WIDTH);
    const float icon_width = to_pixels(ICON_WIDTH);
    ImGui::RenderIconCentered(draw_list,
                              ImVec2(min.x + chevron_width * 0.5f, center_y),
                              result.is_open ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT,
                              imgui_style::get_muted_text_color_u32());
    if(icon != nullptr)
    {
        ImGui::RenderIconCentered(draw_list,
                                  ImVec2(min.x + chevron_width + icon_width * 0.5f, center_y),
                                  icon,
                                  get_accent_color());
    }
    ImGui::PushFont(ImGui::Font::SemiBold);
    draw_list->AddText(ImVec2(min.x + chevron_width + icon_width, center_y - ImGui::GetTextLineHeight() * 0.5f),
                       ImGui::GetColorU32(ImGuiCol_Text),
                       title);
    ImGui::PopFont();
    return result;
}

auto get_row_indent() -> float
{
    return to_pixels(CHEVRON_WIDTH);
}

} // namespace unravel::panel_section
