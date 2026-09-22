#include "inspector_asset_picker.h"
#include "inspector_container_widgets.h"

#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"
#include "editor/imgui/integration/imgui.h"
#include "editor/imgui/integration/imgui_style.h"
#include "imgui/imgui_internal.h"
#include "imgui_widgets/tooltips.h"
#include "imgui_widgets/utils.h"

#include <algorithm>

namespace unravel::asset_picker
{
namespace
{
using container_widgets::to_pixels;

// Sizes are in units of the font size.
constexpr float PICKER_PREVIEW_ROUNDING = 0.3f;
constexpr float PICKER_FIELD_TEXT_PADDING = 0.5f;
constexpr float PICKER_WINDOW_SHARE_X = 0.5f;
constexpr float PICKER_WINDOW_SHARE_Y = 0.6f;
constexpr float PICKER_WINDOW_PADDING_X = 1.2f;
constexpr float PICKER_WINDOW_PADDING_Y = 1.0f;
constexpr float PICKER_WINDOW_ROUNDING = 0.75f;
constexpr float PICKER_TITLE_SCALE = 1.2f;
constexpr float PICKER_TITLE_ICON_GAP = 0.45f;
constexpr float PICKER_GAP = 0.6f;
constexpr float PICKER_TILE_MIN_SIZE = 4.5f;
constexpr float PICKER_TILE_MAX_SIZE = 10.0f;
constexpr float PICKER_TILE_DEFAULT_SIZE = 6.5f;
constexpr float PICKER_TILE_PADDING = 0.35f;
constexpr float PICKER_TILE_GAP = 0.5f;
constexpr float PICKER_TILE_ROUNDING = 0.45f;
constexpr float PICKER_TILE_BORDER = 1.5f;
constexpr float PICKER_EMPTY_ICON_SCALE = 1.6f;
constexpr float PICKER_TILE_ICON_SHARE = 0.4f;
constexpr float PICKER_SLIDER_WIDTH = 8.0f;
constexpr float PICKER_DROP_BORDER = 2.0f;
constexpr float PICKER_MUTED_ALPHA = 0.55f;
constexpr float PICKER_SELECTED_FILL_ALPHA = 0.35f;
constexpr ImU32 PICKER_PREVIEW_COLOR = IM_COL32(0, 0, 0, 40);
constexpr ImU32 PICKER_PREVIEW_HOVERED_BORDER_COLOR = IM_COL32(255, 255, 255, 60);
constexpr ImU32 PICKER_TILE_COLOR = IM_COL32(255, 255, 255, 8);
constexpr ImU32 PICKER_TILE_HOVERED_COLOR = IM_COL32(255, 255, 255, 22);
// The theme's fields are darker than a window and all but vanish on the lighter popup, so the
// search field is washed light, as on the start page.
constexpr ImU32 PICKER_FIELD_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 PICKER_FIELD_HOVERED_COLOR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 PICKER_FIELD_ACTIVE_COLOR = IM_COL32(255, 255, 255, 30);

/// Tile size in font units, kept for the session as Unity keeps the picker's.
float g_tile_size = PICKER_TILE_DEFAULT_SIZE;

auto get_muted_color() -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Text, PICKER_MUTED_ALPHA);
}

auto get_accent_color(float alpha = 1.0f) -> ImU32
{
    ImVec4 accent = imgui_style::get_accent_color();
    accent.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(accent);
}

/// An icon at a given size in pixels, centered on the point.
void draw_icon(ImDrawList* draw_list, const ImVec2& center, const char* icon, ImU32 color, float size)
{
    // Relative to the current size, so the UI scale is applied once.
    ImGui::PushWindowFontScale(size / ImGui::GetFontSize());
    ImGui::RenderIconCentered(draw_list, center, icon, color);
    ImGui::PopWindowFontScale();
}

/// The preview filling the rectangle: the texture fitted by its aspect, or else the icon.
void draw_preview(ImDrawList* draw_list, const ImRect& rect, const preview& content, float icon_size)
{
    const float rounding = to_pixels(PICKER_PREVIEW_ROUNDING);
    draw_list->AddRectFilled(rect.Min, rect.Max, PICKER_PREVIEW_COLOR, rounding);
    if(content.is_loading)
    {
        draw_icon(draw_list, rect.GetCenter(), ICON_MDI_PROGRESS_CLOCK, get_muted_color(), icon_size);
        return;
    }
    if(content.texture == ImTextureID{})
    {
        if(content.icon != nullptr)
        {
            draw_icon(draw_list, rect.GetCenter(), content.icon, get_muted_color(), icon_size);
        }
        return;
    }
    const ImVec2 area = rect.GetSize();
    const ImVec2 texture_size = content.texture_size.x > 0.0f && content.texture_size.y > 0.0f ? content.texture_size : area;
    const float scale = std::min(area.x / texture_size.x, area.y / texture_size.y);
    const ImVec2 image_size = texture_size * scale;
    const ImVec2 image_min = ImFloor(rect.GetCenter() - image_size * 0.5f);
    draw_list->AddImageRounded(content.texture,
                               image_min,
                               image_min + image_size,
                               ImVec2(0.0f, 0.0f),
                               ImVec2(1.0f, 1.0f),
                               IM_COL32_WHITE,
                               rounding);
}

