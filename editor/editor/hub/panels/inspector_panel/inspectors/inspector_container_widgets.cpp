#include "inspector_container_widgets.h"

#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"
#include "editor/imgui/integration/imgui.h"
#include "imgui_widgets/utils.h"

#include "imgui/imgui.h"

namespace unravel::container_widgets
{
namespace
{
// Sizes are in units of the font size.
constexpr float CONTAINER_COUNT_FIELD_WIDTH = 4.0f;
constexpr float CONTAINER_BUTTON_ROUNDING = 0.25f;
constexpr float CONTAINER_ICON_ALPHA = 0.6f;
constexpr ImU32 CONTAINER_BUTTON_HOVERED_COLOR = IM_COL32(255, 255, 255, 26);
constexpr ImU32 CONTAINER_BUTTON_HELD_COLOR = IM_COL32(255, 255, 255, 46);
// Remove stays as quiet as the other buttons until it is pointed at.
constexpr ImU32 CONTAINER_REMOVE_COLOR = IM_COL32(255, 95, 95, 255);
constexpr ImU32 CONTAINER_REMOVE_FILL_COLOR = IM_COL32(255, 95, 95, 40);
constexpr const char* CONTAINER_PENDING_COUNT_ID = "##container_pending_count";
/// How far an open container's rows sit inside its frame, in units of the font size.
constexpr float CONTAINER_BODY_PADDING = 0.35f;
/// How far the body's background goes from the window's color toward the fields'.
constexpr float CONTAINER_BODY_SHADE = 0.4f;
/// The frame reaches this share of the item spacing past its first and last rows, so it never meets its neighbors.
constexpr float CONTAINER_BODY_MARGIN_SHARE = 0.5f;
/// The body's rows are drawn into one channel and its background, once their extent is known, into the one below.
constexpr int CONTAINER_BODY_BACKGROUND_CHANNEL = 0;
constexpr int CONTAINER_BODY_ROWS_CHANNEL = 1;
constexpr int CONTAINER_BODY_CHANNEL_COUNT = 2;

auto get_body_color() -> ImU32
{
    const ImVec4 window = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const ImVec4 field = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    return ImGui::GetColorU32(ImLerp(window, field, CONTAINER_BODY_SHADE));
}

auto get_icon_color(const icon_button& button, bool is_hovered) -> ImU32
{
    if(button.is_destructive && is_hovered)
    {
        return CONTAINER_REMOVE_COLOR;
    }
    if(button.color != 0)
    {
        return button.color;
    }
    return ImGui::GetColorU32(ImGuiCol_Text, is_hovered ? 1.0f : CONTAINER_ICON_ALPHA);
}

//-----------------------------------------------------------------------------
/// <summary>
/// The count field. The count being typed lives across frames: the field writes it while it is
/// active, and on the frame it is left it writes nothing, so that frame reads it back.
/// </summary>
//-----------------------------------------------------------------------------
void draw_count_field(const header_controls& controls, header_request& request)
{
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID pending_key = ImGui::GetID(CONTAINER_PENDING_COUNT_ID);
    int pending_count = storage->GetInt(pending_key, static_cast<int>(controls.size));
    const bool is_readonly = !controls.is_count_editable;
    ImGui::PushReadonly(is_readonly);
    ImGui::SetNextItemWidth(to_pixels(CONTAINER_COUNT_FIELD_WIDTH));
    ImGui::InputInt("##count", &pending_count, 0, 0, is_readonly ? ImGuiInputTextFlags_ReadOnly : 0);
    ImGui::SetItemTooltipEx("%s", controls.count_tooltip);
    request.is_count_entered = ImGui::IsItemDeactivatedAfterEdit() && !is_readonly;
    request.entered_count = pending_count;
    storage->SetInt(pending_key, ImGui::IsItemActive() ? pending_count : static_cast<int>(controls.size));
    ImGui::DrawItemActivityOutline();
    ImGui::PopReadonly();
}
} // namespace

auto to_pixels(float font_units) -> float
{
    return ImFloor(ImGui::GetFontSize() * font_units);
}

auto draw_icon_button(const icon_button& button, const ImRect& bb) -> bool
{
    const ImGuiID item_id = ImGui::GetID(button.id);
    if(!ImGui::ItemAdd(bb, item_id))
    {
        return false;
    }
    bool is_hovered = false;
    bool is_held = false;
    const bool is_pressed = ImGui::ButtonBehavior(bb, item_id, &is_hovered, &is_held);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if(is_hovered || is_held)
    {
        const ImU32 plain_fill_color = is_held ? CONTAINER_BUTTON_HELD_COLOR : CONTAINER_BUTTON_HOVERED_COLOR;
        draw_list->AddRectFilled(bb.Min,
                                 bb.Max,
                                 button.is_destructive ? CONTAINER_REMOVE_FILL_COLOR : plain_fill_color,
                                 to_pixels(CONTAINER_BUTTON_ROUNDING));
    }
    ImGui::RenderIconCentered(draw_list, bb.GetCenter(), button.icon, get_icon_color(button, is_hovered));
    if(button.tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", button.tooltip);
    }
    return is_pressed;
}

auto draw_header_controls(const header_controls& controls) -> header_request
{
    header_request request{};
    const float button_width = ImGui::GetFrameHeight();
    const float add_width = controls.can_add ? ImGui::GetStyle().ItemSpacing.x + button_width : 0.0f;
    const float controls_width = to_pixels(CONTAINER_COUNT_FIELD_WIDTH) + add_width;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - controls_width));
    draw_count_field(controls, request);
    if(controls.can_add)
    {
        ImGui::SameLine();
        const ImVec2 button_min = ImGui::GetCursorScreenPos();
        const ImRect button_rect(button_min, button_min + ImVec2(button_width, button_width));
        ImGui::ItemSize(button_rect);
        request.is_add_pressed = draw_icon_button({"##add", ICON_MDI_PLUS, controls.add_tooltip}, button_rect);
    }
    return request;
}

