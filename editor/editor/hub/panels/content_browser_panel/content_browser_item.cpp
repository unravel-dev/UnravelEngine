#include "content_browser_item.h"

#include <editor/imgui/integration/imgui.h>
#include <engine/assets/impl/asset_extensions.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/utils.h>

#include <algorithm>
#include <array>

namespace unravel
{
// Unity builds merge the anonymous namespaces of the editor's sources; the helpers' own namespace keeps their
// generic names from clashing.
namespace content_card
{
namespace
{
constexpr float CARD_ROUNDING = 6.0f;
constexpr float THUMBNAIL_INSET = 4.0f;
constexpr float THUMBNAIL_ROUNDING = 4.0f;
constexpr float ACCENT_STRIP_HEIGHT = 2.0f;
constexpr float TEXT_PADDING = 6.0f;
constexpr float PAD_ACCENT_TO_NAME = 5.0f;
constexpr float PAD_NAME_TO_CAPTION = 1.0f;
constexpr float PAD_CAPTION_TO_BOTTOM = 6.0f;
/// The caption row stays secondary to the name: smaller, and dimmer than regular text.
constexpr float CAPTION_FONT_SCALE = 0.85f;
constexpr float CAPTION_TEXT_BLEND = 0.35f;
/// The summary badge sits in the thumbnail's bottom-right corner; it is dropped when the card is too narrow.
constexpr float BADGE_FONT_SCALE = 0.75f;
constexpr float BADGE_MARGIN = 3.0f;
constexpr float BADGE_PADDING_X = 4.0f;
constexpr float BADGE_PADDING_Y = 1.0f;
constexpr float BADGE_ROUNDING = 3.0f;
constexpr ImU32 BADGE_BACKGROUND = IM_COL32(0, 0, 0, 165);
constexpr ImU32 BADGE_TEXT_COLOR = IM_COL32(235, 236, 240, 255);
constexpr float SELECTED_RING_THICKNESS = 1.5f;
constexpr float FOCUSED_RING_THICKNESS = 2.0f;
constexpr float HOVERED_RING_THICKNESS = 1.0f;
constexpr int HOVERED_RING_ALPHA = 170;
constexpr float SPINNER_RADIUS_FRACTION = 0.18f;
constexpr float SPINNER_THICKNESS = 3.0f;
/// Radians per second.
constexpr float SPINNER_SPEED = 6.0f;
constexpr float SPINNER_ARC = IM_PI * 1.5f;
constexpr int SPINNER_SEGMENTS = 24;

constexpr ImU32 WELL_COLOR = IM_COL32(0, 0, 0, 56);
constexpr ImU32 TILE_COLOR = IM_COL32(255, 255, 255, 12);
constexpr ImU32 TILE_HOVERED_COLOR = IM_COL32(255, 255, 255, 22);
constexpr ImU32 TILE_HELD_COLOR = IM_COL32(255, 255, 255, 34);
constexpr ImU32 FOCUSED_RING_COLOR = IM_COL32(255, 255, 0, 255);
constexpr ImU32 FALLBACK_ACCENT_COLOR = IM_COL32(150, 150, 158, 255);
/// Near-white and theme-independent, so the tooltip's type label never picks up an off-palette tint.
constexpr ImU32 TOOLTIP_TYPE_COLOR = IM_COL32(224, 226, 231, 255);

constexpr float TOOLTIP_ROUNDING = 10.0f;
constexpr float TOOLTIP_PADDING_X = 14.0f;
constexpr float TOOLTIP_PADDING_Y = 12.0f;
constexpr float TOOLTIP_BORDER_SIZE = 1.0f;
constexpr float TOOLTIP_ITEM_SPACING_X = 10.0f;
constexpr float TOOLTIP_ITEM_SPACING_Y = 4.0f;
constexpr float TOOLTIP_SECTION_RULE_THICKNESS = 1.0f;
constexpr float TOOLTIP_SECTION_TITLE_PADDING_Y = 2.0f;
constexpr float TOOLTIP_THUMBNAIL_SIDE = 72.0f;
constexpr float TOOLTIP_THUMBNAIL_ROUNDING = 8.0f;
constexpr ImU32 TOOLTIP_THUMBNAIL_BACKGROUND = IM_COL32(0, 0, 0, 52);
constexpr float TOOLTIP_WRAP_WIDTH = 360.0f;
constexpr float TOOLTIP_LABEL_GAP = 16.0f;
constexpr float TOOLTIP_TYPE_FONT_SCALE = 0.9f;
constexpr float TOOLTIP_SECTION_FONT_SCALE = 0.9f;
constexpr float TOOLTIP_BACKGROUND_LIFT = 0.035f;
constexpr float TOOLTIP_BORDER_CONTRAST = 1.35f;
constexpr float PREVIEW_SCALE = 2.75f;
constexpr float PREVIEW_MIN_GROWTH = 16.0f;
constexpr float PREVIEW_MAX_SIDE = 384.0f;

struct type_accent
{
    const char* type;
    ImU32 color;
};

/// A distinct color per asset type, so cards read at a glance like the type strip Unreal draws under thumbnails.
constexpr std::array<type_accent, 13> TYPE_ACCENTS{{
    {"Texture", IM_COL32(226, 96, 92, 255)},
    {"Material", IM_COL32(86, 180, 168, 255)},
    {"Physics Material", IM_COL32(214, 124, 72, 255)},
    {"Mesh", IM_COL32(234, 138, 64, 255)},
    {"Shader", IM_COL32(156, 116, 222, 255)},
    {"Prefab", IM_COL32(82, 179, 222, 255)},
    {"Scene", IM_COL32(232, 168, 70, 255)},
    {"Animation Clip", IM_COL32(124, 200, 96, 255)},
    {"Audio Clip", IM_COL32(220, 112, 178, 255)},
    {"Script", IM_COL32(94, 172, 206, 255)},
    {"Font", IM_COL32(186, 186, 196, 255)},
    {"UI Tree", IM_COL32(126, 138, 224, 255)},
    {"Style Sheet", IM_COL32(170, 134, 224, 255)},
}};

/// Fonts and sizes of a card's text rows.
struct card_fonts
{
    ImFont* name{};
    ImFont* type{};
    /// Unscaled sizes, as PushFont() takes them.
    float name_base_size{};
    float caption_base_size{};
    /// Sizes in pixels after the DPI and global font scales, for layout and draw-list text.
    float name_size{};
    float caption_size{};
};

/// Screen rectangles of a card's parts.
struct card_layout
{
    ImRect card;
    ImRect well;
    ImRect accent;
    ImVec2 name_pos;
    ImVec2 caption_pos;
    float text_max_x{};
};

/// Pointer state of a card this frame.
struct card_state
{
    bool is_hovered{};
    bool is_held{};
};

/// Rounded, slightly lifted window style shared by the details and the preview tooltips.
struct tooltip_style_scope
{
    static constexpr int STYLE_VAR_COUNT = 4;
    static constexpr int STYLE_COLOR_COUNT = 2;

