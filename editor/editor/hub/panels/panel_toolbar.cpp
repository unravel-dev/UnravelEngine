#include "panel_toolbar.h"

#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>

namespace unravel::panel_toolbar
{
namespace
{
// Sizes are in units of the font size, so the bars follow the UI scale of the editor.
constexpr float BUTTON_HEIGHT = 1.7f;
constexpr float BUTTON_PADDING_X = 0.45f;
constexpr float BUTTON_ROUNDING = 0.3f;
constexpr float CARET_WIDTH = 0.6f;
// The caret half of a split button is a target of its own: it gets room on both sides.
constexpr float SPLIT_CARET_WIDTH = 1.15f;
constexpr float CARET_HALF_SPAN = 0.19f;
constexpr float CARET_HALF_RISE = 0.1f;
constexpr float CARET_THICKNESS = 0.09f;
constexpr float BAR_PADDING = 0.2f;
constexpr float BAR_ROUNDING = 0.5f;
constexpr float BAR_MARGIN = 0.5f;
constexpr float ROW_GAP = 0.35f;
constexpr float ITEM_GAP = 0.12f;
// After the caret half of a split button, so the pair reads as one control.
constexpr float SPLIT_BUTTON_GAP = 0.3f;
constexpr float SEPARATOR_MARGIN = 0.32f;
constexpr float SEPARATOR_HEIGHT = 0.5f;
constexpr float POPUP_GAP = 0.3f;
constexpr float POPUP_PADDING_X = 0.75f;
constexpr float POPUP_PADDING_Y = 0.6f;
constexpr float POPUP_ROUNDING = 0.45f;
constexpr float POPUP_ITEM_SPACING_X = 0.5f;
constexpr float POPUP_ITEM_SPACING_Y = 0.4f;

// The bar is the window background pulled towards black: dark enough to carry white icons over
// a bright sky, translucent enough to stay part of the image.
constexpr float BAR_BG_DARKEN = 0.4f;
constexpr float BAR_BG_ALPHA = 0.9f;
constexpr ImU32 BAR_BORDER_COLOR = IM_COL32(255, 255, 255, 26);
constexpr ImU32 SEPARATOR_COLOR = IM_COL32(255, 255, 255, 30);
constexpr ImU32 ITEM_HOVERED_COLOR = IM_COL32(255, 255, 255, 26);
constexpr ImU32 ITEM_HELD_COLOR = IM_COL32(255, 255, 255, 46);
constexpr float ITEM_TEXT_ALPHA = 0.86f;
constexpr float FILTER_OFF_TEXT_ALPHA = 0.35f;
constexpr ImU32 FILTER_ON_FILL_COLOR = IM_COL32(255, 255, 255, 18);
// A strip sits on the panel, not on an image: it is opaque, and only a little darker.
constexpr float STRIP_BG_DARKEN = 0.22f;
constexpr ImU32 FIELD_BG_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 FIELD_BG_HOVERED_COLOR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 FIELD_BG_ACTIVE_COLOR = IM_COL32(255, 255, 255, 30);
constexpr float CARET_ALPHA = 0.6f;
// Text on the accent flips to black once the accent gets this bright.
constexpr float ACCENT_LIGHT_LUMINANCE = 0.62f;
// A soft drop shadow out of a few growing rings, to lift the bar off the image.
constexpr int SHADOW_RINGS = 4;
constexpr float SHADOW_OFFSET_Y = 0.12f;
constexpr int SHADOW_RING_ALPHA = 22;

constexpr const char* CENTER_GROUP_WIDTH_ID = "##center_group_width";
constexpr const char* RIGHT_GROUP_WIDTH_ID = "##right_group_width";

constexpr int BAR_STYLE_VARS = 4;
constexpr int BAR_STYLE_COLORS = 2;
constexpr int STRIP_STYLE_VARS = 3;
constexpr int STRIP_STYLE_COLORS = 2;
constexpr int FIELD_STYLE_VARS = 2;
constexpr int FIELD_STYLE_COLORS = 3;
constexpr int POPUP_STYLE_VARS = 3;

struct bar_state
{
    /// Window the bar floats in; it owns the measured width and gets the focus back.
    ImGuiWindow* host{};
    ImGuiID width_key{};
    bar_anchor anchor{bar_anchor::left};
    bool has_items{};
    bool is_hidden{};
    /// The item before was the caret half of a split button.
    bool is_after_split_button{};
    /// Strip only: where the open aligned group starts, and the key its width is kept under.
    float aligned_group_start_x{};
    ImGuiID aligned_group_width_key{};
    /// Strip only: where the line had got to when align_right() took over.
    float left_group_end_x{};
    bool is_in_right_group{};
};

struct dropdown_state
{
    ImGuiID open_id{};
    /// Window that hosts the bar of the open dropdown.
    ImGuiID open_host_id{};
    int open_frame{-1};
};

struct item_desc
{
    /// Icon and / or label. Null draws a caret alone.
    const char* text{};
    /// Text the width is measured from instead, null for the text itself.
    const char* width_text{};
    bool has_caret{};
    /// Filled with the accent, or with active_color.
    bool is_active{};
    /// 0 keeps the theme accent.
    ImU32 active_color{};
    /// Filter look: is_active shows the text color on a soft fill, otherwise the text is dimmed.
    bool is_filter{};
    /// 0 keeps the default.
    ImU32 text_color{};
    ImGuiButtonFlags button_flags{};
};

bar_state g_bar{};
dropdown_state g_dropdown{};

auto to_pixels(float font_units) -> float
{
    return ImFloor(ImGui::GetFontSize() * font_units);
}

auto get_bar_height() -> float
{
    return to_pixels(BUTTON_HEIGHT) + 2.0f * to_pixels(BAR_PADDING);
}

auto get_accent_color() -> ImVec4
{
    return ImGui::GetStyleColorVec4(ImGuiCol_TabSelected);
}

auto get_text_color_on(ImU32 fill_color) -> ImU32
{
    const ImVec4 fill = ImGui::ColorConvertU32ToFloat4(fill_color);
    const float luminance = 0.299f * fill.x + 0.587f * fill.y + 0.114f * fill.z;
    return luminance > ACCENT_LIGHT_LUMINANCE ? IM_COL32(0, 0, 0, 255) : IM_COL32(255, 255, 255, 255);
}

auto get_bar_bg_color() -> ImVec4
{
    const ImVec4 window_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const float keep = 1.0f - BAR_BG_DARKEN;
    return ImVec4(window_bg.x * keep, window_bg.y * keep, window_bg.z * keep, BAR_BG_ALPHA);
}

auto calc_bar_position(const bar_placement& placement, float bar_width) -> ImVec2
{
    const float margin = to_pixels(BAR_MARGIN);
    const float row_stride = get_bar_height() + to_pixels(ROW_GAP);
    const float y = placement.area.Min.y + margin + static_cast<float>(placement.row) * row_stride;
    switch(placement.anchor)
    {
        case bar_anchor::center:
            return ImVec2(ImFloor(placement.area.GetCenter().x - bar_width * 0.5f), y);
        case bar_anchor::right:
            return ImVec2(placement.area.Max.x - margin - bar_width, y);
        case bar_anchor::left:
        default:
            return ImVec2(placement.area.Min.x + margin, y);
    }
}

void draw_bar_shadow(ImDrawList* draw_list, const ImRect& bar_rect, float alpha)
{
    const float rounding = to_pixels(BAR_ROUNDING);
    const ImVec2 offset(0.0f, to_pixels(SHADOW_OFFSET_Y));
    const int ring_alpha = static_cast<int>(static_cast<float>(SHADOW_RING_ALPHA) * alpha);
    for(int ring = 1; ring <= SHADOW_RINGS; ++ring)
    {
        const float grow = static_cast<float>(ring);
        const ImVec2 expand(grow, grow);
        draw_list->AddRectFilled(bar_rect.Min - expand + offset,
                                 bar_rect.Max + expand + offset,
                                 IM_COL32(0, 0, 0, ring_alpha),
                                 rounding + grow);
    }
}

void add_click_blocker(ImGuiWindow* bar)
{
    if(bar->Size.x <= 0.0f || bar->Size.y <= 0.0f)
    {
        return;
    }
    // The manipulation gizmo yields only to a hovered ITEM, and the padding between the buttons
    // has none. Submitted last, the blocker never takes the hover from a button. It must not
    // count as content: a window-sized item would keep the auto-sized bar from ever shrinking.
    const ImVec2 content_max = bar->DC.CursorMaxPos;
    ImGui::SetCursorScreenPos(bar->Pos);
    ImGui::InvisibleButton("##click_blocker", bar->Size);
    bar->DC.CursorMaxPos = content_max;
}

void begin_item(float gap)
{
    if(g_bar.has_items)
    {
        ImGui::SameLine(0.0f, gap);
    }
    g_bar.has_items = true;
}

auto calc_item_size(const item_desc& desc) -> ImVec2
{
    const float height = to_pixels(BUTTON_HEIGHT);
    if(desc.text == nullptr)
    {
        return ImVec2(to_pixels(SPLIT_CARET_WIDTH), height);
    }
    const float caret_width = desc.has_caret ? to_pixels(CARET_WIDTH) : 0.0f;
    const char* measured_text = desc.width_text != nullptr ? desc.width_text : desc.text;
    const float text_width = ImGui::CalcTextSize(measured_text, nullptr, true).x;
    const float padded_width = text_width + 2.0f * to_pixels(BUTTON_PADDING_X);
    // An icon alone makes a square button.
    return ImVec2(ImMax(padded_width, height) + caret_width, height);
}

void draw_caret(ImDrawList* draw_list, const ImVec2& center, ImU32 color)
{
    const float half_span = ImGui::GetFontSize() * CARET_HALF_SPAN;
    const float half_rise = ImGui::GetFontSize() * CARET_HALF_RISE;
    const ImVec2 points[3] = {ImVec2(center.x - half_span, center.y - half_rise),
                              ImVec2(center.x, center.y + half_rise),
                              ImVec2(center.x + half_span, center.y - half_rise)};
    draw_list->AddPolyline(points, 3, color, ImDrawFlags_None, ImGui::GetFontSize() * CARET_THICKNESS);
}

/// The caret turned to point right.
void draw_path_chevron(ImDrawList* draw_list, const ImVec2& center, ImU32 color)
{
    const float half_span = ImGui::GetFontSize() * CARET_HALF_SPAN;
    const float half_rise = ImGui::GetFontSize() * CARET_HALF_RISE;
    const ImVec2 points[3] = {ImVec2(center.x - half_rise, center.y - half_span),
                              ImVec2(center.x + half_rise, center.y),
                              ImVec2(center.x - half_rise, center.y + half_span)};
    draw_list->AddPolyline(points, 3, color, ImDrawFlags_None, ImGui::GetFontSize() * CARET_THICKNESS);
}

auto calc_active_fill_color(const item_desc& desc) -> ImU32
{
    if(desc.is_filter)
    {
        return FILTER_ON_FILL_COLOR;
    }
    if(desc.active_color != 0)
    {
        return ImGui::GetColorU32(ImGui::ColorConvertU32ToFloat4(desc.active_color));
    }
    return ImGui::GetColorU32(get_accent_color());
}

auto calc_item_text_color(const item_desc& desc, bool is_hovered) -> ImU32
{
    if(desc.is_filter)
    {
        const ImVec4 color = ImGui::ColorConvertU32ToFloat4(desc.text_color);
        const float alpha = desc.is_active ? 1.0f : FILTER_OFF_TEXT_ALPHA;
        return ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
    }
    if(desc.is_active)
    {
        return get_text_color_on(calc_active_fill_color(desc));
    }
    if(desc.text_color != 0)
    {
        return desc.text_color;
    }
    return ImGui::GetColorU32(ImGuiCol_Text, is_hovered ? 1.0f : ITEM_TEXT_ALPHA);
}

auto get_strip_bg_color() -> ImVec4
{
    const ImVec4 window_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const float keep = 1.0f - STRIP_BG_DARKEN;
    return ImVec4(window_bg.x * keep, window_bg.y * keep, window_bg.z * keep, 1.0f);
}

void store_aligned_group_width()
{
    if(g_bar.aligned_group_width_key == 0)
    {
        return;
    }
    ImGuiWindow* strip = ImGui::GetCurrentWindow();
    const float width = strip->DC.CursorMaxPos.x - g_bar.aligned_group_start_x;
    strip->StateStorage.SetFloat(g_bar.aligned_group_width_key, width);
}

/// Closes the aligned group before, if any, and starts one at alignment (0.5 center, 1 right) by
/// the width it measured the frame before.
void begin_aligned_group(const char* width_id, float alignment)
{
    ImGuiWindow* strip = ImGui::GetCurrentWindow();
    store_aligned_group_width();
    begin_item(to_pixels(ITEM_GAP));
    g_bar.aligned_group_width_key = strip->GetID(width_id);
    const float group_width = strip->StateStorage.GetFloat(g_bar.aligned_group_width_key, 0.0f);
    const float aligned_x = ImFloor(strip->WorkRect.Min.x + (strip->WorkRect.GetWidth() - group_width) * alignment);
    g_bar.left_group_end_x = strip->DC.CursorPos.x;
    // Never to the left of what is already on the line: a narrow host clips the group instead.
    g_bar.aligned_group_start_x = ImMax(g_bar.left_group_end_x, aligned_x);
    strip->DC.CursorPos.x = g_bar.aligned_group_start_x;
    g_bar.has_items = false;
}

auto calc_caret_width(const item_desc& desc, const ImRect& bb) -> float
{
    if(!desc.has_caret)
    {
        return 0.0f;
    }
    // Alone, the caret has the whole item to sit in the middle of.
    return desc.text != nullptr ? to_pixels(CARET_WIDTH) : bb.GetWidth();
}

void draw_item(const ImRect& bb, const item_desc& desc, bool is_hovered, bool is_held)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float rounding = to_pixels(BUTTON_ROUNDING);
    if(desc.is_active)
    {
        draw_list->AddRectFilled(bb.Min, bb.Max, calc_active_fill_color(desc), rounding);
    }
    if(is_hovered || is_held)
    {
        draw_list->AddRectFilled(bb.Min, bb.Max, is_held ? ITEM_HELD_COLOR : ITEM_HOVERED_COLOR, rounding);
    }
    const ImU32 text_color = calc_item_text_color(desc, is_hovered);
    const float caret_width = calc_caret_width(desc, bb);
    if(desc.text != nullptr)
    {
        const char* text_end = ImGui::FindRenderedTextEnd(desc.text);
        const ImVec2 text_size = ImGui::CalcTextSize(desc.text, text_end);
        const float text_area_width = bb.GetWidth() - caret_width;
        const ImVec2 text_pos(ImFloor(bb.Min.x + (text_area_width - text_size.x) * 0.5f),
                              ImFloor(bb.Min.y + (bb.GetHeight() - text_size.y) * 0.5f));
        draw_list->AddText(text_pos, text_color, desc.text, text_end);
    }
    if(desc.has_caret)
    {
        const ImU32 caret_color = ImGui::GetColorU32(ImGuiCol_Text, is_hovered ? 1.0f : CARET_ALPHA);
        // Next to a label the caret leans towards it; alone it sits in the middle.
        const float lean = desc.text != nullptr ? to_pixels(BUTTON_PADDING_X) * 0.4f : 0.0f;
        const ImVec2 center(bb.Max.x - caret_width * 0.5f - lean, bb.GetCenter().y);
        draw_caret(draw_list, center, desc.is_active ? text_color : caret_color);
    }
}

auto add_item(const char* id, const item_desc& desc, ImRect* out_bb) -> bool
{
    // A caret alone is the second half of a split button.
    const bool is_split_caret = desc.text == nullptr;
    begin_item(g_bar.is_after_split_button ? to_pixels(SPLIT_BUTTON_GAP) : to_pixels(ITEM_GAP));
    g_bar.is_after_split_button = is_split_caret;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window->SkipItems)
    {
        return false;
    }
    const ImGuiID item_id = window->GetID(id);
    const ImVec2 size = calc_item_size(desc);
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
    if(out_bb != nullptr)
    {
        *out_bb = bb;
    }
    ImGui::ItemSize(size);
    if(!ImGui::ItemAdd(bb, item_id))
    {
        return false;
    }
    bool is_hovered = false;
    bool is_held = false;
    const bool is_pressed = ImGui::ButtonBehavior(bb, item_id, &is_hovered, &is_held, desc.button_flags);
    draw_item(bb, desc, is_hovered, is_held);
    return is_pressed;
}

