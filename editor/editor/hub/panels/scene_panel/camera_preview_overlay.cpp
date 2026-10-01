#include "camera_preview_overlay.h"
#include "../panel_toolbar.h"
#include "editor/hub/panels/inspector_panel/inspectors/inspector_container_widgets.h"
#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"
#include "editor/imgui/integration/imgui_style.h"
#include <editor/shortcuts.h>

#include <string>

namespace unravel::camera_preview_overlay
{
namespace
{
using container_widgets::to_pixels;

// Sizes are in units of the font size, so the card follows the UI scale of the editor.
constexpr float CARD_MIN_WIDTH = 14.0f;
constexpr float CARD_MAX_WIDTH = 26.0f;
constexpr float HEADER_HEIGHT = 1.6f;
constexpr float TITLE_ICON_GAP = 0.4f;
constexpr float CONTENT_GAP = 0.45f;
constexpr float IMAGE_ROUNDING = 0.3f;
constexpr float BADGE_PADDING_X = 0.4f;
constexpr float BADGE_PADDING_Y = 0.12f;
constexpr float BADGE_MARGIN = 0.4f;
constexpr float BADGE_ROUNDING = 0.25f;
// Shares of the view the card may take: wide enough to read, never in the way of the scene.
constexpr float CARD_WIDTH_SHARE = 0.28f;
constexpr float IMAGE_HEIGHT_SHARE = 0.45f;
// Until the camera reports a size.
constexpr float DEFAULT_ASPECT = 16.0f / 9.0f;

constexpr ImU32 IMAGE_BG_COLOR = IM_COL32(0, 0, 0, 110);
constexpr ImU32 IMAGE_BORDER_COLOR = IM_COL32(255, 255, 255, 22);
constexpr ImU32 BADGE_BG_COLOR = IM_COL32(0, 0, 0, 150);
constexpr float BADGE_TEXT_ALPHA = 0.85f;
constexpr float PLACEHOLDER_TEXT_ALPHA = 0.5f;

struct card_layout
{
    bool is_visible{};
    ImRect card{};
    /// Height of the area the image is fitted into; 0 while collapsed.
    float image_height{};
};

struct header_request
{
    bool is_align_pressed{};
    bool is_collapse_pressed{};
};

auto calc_aspect(const preview_desc& desc) -> float
{
    if(desc.resolution.x <= 0.0f || desc.resolution.y <= 0.0f)
    {
        return DEFAULT_ASPECT;
    }
    return desc.resolution.x / desc.resolution.y;
}

auto calc_card_layout(const preview_desc& desc, const ImRect& view_rect, bool is_collapsed) -> card_layout
{
    const float margin = panel_toolbar::get_bar_margin();
    const float padding = panel_toolbar::get_overlay_padding();
    const float width = ImClamp(view_rect.GetWidth() * CARD_WIDTH_SHARE, to_pixels(CARD_MIN_WIDTH), to_pixels(CARD_MAX_WIDTH));
    card_layout layout{};
    if(!is_collapsed)
    {
        const float image_width = width - 2.0f * padding;
        layout.image_height = ImFloor(ImMin(image_width / calc_aspect(desc), view_rect.GetHeight() * IMAGE_HEIGHT_SHARE));
    }
    const float image_extent = is_collapsed ? 0.0f : to_pixels(CONTENT_GAP) + layout.image_height;
    const float height = 2.0f * padding + to_pixels(HEADER_HEIGHT) + image_extent;
    layout.is_visible = width + 2.0f * margin <= view_rect.GetWidth() && height + 2.0f * margin <= view_rect.GetHeight();
    const ImVec2 card_max(view_rect.Max.x - margin, view_rect.Max.y - margin);
    layout.card = ImRect(card_max - ImVec2(width, height), card_max);
    return layout;
}

/// The camera name with an icon, and the buttons at the right end of the row.
auto draw_header(const preview_desc& desc, bool is_collapsed) -> header_request
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = to_pixels(HEADER_HEIGHT);
    ImGui::Dummy(ImVec2(width, height));
    header_request request{};
    const ImVec2 button_size(height, height);
    const ImVec2 collapse_min(min.x + width - height, min.y);
    const char* collapse_icon = is_collapsed ? ICON_MDI_CHEVRON_UP : ICON_MDI_CHEVRON_DOWN;
    const char* collapse_tooltip = is_collapsed ? "Show the preview" : "Hide the preview";
    request.is_collapse_pressed = container_widgets::draw_icon_button({"##collapse", collapse_icon, collapse_tooltip},
                                                                      ImRect(collapse_min, collapse_min + button_size));
    const ImVec2 align_min(collapse_min.x - height, min.y);
    const std::string align_tooltip =
        fmt::format("Align with View ({})\nMove the camera to the scene view",
                    ImGui::GetKeyChordName(shortcuts::snap_scene_camera_to_selected_camera));
    request.is_align_pressed = container_widgets::draw_icon_button({"##align", ICON_MDI_CAMERA_SWITCH, align_tooltip.c_str()},
                                                                   ImRect(align_min, align_min + button_size));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float center_y = min.y + height * 0.5f;
    ImGui::PushFont(ImGui::Font::Bold);
    const float icon_width = ImGui::GetFontSize();
    const ImU32 accent = ImGui::GetColorU32(imgui_style::get_accent_color());
    ImGui::RenderIconCentered(draw_list, ImVec2(min.x + icon_width * 0.5f, center_y), ICON_MDI_VIDEO, accent);
    // A long name ends in an ellipsis before the buttons.
    const ImVec2 text_min(min.x + icon_width + to_pixels(TITLE_ICON_GAP), center_y - ImGui::GetTextLineHeight() * 0.5f);
    const ImVec2 text_max(align_min.x - to_pixels(TITLE_ICON_GAP), text_min.y + ImGui::GetTextLineHeight());
    ImGui::RenderTextEllipsis(draw_list, text_min, text_max, text_max.x, desc.name, nullptr, nullptr);
    ImGui::PopFont();
    return request;
}

/// Resolution and projection, on a dark pill in the bottom left corner of the image.
void draw_badge(ImDrawList* draw_list, const preview_desc& desc, const ImRect& image_rect)
{
    std::string text;
    if(desc.resolution.x > 0.0f && desc.resolution.y > 0.0f)
    {
        text = fmt::format("{:.0f}x{:.0f}", desc.resolution.x, desc.resolution.y);
    }
    if(desc.projection != nullptr)
    {
        text.append(text.empty() ? "" : "   ").append(desc.projection);
    }
    if(text.empty())
    {
        return;
    }
    const ImVec2 padding(to_pixels(BADGE_PADDING_X), to_pixels(BADGE_PADDING_Y));
    const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
    const float margin = to_pixels(BADGE_MARGIN);
    const ImVec2 badge_min(image_rect.Min.x + margin, image_rect.Max.y - margin - text_size.y - 2.0f * padding.y);
    const ImVec2 badge_max = badge_min + text_size + 2.0f * padding;
    // A badge wider than the image would cover what it describes.
    if(badge_max.x > image_rect.Max.x - margin || badge_min.y < image_rect.Min.y + margin)
    {
        return;
    }
    draw_list->AddRectFilled(badge_min, badge_max, BADGE_BG_COLOR, to_pixels(BADGE_ROUNDING));
    draw_list->AddText(badge_min + padding, ImGui::GetColorU32(ImGuiCol_Text, BADGE_TEXT_ALPHA), text.c_str());
}

/// The output fitted to its aspect, centered in an area of the full width and the given height.
void draw_image(const preview_desc& desc, float area_height)
{
    const ImVec2 area_min = ImGui::GetCursorScreenPos();
    const ImVec2 area_size(ImGui::GetContentRegionAvail().x, area_height);
    ImGui::Dummy(area_size);
    const float aspect = calc_aspect(desc);
    const ImVec2 image_size(ImMin(area_size.x, area_size.y * aspect), ImMin(area_size.y, area_size.x / aspect));
    const ImVec2 image_min(ImFloor(area_min.x + (area_size.x - image_size.x) * 0.5f), area_min.y);
    const ImRect image_rect(image_min, image_min + image_size);
    const float rounding = to_pixels(IMAGE_ROUNDING);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(image_rect.Min, image_rect.Max, IMAGE_BG_COLOR, rounding);
    if(desc.texture == ImTextureID{})
    {
        const char* placeholder = "No camera output";
        const ImVec2 text_size = ImGui::CalcTextSize(placeholder);
        draw_list->AddText(ImFloor(image_rect.GetCenter() - text_size * 0.5f),
                           ImGui::GetColorU32(ImGuiCol_Text, PLACEHOLDER_TEXT_ALPHA),
                           placeholder);
    }
    else
    {
        draw_list->AddImageRounded(desc.texture,
                                   image_rect.Min,
                                   image_rect.Max,
                                   ImVec2(0.0f, 0.0f),
                                   ImVec2(1.0f, 1.0f),
                                   IM_COL32_WHITE,
                                   rounding);
        draw_badge(draw_list, desc, image_rect);
    }
    draw_list->AddRect(image_rect.Min, image_rect.Max, IMAGE_BORDER_COLOR, rounding);
}

} // namespace

auto draw(const char* id, const preview_desc& desc, const ImRect& view_rect, state& preview_state) -> request
{
    request result{};
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window == nullptr || window->SkipItems)
    {
        return result;
    }
    const card_layout layout = calc_card_layout(desc, view_rect, preview_state.is_collapsed);
    if(!layout.is_visible)
    {
        return result;
    }
    if(panel_toolbar::begin_overlay(id, layout.card))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, to_pixels(CONTENT_GAP)));
        const header_request header = draw_header(desc, preview_state.is_collapsed);
        if(!preview_state.is_collapsed)
        {
            draw_image(desc, layout.image_height);
        }
        ImGui::PopStyleVar();
        result.is_align_pressed = header.is_align_pressed;
        if(header.is_collapse_pressed)
        {
            preview_state.is_collapsed = !preview_state.is_collapsed;
        }
    }
    panel_toolbar::end_overlay();
    return result;
}

} // namespace unravel::camera_preview_overlay