    /// Pushes the tooltip style; it applies to the next window begun.
    tooltip_style_scope()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, TOOLTIP_ROUNDING);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(TOOLTIP_PADDING_X, TOOLTIP_PADDING_Y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, TOOLTIP_BORDER_SIZE);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(TOOLTIP_ITEM_SPACING_X, TOOLTIP_ITEM_SPACING_Y));
        ImVec4 window_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        window_bg.x = std::min(window_bg.x + TOOLTIP_BACKGROUND_LIFT, 1.0f);
        window_bg.y = std::min(window_bg.y + TOOLTIP_BACKGROUND_LIFT, 1.0f);
        window_bg.z = std::min(window_bg.z + TOOLTIP_BACKGROUND_LIFT, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, window_bg);
        ImVec4 border = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        border.w = std::min(border.w * TOOLTIP_BORDER_CONTRAST, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, border);
    }

    /// Pops the tooltip style.
    ~tooltip_style_scope()
    {
        ImGui::PopStyleColor(STYLE_COLOR_COUNT);
        ImGui::PopStyleVar(STYLE_VAR_COUNT);
    }

    tooltip_style_scope(const tooltip_style_scope&) = delete;
    auto operator=(const tooltip_style_scope&) -> tooltip_style_scope& = delete;
};

/// The accent color of an asset type; a neutral gray for types without one.
auto get_type_accent(const std::string& type) -> ImU32
{
    const auto found = std::find_if(TYPE_ACCENTS.begin(),
                                    TYPE_ACCENTS.end(),
                                    [&type](const type_accent& accent)
                                    {
                                        return type == accent.type;
                                    });
    return found == TYPE_ACCENTS.end() ? FALLBACK_ACCENT_COLOR : found->color;
}