void set_item_tooltip(const char* tooltip)
{
    if(tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", tooltip);
    }
}

auto is_other_dropdown_open(ImGuiID item_id) -> bool
{
    const bool is_recent = g_dropdown.open_frame >= ImGui::GetFrameCount() - 1;
    return is_recent && g_dropdown.open_id != 0 && g_dropdown.open_id != item_id;
}

void set_next_popup_placement(const ImRect& item_bb)
{
    const ImGuiWindow* bar = ImGui::GetCurrentWindow();
    const float y = bar->Pos.y + bar->Size.y + to_pixels(POPUP_GAP);
    // The popup hangs from the edge of its button that faces the viewport edge, so it opens
    // towards the middle and never off the side.
    if(g_bar.anchor == bar_anchor::right)
    {
        ImGui::SetNextWindowPos(ImVec2(item_bb.Max.x, y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(item_bb.Min.x, y), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
}

void push_popup_style()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(to_pixels(POPUP_PADDING_X), to_pixels(POPUP_PADDING_Y)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, to_pixels(POPUP_ROUNDING));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(to_pixels(POPUP_ITEM_SPACING_X), to_pixels(POPUP_ITEM_SPACING_Y)));
}

} // namespace

auto get_rows_extent(int row_count) -> float
{
    if(row_count <= 0)
    {
        return 0.0f;
    }
    const float rows = static_cast<float>(row_count);
    return to_pixels(BAR_MARGIN) + rows * get_bar_height() + (rows - 1.0f) * to_pixels(ROW_GAP);
}