/// Text cut with an ellipsis where it does not fit.
void draw_clipped_text(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, const char* text, ImU32 color)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(draw_list, min, max, max.x, text, nullptr, nullptr);
    ImGui::PopStyleColor();
}

/// An icon button that takes its place in the layout.
auto draw_inline_icon_button(const char* id, const char* icon, const char* tooltip) -> bool
{
    const float side = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect rect(min, min + ImVec2(side, side));
    ImGui::ItemSize(rect);
    return container_widgets::draw_icon_button({id, icon, tooltip}, rect);
}

/// The tooltip of the parts of the field that open the picker.
void set_pick_tooltip(const field_content& content)
{
    const bool has_asset = !content.name.empty();
    const std::string subject = has_asset ? content.path : fmt::format("None ({})", content.type);
    ImGui::SetItemTooltipEx("%s\n\nClick to pick another %s.", subject.c_str(), content.type.c_str());
}

/// The field naming the asset, drawn as an input field with the picker icon at its right end.
auto draw_pick_field(const field_content& content) -> bool
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    const bool is_pressed = ImGui::InvisibleButton("##pick", size);
    const bool is_hovered = ImGui::IsItemHovered();
    const bool is_active = ImGui::IsItemActive();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImRect rect(min, min + size);
    const ImGuiCol fill = is_active ? ImGuiCol_FrameBgActive : (is_hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fill), ImGui::GetStyle().FrameRounding);

    const float side = size.y;
    ImGui::RenderIconCentered(draw_list,
                              ImVec2(rect.Max.x - side * 0.5f, rect.GetCenter().y),
                              ICON_MDI_TARGET,
                              is_hovered ? ImGui::GetColorU32(ImGuiCol_Text) : get_muted_color());

    const bool has_asset = !content.name.empty();
    const std::string text = has_asset ? content.name : fmt::format("None ({})", content.type);
    const float text_padding = to_pixels(PICKER_FIELD_TEXT_PADDING);
    const float text_y = rect.GetCenter().y - ImGui::GetTextLineHeight() * 0.5f;
    draw_clipped_text(draw_list,
                      ImVec2(rect.Min.x + text_padding, text_y),
                      ImVec2(rect.Max.x - side, rect.Max.y),
                      text.c_str(),
                      has_asset ? ImGui::GetColorU32(ImGuiCol_Text) : get_muted_color());
    ImGui::DrawItemActivityOutline();
    set_pick_tooltip(content);
    return is_pressed;
}

/// Under the field: show in the content browser, clear, and where the asset is.
void draw_field_actions(const field_content& content, field_request& request)
{
    const bool has_asset = !content.name.empty();
    ImGui::BeginDisabled(!has_asset);
    request.is_locate_pressed |= draw_inline_icon_button("##locate", ICON_MDI_CROSSHAIRS_GPS, "Show in the content browser");
    ImGui::SameLine(0.0f, 0.0f);
    request.is_clear_pressed = draw_inline_icon_button("##clear", ICON_MDI_CLOSE_CIRCLE_OUTLINE, "Clear");
    ImGui::EndDisabled();
    if(!has_asset)
    {
        return;
    }
    ImGui::SameLine();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    ImGui::ItemSize(size);
    const float text_y = min.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f;
    draw_clipped_text(ImGui::GetWindowDrawList(), ImVec2(min.x, text_y), min + size, content.path.c_str(), get_muted_color());
}