auto get_row_button_width() -> float
{
    return ImGui::GetFrameHeight();
}

auto draw_row_remove_button(const ImRect& label_rect, const ImRect& row_rect, const char* tooltip) -> bool
{
    const bool is_row_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                                ImGui::IsMouseHoveringRect(row_rect.Min, row_rect.Max) && !ImGui::IsDragDropActive();
    if(!is_row_hovered)
    {
        return false;
    }
    const float side = get_row_button_width();
    const ImRect remove_rect(ImVec2(label_rect.Max.x - side, label_rect.Min.y), ImVec2(label_rect.Max.x, label_rect.Min.y + side));
    return draw_icon_button({"##remove", ICON_MDI_DELETE_OUTLINE, tooltip, true}, remove_rect);
}

void draw_empty_hint(const std::string& text)
{
    ImGui::Indent();
    ImGui::AlignTextToFramePadding();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text.empty() ? "Empty" : text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Unindent();
}

body_frame::body_frame(float left_x)
    : draw_list_(ImGui::GetWindowDrawList())
    , left_x_(ImMin(left_x, ImGui::GetCursorScreenPos().x))
    , padding_(to_pixels(CONTAINER_BODY_PADDING))
{
    splitter_.Split(draw_list_, CONTAINER_BODY_CHANNEL_COUNT);
    splitter_.SetCurrentChannel(draw_list_, CONTAINER_BODY_ROWS_CHANNEL);
    ImGui::BeginGroup();
    ImGui::Indent(padding_);
}

body_frame::~body_frame()
{
    ImGui::Unindent(padding_);
    ImGui::EndGroup();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float margin = style.ItemSpacing.y * CONTAINER_BODY_MARGIN_SHARE;
    const ImVec2 rows_min = ImGui::GetItemRectMin();
    const ImVec2 rows_max = ImGui::GetItemRectMax();
    const ImVec2 frame_min(left_x_, rows_min.y - margin);
    const ImVec2 frame_max(ImMax(rows_max.x, ImGui::GetCurrentWindow()->WorkRect.Max.x), rows_max.y + margin);
    splitter_.SetCurrentChannel(draw_list_, CONTAINER_BODY_BACKGROUND_CHANNEL);
    draw_list_->AddRectFilled(frame_min, frame_max, get_body_color(), style.FrameRounding);
    splitter_.Merge(draw_list_);
    draw_list_->AddRect(frame_min, frame_max, ImGui::GetColorU32(ImGuiCol_TableBorderLight), style.FrameRounding);
}

} // namespace unravel::container_widgets