auto get_bar_margin() -> float
{
    return to_pixels(BAR_MARGIN);
}

void update_layout(layout_state& state, float bars_width, int bar_count, float area_width)
{
    // The bars were measured in the frame before, in the layout that frame used.
    if(bars_width > 0.0f)
    {
        float& measured_width = state.is_compact ? state.compact_width : state.full_width;
        measured_width = bars_width;
    }
    // A margin to either edge and one between neighbours.
    const float spacing = static_cast<float>(bar_count + 1) * get_bar_margin();
    const auto fits = [&](float width) -> bool
    {
        return width <= 0.0f || area_width >= width + spacing;
    };
    state.is_compact = !fits(state.full_width);
    state.is_stacked = state.is_compact && !fits(state.compact_width);
}

auto make_text(const char* icon, const char* name, bool is_compact) -> std::string
{
    if(is_compact)
    {
        return icon;
    }
    return std::string(icon) + " " + name;
}

auto begin_bar(const char* id, const bar_placement& placement) -> bool
{
    ImGuiWindow* host = ImGui::GetCurrentWindow();
    g_bar = {};
    g_bar.host = host;
    g_bar.width_key = host->GetID(id);
    g_bar.anchor = placement.anchor;
    const float measured_width = host->StateStorage.GetFloat(g_bar.width_key, 0.0f);
    g_bar.is_hidden = placement.anchor != bar_anchor::left && measured_width <= 0.0f;
    const ImVec2 pos = calc_bar_position(placement, measured_width);
    if(measured_width > 0.0f)
    {
        draw_bar_shadow(host->DrawList, ImRect(pos, pos + ImVec2(measured_width, get_bar_height())), placement.alpha);
    }
    ImGui::SetCursorScreenPos(pos);
    const float bar_padding = to_pixels(BAR_PADDING);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_bar.is_hidden ? 0.0f : ImGui::GetStyle().Alpha * placement.alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, to_pixels(BAR_ROUNDING));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(bar_padding, bar_padding));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, get_bar_bg_color());
    ImGui::PushStyleColor(ImGuiCol_Border, BAR_BORDER_COLOR);
    const ImGuiChildFlags child_flags = ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeX |
                                        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize |
                                        ImGuiChildFlags_Borders;
    const ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
    return ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), child_flags, window_flags);
}