/// The color with its alpha replaced.
auto with_alpha(ImU32 color, int alpha) -> ImU32
{
    return (color & ~IM_COL32_A_MASK) | (ImU32(alpha) << IM_COL32_A_SHIFT);
}

/// Between the disabled and the regular text color of the current theme.
auto get_caption_color() -> ImU32
{
    const ImVec4 disabled = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const ImVec4 regular = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    return ImGui::GetColorU32(ImLerp(disabled, regular, CAPTION_TEXT_BLEND));
}

/// The name in the current font; the caption smaller, its type in bold.
auto get_card_fonts() -> card_fonts
{
    card_fonts fonts;
    fonts.name = ImGui::GetFont();
    ImFont* bold = ImGui::GetFont(ImGui::Font::Bold);
    fonts.type = bold != nullptr ? bold : fonts.name;
    fonts.name_base_size = ImGui::GetCurrentContext()->FontSizeBase;
    fonts.caption_base_size = fonts.name_base_size * CAPTION_FONT_SCALE;
    fonts.name_size = ImGui::GetFontSize();
    fonts.caption_size = fonts.name_size * CAPTION_FONT_SCALE;
    return fonts;
}

/// A square thumbnail well as wide as the card, the accent strip under it, then the name and caption rows. Every
/// card reserves both rows, so the grid rows stay aligned whatever the entry is.
auto compute_card_layout(ImVec2 origin, float width, const card_fonts& fonts) -> card_layout
{
    card_layout layout;
    layout.well = ImRect(origin, origin + ImVec2(width, width));
    layout.accent = ImRect(ImVec2(origin.x, layout.well.Max.y),
                           ImVec2(origin.x + width, layout.well.Max.y + ACCENT_STRIP_HEIGHT));
    layout.name_pos = ImVec2(origin.x + TEXT_PADDING, layout.accent.Max.y + PAD_ACCENT_TO_NAME);
    layout.caption_pos = ImVec2(layout.name_pos.x, layout.name_pos.y + fonts.name_size + PAD_NAME_TO_CAPTION);
    layout.text_max_x = origin.x + width - TEXT_PADDING;
    const float bottom = layout.caption_pos.y + fonts.caption_size + PAD_CAPTION_TO_BOTTOM;
    layout.card = ImRect(origin, ImVec2(origin.x + width, bottom));
    return layout;
}

/// The theme's selection color when selected, a brighter tile under the pointer, a faint one otherwise; idle
/// folders blend into the panel.
auto get_tile_color(const content_browser_item& item, const card_state& state) -> ImU32
{
    if(item.is_selected)
    {
        return ImGui::GetColorU32(ImGuiCol_Header);
    }
    if(state.is_held)
    {
        return TILE_HELD_COLOR;
    }
    if(state.is_hovered)
    {
        return TILE_HOVERED_COLOR;
    }
    return item.is_folder() ? 0 : TILE_COLOR;
}

/// The tile, and for files the darker thumbnail well and the type's accent strip.
void draw_card_background(ImDrawList* draw_list,
                          const card_layout& layout,
                          const content_browser_item& item,
                          const card_state& state)
{
    if(const ImU32 tile = get_tile_color(item, state); tile != 0)
    {
        draw_list->AddRectFilled(layout.card.Min, layout.card.Max, tile, CARD_ROUNDING);
    }
    if(item.is_folder())
    {
        return;
    }
    draw_list->AddRectFilled(layout.well.Min, layout.well.Max, WELL_COLOR, CARD_ROUNDING, ImDrawFlags_RoundCornersTop);
    if(!item.type.empty())
    {
        draw_list->AddRectFilled(layout.accent.Min, layout.accent.Max, get_type_accent(item.type));
    }
}