void draw_window_header(const char* icon, const std::string& title, bool& is_close_requested)
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushWindowFontScale(PICKER_TITLE_SCALE);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float icon_width = ImGui::GetFontSize();
    const float line_height = ImGui::GetTextLineHeight();
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                              ImVec2(min.x + icon_width * 0.5f, min.y + line_height * 0.5f),
                              icon,
                              get_accent_color());
    ImGui::SetCursorScreenPos(ImVec2(min.x + icon_width + to_pixels(PICKER_TITLE_ICON_GAP), min.y));
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopWindowFontScale();
    ImGui::PopFont();

    const float side = ImGui::GetFrameHeight();
    const ImVec2 close_min(min.x + ImGui::GetContentRegionAvail().x - side, min.y + (line_height - side) * 0.5f);
    if(container_widgets::draw_icon_button({"##picker_close", ICON_MDI_CLOSE, "Close"}, ImRect(close_min, close_min + ImVec2(side, side))))
    {
        is_close_requested = true;
    }
}

struct tile_result
{
    bool is_pressed{};
    bool is_hovered{};
};

auto draw_tile(const tile_content& content, const ImVec2& size) -> tile_result
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    tile_result result{};
    result.is_pressed = ImGui::InvisibleButton("##tile", size);
    result.is_hovered = ImGui::IsItemHovered();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImRect rect(min, min + size);
    const float rounding = to_pixels(PICKER_TILE_ROUNDING);
    if(content.is_selected)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, get_accent_color(PICKER_SELECTED_FILL_ALPHA), rounding);
        draw_list->AddRect(rect.Min, rect.Max, get_accent_color(), rounding, 0, PICKER_TILE_BORDER);
    }
    else
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, result.is_hovered ? PICKER_TILE_HOVERED_COLOR : PICKER_TILE_COLOR, rounding);
    }

    const float padding = to_pixels(PICKER_TILE_PADDING);
    const float preview_side = size.x - padding * 2.0f;
    const ImRect preview_rect(min + ImVec2(padding, padding), min + ImVec2(padding + preview_side, padding + preview_side));
    draw_preview(draw_list, preview_rect, content.thumbnail, preview_side * PICKER_TILE_ICON_SHARE);

    const float text_y = preview_rect.Max.y + padding * 0.5f;
    const float text_width = ImGui::CalcTextSize(content.name.c_str()).x;
    const float text_x = rect.Min.x + std::max(padding, (size.x - text_width) * 0.5f);
    draw_clipped_text(draw_list,
                      ImVec2(text_x, text_y),
                      ImVec2(rect.Max.x - padding, text_y + ImGui::GetTextLineHeight()),
                      content.name.c_str(),
                      ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SetItemTooltipEx("%s", content.name.c_str());
    return result;
}
} // namespace

auto draw_field(const field_content& content) -> field_request
{
    field_request request{};
    const ImGuiStyle& style = ImGui::GetStyle();
    const float side = ImGui::GetFrameHeight() * 2.0f + style.ItemSpacing.y;

    ImGui::BeginGroup();
    const ImVec2 preview_min = ImGui::GetCursorScreenPos();
    // The thumbnail is the largest target of the field, so it opens the picker like the name
    // does; showing the asset in the content browser has its own button.
    const bool is_preview_pressed = ImGui::InvisibleButton("##thumbnail", ImVec2(side, side));
    const bool is_preview_hovered = ImGui::IsItemHovered();
    set_pick_tooltip(content);
    const ImRect preview_rect(preview_min, preview_min + ImVec2(side, side));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_preview(draw_list, preview_rect, content.thumbnail, ImGui::GetFontSize() * PICKER_EMPTY_ICON_SCALE);
    if(is_preview_hovered)
    {
        draw_list->AddRect(preview_rect.Min, preview_rect.Max, PICKER_PREVIEW_HOVERED_BORDER_COLOR, to_pixels(PICKER_PREVIEW_ROUNDING));
    }

    ImGui::SameLine();
    ImGui::BeginGroup();
    request.is_pick_pressed = draw_pick_field(content) || is_preview_pressed;
    draw_field_actions(content, request);
    ImGui::EndGroup();
    ImGui::EndGroup();
    return request;
}

void draw_drop_highlight(bool is_drop_possible)
{
    if(!is_drop_possible)
    {
        return;
    }
    const ImVec2 grow(PICKER_DROP_BORDER, PICKER_DROP_BORDER);
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin() - grow,
                                        ImGui::GetItemRectMax() + grow,
                                        get_accent_color(),
                                        to_pixels(PICKER_PREVIEW_ROUNDING),
                                        0,
                                        PICKER_DROP_BORDER);
}