void end_bar()
{
    add_click_blocker(ImGui::GetCurrentWindow());
    // A click leaves the keyboard focus on the bar, where the viewport shortcuts do not listen.
    // An open popup is a window of its own, so this never pulls the focus out from under one.
    const bool is_holding_focus = ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive();
    ImGui::EndChild();
    ImGui::PopStyleColor(BAR_STYLE_COLORS);
    ImGui::PopStyleVar(BAR_STYLE_VARS);
    g_bar.host->StateStorage.SetFloat(g_bar.width_key, ImGui::GetItemRectSize().x);
    if(is_holding_focus)
    {
        ImGui::SetWindowFocus();
    }
}

void draw_readout(const bar_placement& placement, const char* text, const char* width_text)
{
    ImGuiWindow* host = ImGui::GetCurrentWindow();
    if(host->SkipItems || placement.alpha <= 0.0f)
    {
        return;
    }
    item_desc desc{};
    desc.text = text;
    desc.width_text = width_text;
    const ImVec2 item_size = calc_item_size(desc);
    const float bar_padding = to_pixels(BAR_PADDING);
    const ImVec2 bar_size(item_size.x + 2.0f * bar_padding, get_bar_height());
    const ImVec2 pos = calc_bar_position(placement, bar_size.x);
    const ImRect bar_rect(pos, pos + bar_size);
    const float rounding = to_pixels(BAR_ROUNDING);
    const float alpha = placement.alpha;
    ImVec4 bg_color = get_bar_bg_color();
    bg_color.w *= alpha;
    ImVec4 border_color = ImGui::ColorConvertU32ToFloat4(BAR_BORDER_COLOR);
    border_color.w *= alpha;
    draw_bar_shadow(host->DrawList, bar_rect, alpha);
    host->DrawList->AddRectFilled(bar_rect.Min, bar_rect.Max, ImGui::GetColorU32(bg_color), rounding);
    host->DrawList->AddRect(bar_rect.Min, bar_rect.Max, ImGui::GetColorU32(border_color), rounding);
    const char* text_end = ImGui::FindRenderedTextEnd(text);
    const ImVec2 text_size = ImGui::CalcTextSize(text, text_end);
    const ImVec2 text_pos(ImFloor(bar_rect.Min.x + (bar_size.x - text_size.x) * 0.5f),
                          ImFloor(bar_rect.Min.y + (bar_size.y - text_size.y) * 0.5f));
    host->DrawList->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text, ITEM_TEXT_ALPHA * alpha), text, text_end);
}