/// An arc turning in the middle of the well while the asset loads.
void draw_card_spinner(ImDrawList* draw_list, const ImRect& area)
{
    const float radius = area.GetWidth() * SPINNER_RADIUS_FRACTION;
    const float start = float(ImGui::GetTime()) * SPINNER_SPEED;
    draw_list->PathArcTo(area.GetCenter(), radius, start, start + SPINNER_ARC, SPINNER_SEGMENTS);
    draw_list->PathStroke(ImGui::GetColorU32(ImGuiCol_Text), ImDrawFlags_None, SPINNER_THICKNESS);
}

/// The part of the well the thumbnail may cover.
auto get_thumbnail_area(const ImRect& well) -> ImRect
{
    const ImVec2 inset(THUMBNAIL_INSET, THUMBNAIL_INSET);
    return ImRect(well.Min + inset, well.Max - inset);
}

/// The thumbnail centered in the well with its aspect kept, or the spinner while the asset loads.
void draw_card_thumbnail(ImDrawList* draw_list, const ImRect& well, const content_browser_item& item)
{
    const ImRect area = get_thumbnail_area(well);
    if(item.is_loading)
    {
        draw_card_spinner(draw_list, area);
        return;
    }
    ImVec2 image = ImGui::GetSize(item.icon);
    if(!item.icon || image.x <= 0.0f || image.y <= 0.0f)
    {
        return;
    }
    image *= ImMin(area.GetWidth() / image.x, area.GetHeight() / image.y);
    const ImVec2 image_min = area.Min + (area.GetSize() - image) * 0.5f;
    draw_list->AddImageRounded(ImGui::ToId(item.icon),
                               image_min,
                               image_min + image,
                               ImVec2(0.0f, 0.0f),
                               ImVec2(1.0f, 1.0f),
                               IM_COL32_WHITE,
                               THUMBNAIL_ROUNDING);
}