auto begin_window(const char* popup_id, const char* icon, const std::string& title, ImGuiTextFilter& filter) -> bool
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(viewport->WorkSize * ImVec2(PICKER_WINDOW_SHARE_X, PICKER_WINDOW_SHARE_Y), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(to_pixels(PICKER_WINDOW_PADDING_X), to_pixels(PICKER_WINDOW_PADDING_Y)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, to_pixels(PICKER_WINDOW_ROUNDING));
    const bool is_open = ImGui::BeginPopupModal(popup_id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    // Only the window itself: tooltips opened from it keep the theme's padding.
    ImGui::PopStyleVar(2);
    if(!is_open)
    {
        return false;
    }

    bool is_close_requested = false;
    draw_window_header(icon, title, is_close_requested);
    // The search field is active from the start, so Escape closes the picker even while typing.
    if(is_close_requested || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::Dummy(ImVec2(0.0f, to_pixels(PICKER_GAP) * 0.5f));

    if(ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::PushStyleColor(ImGuiCol_FrameBg, PICKER_FIELD_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PICKER_FIELD_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, PICKER_FIELD_ACTIVE_COLOR);
    ImGui::DrawFilterWithHint(filter, ICON_MDI_MAGNIFY " Search", ImGui::GetContentRegionAvail().x);
    ImGui::PopStyleColor(3);
    ImGui::DrawItemActivityOutline();
    ImGui::Dummy(ImVec2(0.0f, to_pixels(PICKER_GAP) * 0.5f));
    return true;
}

void end_window()
{
    ImGui::EndPopup();
}

auto draw_grid(std::size_t count, const std::function<tile_content(std::size_t)>& get_tile) -> grid_result
{
    grid_result result{};
    const float footer_height = ImGui::GetFrameHeightWithSpacing() + to_pixels(PICKER_GAP);
    const float gap = to_pixels(PICKER_TILE_GAP);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(gap, gap));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, to_pixels(PICKER_TILE_ROUNDING));
    const bool is_visible = ImGui::BeginChild("##asset_grid", ImVec2(0.0f, -footer_height), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    if(!is_visible)
    {
        ImGui::EndChild();
        return result;
    }

    const float padding = to_pixels(PICKER_TILE_PADDING);
    const float tile_width = to_pixels(g_tile_size);
    const ImVec2 tile_size(tile_width, tile_width + padding * 0.5f + ImGui::GetTextLineHeight() + padding);
    const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + gap) / (tile_width + gap)));
    const int rows = static_cast<int>((count + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns));

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    ImGuiListClipper clipper;
    clipper.Begin(rows, tile_size.y + gap);
    while(clipper.Step())
    {
        for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            for(int column = 0; column < columns; ++column)
            {
                const std::size_t index = static_cast<std::size_t>(row * columns + column);
                if(index >= count)
                {
                    break;
                }
                if(column > 0)
                {
                    ImGui::SameLine();
                }
                ImGui::PushID(static_cast<int>(index));
                const tile_result tile = draw_tile(get_tile(index), tile_size);
                ImGui::PopID();
                if(tile.is_pressed)
                {
                    result.picked = static_cast<int>(index);
                }
                if(tile.is_hovered)
                {
                    result.hovered = static_cast<int>(index);
                }
            }
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    return result;
}

void draw_footer(const std::string& text)
{
    ImGui::Dummy(ImVec2(0.0f, to_pixels(PICKER_GAP) * 0.5f));
    const float slider_width = to_pixels(PICKER_SLIDER_WIDTH);
    const float icon_width = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float row_width = ImGui::GetContentRegionAvail().x;
    const float text_width = std::max(0.0f, row_width - slider_width - icon_width - ImGui::GetStyle().ItemSpacing.x);
    const ImVec2 text_size(text_width, ImGui::GetFrameHeight());
    ImGui::ItemSize(text_size);
    const float text_y = min.y + (text_size.y - ImGui::GetTextLineHeight()) * 0.5f;
    draw_clipped_text(ImGui::GetWindowDrawList(), ImVec2(min.x, text_y), min + text_size, text.c_str(), get_muted_color());

    ImGui::SameLine();
    const ImVec2 icon_min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(ImGui::GetFontSize(), ImGui::GetFrameHeight()));
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                              ImVec2(icon_min.x + ImGui::GetFontSize() * 0.5f, icon_min.y + ImGui::GetFrameHeight() * 0.5f),
                              ICON_MDI_IMAGE_SIZE_SELECT_LARGE,
                              get_muted_color());
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, PICKER_FIELD_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PICKER_FIELD_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, PICKER_FIELD_ACTIVE_COLOR);
    ImGui::SetNextItemWidth(slider_width);
    ImGui::SliderFloat("##tile_size", &g_tile_size, PICKER_TILE_MIN_SIZE, PICKER_TILE_MAX_SIZE, "");
    ImGui::PopStyleColor(3);
    ImGui::SetItemTooltipEx("%s", "Tile size");
}

} // namespace unravel::asset_picker