auto get_bar_width(const char* id) -> float
{
    ImGuiWindow* host = ImGui::GetCurrentWindow();
    return host->StateStorage.GetFloat(host->GetID(id), 0.0f);
}

auto is_dropdown_open() -> bool
{
    const bool is_recent = g_dropdown.open_frame >= ImGui::GetFrameCount() - 1;
    return is_recent && g_dropdown.open_host_id == ImGui::GetCurrentWindow()->ID;
}

auto get_strip_height() -> float
{
    return get_bar_height();
}

auto begin_strip(const char* id, strip_style style) -> bool
{
    g_bar = {};
    g_bar.host = ImGui::GetCurrentWindow();
    const bool is_card = style == strip_style::card;
    const float padding_y = to_pixels(BAR_PADDING);
    // A flat strip reaches the edges of its host, so its items keep the margin a bar keeps.
    const float padding_x = is_card ? padding_y : to_pixels(BAR_MARGIN);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, is_card ? to_pixels(BAR_ROUNDING) : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, is_card ? 1.0f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding_x, padding_y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, is_card ? get_strip_bg_color() : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, BAR_BORDER_COLOR);
    const ImGuiChildFlags child_flags = ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders;
    const ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
    return ImGui::BeginChild(id, ImVec2(0.0f, get_strip_height()), child_flags, window_flags);
}