/// One line of text starting at pos, ellipsized where it would pass max_x. font_base_size is unscaled.
void draw_ellipsized_text(ImDrawList* draw_list,
                          ImFont* font,
                          float font_base_size,
                          ImVec2 pos,
                          float max_x,
                          ImU32 color,
                          const char* text)
{
    ImGui::PushFont(font, font_base_size);
    const ImVec2 text_size = ImGui::CalcTextSize(text, nullptr, true);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(draw_list, pos, ImVec2(max_x, pos.y + text_size.y), max_x, text, nullptr, &text_size);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

/// The summary on a dark rounded badge in the thumbnail's bottom-right corner, when the card is wide enough.
void draw_card_badge(ImDrawList* draw_list, const ImRect& well, const card_fonts& fonts, const content_browser_item& item)
{
    if(item.summary.empty() || item.is_loading)
    {
        return;
    }
    const float font_size = fonts.name_size * BADGE_FONT_SCALE;
    const ImVec2 text_size = fonts.name->CalcTextSizeA(font_size, FLT_MAX, 0.0f, item.summary.c_str());
    const ImVec2 padding(BADGE_PADDING_X, BADGE_PADDING_Y);
    const ImRect area = get_thumbnail_area(well);
    const ImVec2 badge_max = area.Max - ImVec2(BADGE_MARGIN, BADGE_MARGIN);
    const ImVec2 badge_min = badge_max - text_size - padding * 2.0f;
    if(badge_min.x < area.Min.x + BADGE_MARGIN)
    {
        return;
    }
    draw_list->AddRectFilled(badge_min, badge_max, BADGE_BACKGROUND, BADGE_ROUNDING);
    draw_list->AddText(fonts.name, font_size, badge_min + padding, BADGE_TEXT_COLOR, item.summary.c_str());
}

/// The name, then the type as a dimmer, smaller caption.
void draw_card_labels(ImDrawList* draw_list,
                      const card_layout& layout,
                      const card_fonts& fonts,
                      const content_browser_item& item)
{
    draw_ellipsized_text(draw_list,
                         fonts.name,
                         fonts.name_base_size,
                         layout.name_pos,
                         layout.text_max_x,
                         ImGui::GetColorU32(ImGuiCol_Text),
                         item.entry.stem.c_str());
    if(item.type.empty() || item.is_folder())
    {
        return;
    }
    draw_ellipsized_text(draw_list,
                         fonts.type,
                         fonts.caption_base_size,
                         layout.caption_pos,
                         layout.text_max_x,
                         get_caption_color(),
                         item.type.c_str());
}

/// A yellow ring for the focused entry, the theme's navigation color for a selected one, and the type's accent
/// under the pointer.
void draw_card_ring(ImDrawList* draw_list,
                    const card_layout& layout,
                    const content_browser_item& item,
                    const card_state& state)
{
    if(item.is_focused)
    {
        draw_list->AddRect(layout.card.Min, layout.card.Max, FOCUSED_RING_COLOR, CARD_ROUNDING, 0, FOCUSED_RING_THICKNESS);
    }
    else if(item.is_selected)
    {
        draw_list->AddRect(layout.card.Min,
                           layout.card.Max,
                           ImGui::GetColorU32(ImGuiCol_NavCursor),
                           CARD_ROUNDING,
                           0,
                           SELECTED_RING_THICKNESS);
    }
    else if(state.is_hovered && !item.is_folder())
    {
        draw_list->AddRect(layout.card.Min,
                           layout.card.Max,
                           with_alpha(get_type_accent(item.type), HOVERED_RING_ALPHA),
                           CARD_ROUNDING,
                           0,
                           HOVERED_RING_THICKNESS);
    }
}

/// The thumbnail in a rounded well, its aspect kept.
void draw_tooltip_thumbnail(const content_browser_item& item)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, TOOLTIP_THUMBNAIL_ROUNDING);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, TOOLTIP_THUMBNAIL_BACKGROUND);
    const ImVec2 side(TOOLTIP_THUMBNAIL_SIDE, TOOLTIP_THUMBNAIL_SIDE);
    if(ImGui::BeginChild("asset_tooltip_thumb", side, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::ImageWithAspect(ImGui::ToId(item.icon), ImGui::GetSize(item.icon, side), side, ImVec2(0.5f, 0.5f));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

/// The thumbnail beside the name and the type.
void draw_tooltip_header(const content_browser_item& item)
{
    draw_tooltip_thumbnail(item);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImFont* name_font = ImGui::GetFont(ImGui::Font::Bold);
    ImGui::PushFont(name_font, name_font->LegacySize);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + TOOLTIP_WRAP_WIDTH - TOOLTIP_THUMBNAIL_SIDE -
                           ImGui::GetStyle().ItemSpacing.x);
    ImGui::TextUnformatted(item.entry.stem.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if(!item.type.empty())
    {
        ImFont* type_font = ImGui::GetFont(ImGui::Font::Black);
        ImGui::PushFont(type_font, type_font->LegacySize * TOOLTIP_TYPE_FONT_SCALE);
        ImGui::PushStyleColor(ImGuiCol_Text, TOOLTIP_TYPE_COLOR);
        ImGui::TextUnformatted(item.type.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::EndGroup();
}

/// The widest detail label, so every value of the tooltip starts on one column.
auto measure_label_column(const content_item_sections& sections) -> float
{
    float width = 0.0f;
    for(const auto& section : sections)
    {
        for(const auto& detail : section.details)
        {
            width = ImMax(width, ImGui::CalcTextSize(detail.label.c_str()).x);
        }
    }
    return width;
}

/// A dim bold title followed by a thin rule.
void draw_section_title(const std::string& title)
{
    ImGui::Spacing();
    ImFont* title_font = ImGui::GetFont(ImGui::Font::Bold);
    ImGui::PushFont(title_font, title_font->LegacySize * TOOLTIP_SECTION_FONT_SCALE);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextBorderSize, TOOLTIP_SECTION_RULE_THICKNESS);
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, ImVec2(0.0f, TOOLTIP_SECTION_TITLE_PADDING_Y));
    ImGui::SeparatorText(title.c_str());
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

/// A dim label, and its value starting at value_x and wrapping past the tooltip's width.
void draw_detail_row(const content_item_detail& detail, float value_x)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(detail.label.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine(value_x);
    ImGui::PushTextWrapPos(value_x + TOOLTIP_WRAP_WIDTH);
    ImGui::TextUnformatted(detail.value.c_str());
    ImGui::PopTextWrapPos();
}

/// The header, a rule in the type's accent, then the file facts and every type-specific section.
void draw_details_tooltip(const content_browser_item& item)
{
    content_item_sections sections;
    collect_file_details(item.entry, item.uid, sections);
    if(item.collect_details)
    {
        item.collect_details(sections);
    }
    ImGui::SetNextWindowViewportToCurrent();
    tooltip_style_scope style;
    if(!ImGui::BeginTooltipEx(ImGuiTooltipFlags_None, ImGuiWindowFlags_None))
    {
        return;
    }
    draw_tooltip_header(item);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, get_type_accent(item.type));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
    const float value_x = ImGui::GetCursorPosX() + measure_label_column(sections) + TOOLTIP_LABEL_GAP;
    for(const auto& section : sections)
    {
        if(section.details.empty())
        {
            continue;
        }
        if(!section.title.empty())
        {
            draw_section_title(section.title);
        }
        for(const auto& detail : section.details)
        {
            draw_detail_row(detail, value_x);
        }
    }
    ImGui::EndTooltip();
}

/// The thumbnail enlarged above the pointer.
void draw_preview_tooltip(const content_browser_item& item)
{
    ImGui::SetNextWindowViewportToCurrent();
    ImGui::SetNextWindowPos(ImGui::GetIO().MousePos, ImGuiCond_None, ImVec2(0.5f, 1.0f));
    tooltip_style_scope style;
    if(!ImGui::BeginTooltipEx(ImGuiTooltipFlags_None, ImGuiWindowFlags_None))
    {
        return;
    }
    const float side = ImClamp(item.size * PREVIEW_SCALE, item.size + PREVIEW_MIN_GROWTH, PREVIEW_MAX_SIDE);
    ImGui::ContentItem preview{};
    preview.texId = ImGui::ToId(item.icon);
    preview.texture_size = ImGui::GetSize(item.icon, ImVec2(side, side));
    preview.image_size = ImVec2(side, side);
    preview.name = item.entry.stem.c_str();
    preview.type = item.type.c_str();
    preview.type_font = ImGui::GetFont(ImGui::Font::Black);
    ImGui::PushID("shift_thumbnail_preview");
    ImGui::ContentButtonItem(preview);
    ImGui::PopID();
    ImGui::EndTooltip();
}
} // namespace
} // namespace content_card