void end_strip()
{
    store_aligned_group_width();
    ImGui::EndChild();
    ImGui::PopStyleColor(STRIP_STYLE_COLORS);
    ImGui::PopStyleVar(STRIP_STYLE_VARS);
}

void align_center()
{
    begin_aligned_group(CENTER_GROUP_WIDTH_ID, 0.5f);
}

void align_right()
{
    // Popups of the group open towards the middle of the strip, like those of a right anchored bar.
    g_bar.anchor = bar_anchor::right;
    g_bar.is_in_right_group = true;
    begin_aligned_group(RIGHT_GROUP_WIDTH_ID, 1.0f);
}

void begin_group()
{
    begin_item(to_pixels(ITEM_GAP));
    g_bar.is_after_split_button = false;
    ImGui::BeginGroup();
    g_bar.has_items = false;
}

void end_group()
{
    ImGui::EndGroup();
    g_bar.has_items = true;
}

void begin_field(float width)
{
    begin_item(to_pixels(ITEM_GAP));
    g_bar.is_after_split_button = false;
    const float padding_y = (to_pixels(BUTTON_HEIGHT) - ImGui::GetFontSize()) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(to_pixels(BUTTON_PADDING_X), padding_y));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(BUTTON_ROUNDING));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, FIELD_BG_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, FIELD_BG_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, FIELD_BG_ACTIVE_COLOR);
    ImGui::SetNextItemWidth(width);
}

void end_field()
{
    ImGui::PopStyleColor(FIELD_STYLE_COLORS);
    ImGui::PopStyleVar(FIELD_STYLE_VARS);
}

auto calc_flexible_width(float min_width, float max_width) -> float
{
    ImGuiWindow* strip = ImGui::GetCurrentWindow();
    const float gap = to_pixels(ITEM_GAP);
    if(g_bar.is_in_right_group)
    {
        return ImClamp(strip->WorkRect.Max.x - g_bar.left_group_end_x, min_width, max_width);
    }
    const float field_x = g_bar.has_items ? strip->DC.CursorPosPrevLine.x + gap : strip->DC.CursorPos.x;
    const float right_group_width = strip->StateStorage.GetFloat(strip->GetID(RIGHT_GROUP_WIDTH_ID), 0.0f);
    const float free_width = strip->WorkRect.Max.x - field_x - right_group_width - gap;
    return ImClamp(free_width, min_width, max_width);
}

void separator()
{
    begin_item(0.0f);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window->SkipItems)
    {
        return;
    }
    const float height = to_pixels(BUTTON_HEIGHT);
    const float margin = to_pixels(SEPARATOR_MARGIN);
    const ImVec2 size(2.0f * margin + 1.0f, height);
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
    ImGui::ItemSize(size);
    if(!ImGui::ItemAdd(bb, 0))
    {
        return;
    }
    const float line_inset = height * (1.0f - SEPARATOR_HEIGHT) * 0.5f;
    const float x = ImFloor(bb.Min.x + margin) + 0.5f;
    window->DrawList->AddLine(ImVec2(x, bb.Min.y + line_inset), ImVec2(x, bb.Max.y - line_inset), SEPARATOR_COLOR);
}

void path_separator()
{
    begin_item(0.0f);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window->SkipItems)
    {
        return;
    }
    const ImVec2 size(to_pixels(CARET_WIDTH), to_pixels(BUTTON_HEIGHT));
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
    ImGui::ItemSize(size);
    if(!ImGui::ItemAdd(bb, 0))
    {
        return;
    }
    draw_path_chevron(window->DrawList, bb.GetCenter(), ImGui::GetColorU32(ImGuiCol_Text, CARET_ALPHA));
}