content_browser_item::content_browser_item(const fs::directory_cache::cache_entry& e)
    : entry(e)
    , type(ex::get_type(e.extension, e.entry.is_directory()))
{
}

auto content_browser_item::is_folder() const -> bool
{
    return entry.entry.is_directory();
}

auto draw_content_card(const content_browser_item& item) -> bool
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window->SkipItems)
    {
        return false;
    }
    const content_card::card_fonts fonts = content_card::get_card_fonts();
    const content_card::card_layout layout = content_card::compute_card_layout(window->DC.CursorPos, item.size, fonts);
    const ImGuiID id = window->GetID(item.entry.stem.c_str());
    ImGui::ItemSize(layout.card);
    if(!ImGui::ItemAdd(layout.card, id))
    {
        return false;
    }
    content_card::card_state state;
    const bool is_pressed = ImGui::ButtonBehavior(layout.card, id, &state.is_hovered, &state.is_held);
    ImDrawList* draw_list = window->DrawList;
    content_card::draw_card_background(draw_list, layout, item, state);
    content_card::draw_card_thumbnail(draw_list, layout.well, item);
    content_card::draw_card_badge(draw_list, layout.well, fonts, item);
    content_card::draw_card_labels(draw_list, layout, fonts, item);
    content_card::draw_card_ring(draw_list, layout, item, state);
    return is_pressed;
}

void draw_content_tooltip(const content_browser_item& item)
{
    if(item.is_loading)
    {
        return;
    }
    if(ImGui::IsItemHovered() && ImGui::GetIO().KeyShift)
    {
        content_card::draw_preview_tooltip(item);
    }
    else if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        content_card::draw_details_tooltip(item);
    }
}

} // namespace unravel