void label(const char* text)
{
    begin_item(to_pixels(ITEM_GAP));
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window->SkipItems)
    {
        return;
    }
    const ImVec2 text_size = ImGui::CalcTextSize(text, nullptr, true);
    const ImVec2 size(text_size.x + 2.0f * to_pixels(BUTTON_PADDING_X), to_pixels(BUTTON_HEIGHT));
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
    ImGui::ItemSize(size);
    if(!ImGui::ItemAdd(bb, 0))
    {
        return;
    }
    const ImVec2 text_pos(ImFloor(bb.Min.x + to_pixels(BUTTON_PADDING_X)),
                          ImFloor(bb.Min.y + (size.y - text_size.y) * 0.5f));
    window->DrawList->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), text, ImGui::FindRenderedTextEnd(text));
}

auto button(const char* id, const char* text, const char* tooltip, bool is_accented, const char* width_text) -> bool
{
    item_desc desc{};
    desc.text = text;
    desc.width_text = width_text;
    desc.is_active = is_accented;
    const bool is_pressed = add_item(id, desc, nullptr);
    set_item_tooltip(tooltip);
    return is_pressed;
}

auto toggle(const char* id,
            const char* text,
            bool is_active,
            const char* tooltip,
            const char* width_text,
            ImU32 active_color) -> bool
{
    item_desc desc{};
    desc.text = text;
    desc.width_text = width_text;
    desc.is_active = is_active;
    desc.active_color = active_color;
    const bool is_pressed = add_item(id, desc, nullptr);
    set_item_tooltip(tooltip);
    return is_pressed;
}

auto filter_toggle(const char* id,
                   const char* text,
                   bool is_active,
                   ImU32 color,
                   const char* tooltip,
                   const char* width_text) -> bool
{
    item_desc desc{};
    desc.text = text;
    desc.width_text = width_text;
    desc.is_active = is_active;
    desc.is_filter = true;
    desc.text_color = color;
    const bool is_pressed = add_item(id, desc, nullptr);
    set_item_tooltip(tooltip);
    return is_pressed;
}

auto begin_dropdown(const char* id, const char* text, const char* tooltip, ImU32 text_color) -> bool
{
    item_desc desc{};
    desc.text = text;
    desc.has_caret = true;
    desc.text_color = text_color;
    // A menu opens on the press, not on the release.
    desc.button_flags = ImGuiButtonFlags_PressedOnClick;
    // A size the caller set for the popup must not end up on the tooltip of the button, which is
    // the next window to begin while the button is hovered.
    ImGuiContext& context = *ImGui::GetCurrentContext();
    const ImGuiNextWindowData popup_window_data = context.NextWindowData;
    context.NextWindowData.ClearFlags();
    ImRect item_bb{};
    const bool is_pressed = add_item(id, desc, &item_bb);
    set_item_tooltip(tooltip);
    context.NextWindowData = popup_window_data;
    const ImGuiID item_id = ImGui::GetID(id);
    ImGui::PushID(id);
    const char* popup_name = "##dropdown";
    // The open popup blocks the hover of every other window, so the switch between dropdowns
    // tests the rectangle itself.
    const bool is_switch_hover = is_other_dropdown_open(item_id) && ImGui::IsMouseHoveringRect(item_bb.Min, item_bb.Max);
    if(is_pressed || (is_switch_hover && !ImGui::IsPopupOpen(popup_name)))
    {
        ImGui::OpenPopup(popup_name);
    }
    set_next_popup_placement(item_bb);
    ImGui::SetNextWindowViewportToCurrent();
    push_popup_style();
    const bool is_open = ImGui::BeginPopup(popup_name, ImGuiWindowFlags_NoMove);
    if(!is_open)
    {
        ImGui::PopStyleVar(POPUP_STYLE_VARS);
        ImGui::PopID();
        return false;
    }
    g_dropdown.open_id = item_id;
    g_dropdown.open_host_id = g_bar.host->ID;
    g_dropdown.open_frame = ImGui::GetFrameCount();
    return true;
}

void end_dropdown()
{
    ImGui::EndPopup();
    ImGui::PopStyleVar(POPUP_STYLE_VARS);
    ImGui::PopID();
}

} // namespace unravel::panel_toolbar
