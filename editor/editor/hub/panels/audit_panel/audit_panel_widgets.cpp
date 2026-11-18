#include "audit_panel_widgets.h"
#include "../packages_panel/packages_widgets.h"

#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <audit/audit_settings_table.h>
#include <editor/imgui/screen_art.h>
#include <hpp/string_view.hpp>
#include <logging/logging.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace unravel::audit_widgets
{
namespace
{

// Indexed by audit::severity.
constexpr std::array<severity_style, 3> AUDIT_SEVERITY_STYLES{{
    {ICON_MDI_INFORMATION, "Info", "Info", IM_COL32(120, 170, 230, 255)},
    {ICON_MDI_ALERT, "Warning", "Warnings", IM_COL32(255, 190, 60, 255)},
    {ICON_MDI_ALERT_OCTAGON, "Error", "Errors", IM_COL32(255, 95, 95, 255)},
}};

// Sizes in font units.
constexpr float AUDIT_CARD_HEIGHT = 4.6f;
constexpr float AUDIT_CARD_PADDING = 0.8f;
constexpr float AUDIT_CARD_ROUNDING = 0.4f;
constexpr float AUDIT_CARD_STRIPE = 0.2f;
constexpr float AUDIT_CARD_VALUE_SCALE = 2.0f;
constexpr float AUDIT_CARD_CAPTION_SCALE = 0.9f;
constexpr float AUDIT_BAR_LABEL_SHARE = 0.45f;
constexpr float AUDIT_BAR_COUNT_WIDTH = 4.5f;
constexpr float AUDIT_BAR_HEIGHT = 0.55f;
constexpr float AUDIT_BAR_ICON_COLUMN = 1.5f;
constexpr float AUDIT_CALLOUT_PADDING = 0.75f;
constexpr float AUDIT_CALLOUT_ICON_COLUMN = 1.6f;
constexpr float AUDIT_CALLOUT_STRIPE = 0.18f;
constexpr float AUDIT_CHIP_PADDING_X = 0.5f;
constexpr float AUDIT_CHIP_PADDING_Y = 0.12f;
constexpr float AUDIT_SPINNER_THICKNESS = 0.28f;

constexpr float AUDIT_CARD_FILL_ALPHA = 0.18f;
constexpr float AUDIT_CARD_HOVER_ALPHA = 0.28f;
constexpr float AUDIT_TRACK_ALPHA = 0.07f;
constexpr float AUDIT_CALLOUT_FILL_ALPHA = 0.10f;
constexpr float AUDIT_CHIP_FILL_ALPHA = 0.16f;
constexpr float AUDIT_CHIP_HOVER_ALPHA = 0.30f;
constexpr ImU32 AUDIT_CARD_COLOR = IM_COL32(0, 0, 0, 56);
constexpr ImU32 AUDIT_CARD_HOVERED_COLOR = IM_COL32(255, 255, 255, 18);

/// What begin_callout leaves for end_callout: callouts do not nest.
struct audit_callout_state
{
    ImDrawList* draw_list{nullptr};
    ImVec2 min{};
    float right{};
    ImU32 color{};
};
audit_callout_state g_audit_callout;
/// The callout's own splitter: inside a table the window's draw list is already split by the table's, and splits
/// nest only through separate splitters.
ImDrawListSplitter g_audit_callout_splitter;

auto scaled_font_size(float scale) -> float
{
    return ImGui::GetCurrentContext()->FontSizeBase * scale;
}

} // namespace

auto get_severity_style(audit::severity level) -> const severity_style&
{
    return AUDIT_SEVERITY_STYLES[static_cast<std::size_t>(level)];
}

auto get_area_title(audit::area value) -> const char*
{
    switch(value)
    {
        case audit::area::correctness:
            return "Correctness";
        case audit::area::cpu:
            return "CPU";
        case audit::area::gpu:
            return "GPU";
        case audit::area::memory:
            return "Memory";
        case audit::area::download:
            return "Download";
        case audit::area::load_time:
            return "Load Time";
        case audit::area::quality:
            return "Quality";
        case audit::area::none:
        default:
            return "None";
    }
}

auto get_area_icon(audit::area value) -> const char*
{
    switch(value)
    {
        case audit::area::correctness:
            return ICON_MDI_CHECK_DECAGRAM;
        case audit::area::cpu:
            return ICON_MDI_CHIP;
        case audit::area::gpu:
            return ICON_MDI_EXPANSION_CARD;
        case audit::area::memory:
            return ICON_MDI_MEMORY;
        case audit::area::download:
            return ICON_MDI_DOWNLOAD;
        case audit::area::load_time:
            return ICON_MDI_TIMER_SAND;
        case audit::area::quality:
            return ICON_MDI_IMAGE_FILTER_HDR;
        case audit::area::none:
        default:
            return ICON_MDI_CIRCLE_SMALL;
    }
}

auto get_rule_source_title(audit::rule_source source) -> const char*
{
    switch(source)
    {
        case audit::rule_source::assets:
            return "Assets";
        case audit::rule_source::settings:
            return "Settings";
        case audit::rule_source::content:
            return "Deploy Content";
        case audit::rule_source::scenes:
            return "Scenes and Prefabs";
        case audit::rule_source::scripts:
            return "Script Compiler";
        case audit::rule_source::code:
        default:
            return "Script Analysis";
    }
}

auto get_rule_source_icon(audit::rule_source source) -> const char*
{
    switch(source)
    {
        case audit::rule_source::assets:
            return ICON_MDI_PACKAGE_VARIANT_CLOSED;
        case audit::rule_source::settings:
            return ICON_MDI_COG_OUTLINE;
        case audit::rule_source::content:
            return ICON_MDI_TRUCK_OUTLINE;
        case audit::rule_source::scenes:
            return ICON_MDI_FILE_TREE;
        case audit::rule_source::scripts:
            return ICON_MDI_FILE_CODE_OUTLINE;
        case audit::rule_source::code:
        default:
            return ICON_MDI_CODE_BRACES;
    }
}

auto get_platform_icon(const std::string& platform_title) -> const char*
{
    if(platform_title == "Android")
    {
        return ICON_MDI_ANDROID;
    }
    if(platform_title == "Web")
    {
        return ICON_MDI_WEB;
    }
    return ICON_MDI_MONITOR;
}

auto with_alpha(ImU32 color, float alpha) -> ImU32
{
    const auto base = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF);
    const auto scaled = static_cast<ImU32>(std::clamp(base * alpha, 0.0f, 255.0f));
    return (color & ~IM_COL32_A_MASK) | (scaled << IM_COL32_A_SHIFT);
}

auto to_pixels(float font_units) -> float
{
    return packages_widgets::to_pixels(font_units);
}

auto get_file_name(const std::string& key) -> std::string
{
    const auto slash = key.find_last_of("/\\");
    return slash == std::string::npos ? key : key.substr(slash + 1);
}

auto format_duration(double seconds) -> std::string
{
    constexpr double SECONDS_SHOWN_WITH_TENTHS = 10.0;
    constexpr double SECONDS_IN_MINUTE = 60.0;
    if(seconds < SECONDS_SHOWN_WITH_TENTHS)
    {
        return fmt::format("{:.1f} s", seconds);
    }
    if(seconds < SECONDS_IN_MINUTE)
    {
        return fmt::format("{:.0f} s", std::floor(seconds));
    }
    const auto whole = static_cast<std::int64_t>(seconds);
    return fmt::format("{} min {:02} s", whole / 60, whole % 60);
}

auto format_time(std::int64_t seconds) -> std::string
{
    const auto time = static_cast<std::time_t>(seconds);
    const std::tm* local = std::localtime(&time);
    if(local == nullptr)
    {
        return "an unknown time";
    }
    char text[64]{};
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", local);
    return text;
}

auto get_setting_label(const std::string& path) -> std::string
{
    const auto* setting = audit::find_performance_setting(path);
    if(setting != nullptr)
    {
        return setting->label;
    }
    if(path == "assets/texture/default_compression")
    {
        return "Assets > Textures > Default Compression";
    }
    if(path == "assets/audio/default_compression")
    {
        return "Assets > Audio > Default Compression";
    }
    if(path == "physics/fixed_timestep")
    {
        return "Physics > Fixed Timestep";
    }
    return path;
}

auto get_settings_category(const std::string& path) -> std::string
{
    const auto first = path.substr(0, path.find('/'));
    if(first == "graphics")
    {
        return "Graphics";
    }
    if(first == "global_illumination")
    {
        return "Global Illumination";
    }
    if(first == "assets")
    {
        return "Assets";
    }
    if(first == "physics")
    {
        return "Physics";
    }
    return {};
}

auto describe_subject(const audit::subject& where) -> std::string
{
    switch(where.kind)
    {
        case audit::subject_kind::asset:
            return where.key;
        case audit::subject_kind::entity:
            return fmt::format("{} in {}", where.label.empty() ? where.detail : where.label, where.key);
        case audit::subject_kind::script:
        {
            std::string text = where.key;
            if(where.line > 0)
            {
                text += fmt::format(" line {}", where.line);
            }
            if(!where.detail.empty())
            {
                text += fmt::format(" ({})", where.detail);
            }
            return text;
        }
        case audit::subject_kind::setting:
            return get_setting_label(where.key);
        case audit::subject_kind::profile:
            return where.detail.empty() ? fmt::format("Profile {}", where.key) : fmt::format("Profile {}: {}", where.key, where.detail);
        case audit::subject_kind::project:
        default:
            return where.key;
    }
}

auto describe_subject_short(const audit::subject& where) -> std::string
{
    switch(where.kind)
    {
        case audit::subject_kind::asset:
            return get_file_name(where.key);
        case audit::subject_kind::entity:
            return fmt::format("{} ({})", get_file_name(where.label.empty() ? where.detail : where.label), get_file_name(where.key));
        case audit::subject_kind::script:
            if(!where.detail.empty())
            {
                return where.detail;
            }
            return where.line > 0 ? fmt::format("{}:{}", get_file_name(where.key), where.line) : get_file_name(where.key);
        case audit::subject_kind::setting:
            return get_setting_label(where.key);
        case audit::subject_kind::profile:
        case audit::subject_kind::project:
        default:
            return where.key;
    }
}

auto make_issue_text(const audit::issue& item) -> std::string
{
    const auto* owner = audit::find_rule(item.rule);
    std::string text = fmt::format("[{}] {}\n{}\n{}",
                                   get_severity_style(item.level).name,
                                   owner != nullptr ? owner->title : item.rule,
                                   describe_subject(item.where),
                                   item.message);
    if(item.lines.size() > 1)
    {
        text += "\nLines:";
        for(const auto line : item.lines)
        {
            text += " " + std::to_string(line);
        }
    }
    for(const auto& line : item.details)
    {
        text += "\n" + line;
    }
    if(item.call_path.size() > 1)
    {
        text += "\nReached from:";
        for(std::size_t index = 0; index < item.call_path.size(); ++index)
        {
            text += (index == 0 ? " " : " > ") + item.call_path[index];
        }
    }
    if(!item.profiles.empty())
    {
        text += "\nProfiles:";
        for(const auto& profile : item.profiles)
        {
            text += " " + profile;
        }
    }
    if(owner != nullptr)
    {
        text += "\nWhat to do: " + owner->recommendation;
    }
    text += "\nRule: " + item.rule + "\nIssue id: " + item.get_id();
    return text;
}

void draw_section_header(const char* title, const char* right_text)
{
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if(right_text != nullptr && right_text[0] != '\0')
    {
        const float width = ImGui::CalcTextSize(right_text).x;
        ImGui::SameLine(ImMax(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width));
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
        ImGui::TextUnformatted(right_text);
        ImGui::PopStyleColor();
    }
}

auto draw_section_header_link(const char* title, const char* id, const char* link_text, const char* tooltip) -> bool
{
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    const float width = ImGui::CalcTextSize(link_text).x;
    ImGui::SameLine(ImMax(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width));
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool is_clicked = ImGui::InvisibleButton(id, ImVec2(width, ImGui::GetTextLineHeight()));
    const bool is_hovered = ImGui::IsItemHovered();
    const ImVec4 accent = imgui_style::get_accent_color();
    const ImU32 color = is_hovered ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::ColorConvertFloat4ToU32(accent);
    ImGui::GetWindowDrawList()->AddText(min, color, link_text);
    if(is_hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImGui::SetItemTooltipEx("%s", tooltip);
    return is_clicked;
}

auto get_stat_card_height() -> float
{
    return to_pixels(AUDIT_CARD_HEIGHT);
}

auto draw_stat_card(const char* id, const stat_card& card, float width) -> bool
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImMax(1.0f, width), get_stat_card_height());
    const ImVec2 max = min + size;
    const bool is_pressed = ImGui::InvisibleButton(id, size) && !card.is_muted;
    const bool is_hovered = !card.is_muted && ImGui::IsItemHovered();
    if(card.tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", card.tooltip);
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float rounding = to_pixels(AUDIT_CARD_ROUNDING);
    draw_list->AddRectFilled(min, max, AUDIT_CARD_COLOR, rounding);
    if(!card.is_muted)
    {
        draw_list->AddRectFilled(min, max, with_alpha(card.color, is_hovered ? AUDIT_CARD_HOVER_ALPHA * 0.5f : AUDIT_CARD_FILL_ALPHA * 0.35f), rounding);
    }
    if(is_hovered)
    {
        draw_list->AddRectFilled(min, max, AUDIT_CARD_HOVERED_COLOR, rounding);
    }
    const ImU32 accent = card.is_muted ? imgui_style::get_muted_text_color_u32() : card.color;
    draw_list->AddRectFilled(min, ImVec2(min.x + to_pixels(AUDIT_CARD_STRIPE), max.y), accent, rounding, ImDrawFlags_RoundCornersLeft);

    const float padding = to_pixels(AUDIT_CARD_PADDING);
    const ImVec4 clip(min.x, min.y, max.x - padding * 0.5f, max.y);
    const float icon_center_y = min.y + padding + font_size * 0.5f;
    ImGui::RenderIconCentered(draw_list, ImVec2(min.x + padding + font_size * 0.5f, icon_center_y), card.icon, accent);
    draw_list->AddText(ImGui::GetFont(),
                       font_size,
                       ImVec2(min.x + padding + font_size * 1.5f, min.y + padding),
                       imgui_style::get_muted_text_color_u32(),
                       card.label,
                       nullptr,
                       0.0f,
                       &clip);
    ImFont* value_font = ImGui::GetFont(ImGui::Font::ExtraBold);
    const float value_size = scaled_font_size(AUDIT_CARD_VALUE_SCALE);
    const float value_y = min.y + padding + font_size * 1.35f;
    const ImU32 value_color = card.is_muted ? imgui_style::get_muted_text_color_u32() : ImGui::GetColorU32(ImGuiCol_Text);
    draw_list->AddText(value_font, value_size, ImVec2(min.x + padding, value_y), value_color, card.value.c_str(), nullptr, 0.0f, &clip);
    if(!card.caption.empty())
    {
        const float caption_size = font_size * AUDIT_CARD_CAPTION_SCALE;
        const float value_width = value_font->CalcTextSizeA(value_size, FLT_MAX, 0.0f, card.value.c_str()).x;
        const ImVec2 caption_pos(min.x + padding + value_width + font_size * 0.6f,
                                 value_y + value_size - caption_size - font_size * 0.25f);
        draw_list->AddText(ImGui::GetFont(),
                           caption_size,
                           caption_pos,
                           imgui_style::get_muted_text_color_u32(),
                           card.caption.c_str(),
                           nullptr,
                           0.0f,
                           &clip);
    }
    if(is_hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    return is_pressed;
}

auto draw_bar_row(const char* id,
                  const char* icon,
                  ImU32 icon_color,
                  const char* label,
                  const std::vector<bar_segment>& segments,
                  float scale,
                  const char* right_text,
                  const char* tooltip) -> bool
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    const ImVec2 max = min + size;
    const bool is_clicked = ImGui::InvisibleButton(id, size);
    const bool is_hovered = ImGui::IsItemHovered();
    if(tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", tooltip);
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if(is_hovered)
    {
        draw_list->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.6f), to_pixels(AUDIT_CARD_ROUNDING));
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float padding = font_size * 0.4f;
    const float text_y = ImFloor(min.y + (size.y - font_size) * 0.5f);
    float left = min.x + padding;
    if(icon != nullptr)
    {
        ImGui::RenderIconCentered(draw_list, ImVec2(left + font_size * 0.5f, min.y + size.y * 0.5f), icon, icon_color);
        left += to_pixels(AUDIT_BAR_ICON_COLUMN);
    }
    const float count_width = to_pixels(AUDIT_BAR_COUNT_WIDTH);
    const float label_right = min.x + ImMax(size.x * AUDIT_BAR_LABEL_SHARE, left - min.x + font_size * 4.0f);
    ImGui::RenderTextEllipsis(draw_list,
                              ImVec2(left, text_y),
                              ImVec2(label_right - padding, max.y),
                              label_right - padding,
                              label,
                              nullptr,
                              nullptr);
    const float bar_left = label_right;
    const float bar_right = max.x - count_width - padding;
    const float bar_height = to_pixels(AUDIT_BAR_HEIGHT);
    const float bar_top = ImFloor(min.y + (size.y - bar_height) * 0.5f);
    if(bar_right > bar_left)
    {
        const float rounding = bar_height * 0.5f;
        draw_list->AddRectFilled(ImVec2(bar_left, bar_top),
                                 ImVec2(bar_right, bar_top + bar_height),
                                 ImGui::GetColorU32(ImGuiCol_Text, AUDIT_TRACK_ALPHA),
                                 rounding);
        float x = bar_left;
        const float width = bar_right - bar_left;
        for(const auto& segment : segments)
        {
            if(segment.value <= 0.0f || scale <= 0.0f)
            {
                continue;
            }
            const float segment_width = ImMax(2.0f, width * segment.value / scale);
            const float end = ImMin(bar_right, x + segment_width);
            draw_list->AddRectFilled(ImVec2(x, bar_top), ImVec2(end, bar_top + bar_height), segment.color, rounding);
            x = end;
        }
    }
    if(right_text != nullptr)
    {
        const float text_width = ImGui::CalcTextSize(right_text).x;
        draw_list->AddText(ImVec2(max.x - padding - text_width, text_y), ImGui::GetColorU32(ImGuiCol_Text), right_text);
    }
    return is_clicked;
}

void begin_callout(const char* id, const char* icon, const char* title, const char* text, ImU32 color)
{
    ImGui::PushID(id);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    g_audit_callout_splitter.Split(draw_list, 2);
    g_audit_callout_splitter.SetCurrentChannel(draw_list, 1);
    const float padding = to_pixels(AUDIT_CALLOUT_PADDING);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    g_audit_callout = {draw_list, min, min.x + ImGui::GetContentRegionAvail().x, color};
    ImGui::SetCursorScreenPos(ImVec2(min.x + padding, min.y + padding));
    ImGui::BeginGroup();
    const float font_size = ImGui::GetFontSize();
    const ImVec2 icon_origin = ImGui::GetCursorScreenPos();
    ImGui::RenderIconCentered(draw_list,
                              ImVec2(icon_origin.x + font_size * 0.55f, icon_origin.y + ImGui::GetTextLineHeight() * 0.5f),
                              icon,
                              color);
    const float indent = to_pixels(AUDIT_CALLOUT_ICON_COLUMN);
    ImGui::Indent(indent);
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if(text != nullptr && text[0] != '\0')
    {
        ImGui::PushTextWrapPos(g_audit_callout.right - padding - ImGui::GetWindowPos().x + ImGui::GetScrollX());
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
    }
}

void draw_callout_text(const char* text, bool is_bullet)
{
    const float padding = to_pixels(AUDIT_CALLOUT_PADDING);
    if(is_bullet)
    {
        ImGui::Bullet();
        ImGui::SameLine();
    }
    ImGui::PushTextWrapPos(g_audit_callout.right - padding - ImGui::GetWindowPos().x + ImGui::GetScrollX());
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

void end_callout()
{
    const float padding = to_pixels(AUDIT_CALLOUT_PADDING);
    ImGui::Unindent(to_pixels(AUDIT_CALLOUT_ICON_COLUMN));
    ImGui::EndGroup();
    const auto& state = g_audit_callout;
    const ImVec2 max(state.right, ImGui::GetItemRectMax().y + padding);
    const float rounding = to_pixels(AUDIT_CARD_ROUNDING);
    g_audit_callout_splitter.SetCurrentChannel(state.draw_list, 0);
    state.draw_list->AddRectFilled(state.min, max, with_alpha(state.color, AUDIT_CALLOUT_FILL_ALPHA), rounding);
    state.draw_list->AddRectFilled(state.min,
                                   ImVec2(state.min.x + to_pixels(AUDIT_CALLOUT_STRIPE), max.y),
                                   state.color,
                                   rounding,
                                   ImDrawFlags_RoundCornersLeft);
    g_audit_callout_splitter.Merge(state.draw_list);
    ImGui::SetCursorScreenPos(ImVec2(state.min.x, max.y));
    ImGui::Dummy(ImVec2(state.right - state.min.x, 0.0f));
    ImGui::PopID();
}

auto calc_chip_width(const char* text) -> float
{
    return ImGui::CalcTextSize(text).x + to_pixels(AUDIT_CHIP_PADDING_X) * 2.0f;
}

auto draw_chip_at(const ImVec2& min, const char* text, ImU32 color, bool is_hovered) -> ImVec2
{
    const float padding_x = to_pixels(AUDIT_CHIP_PADDING_X);
    const float padding_y = to_pixels(AUDIT_CHIP_PADDING_Y);
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const ImVec2 size(text_size.x + padding_x * 2.0f, text_size.y + padding_y * 2.0f);
    const float offset_y = ImMax(0.0f, (ImGui::GetFrameHeight() - size.y) * 0.5f);
    const ImVec2 chip_min(min.x, min.y + offset_y);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(chip_min,
                             chip_min + size,
                             with_alpha(color, is_hovered ? AUDIT_CHIP_HOVER_ALPHA : AUDIT_CHIP_FILL_ALPHA),
                             size.y * 0.5f);
    draw_list->AddText(ImVec2(chip_min.x + padding_x, chip_min.y + padding_y), color, text);
    return ImVec2(size.x, ImMax(size.y, ImGui::GetFrameHeight()));
}


void draw_chip(const char* text, ImU32 color, const char* tooltip)
{
    const ImVec2 size = draw_chip_at(ImGui::GetCursorScreenPos(), text, color, false);
    ImGui::Dummy(size);
    if(tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", tooltip);
    }
}

auto draw_chip_button(const char* id, const char* text, ImU32 color, const char* tooltip) -> bool
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(calc_chip_width(text), ImGui::GetFrameHeight());
    const bool is_clicked = ImGui::InvisibleButton(id, size);
    const bool is_hovered = ImGui::IsItemHovered();
    draw_chip_at(min, text, color, is_hovered);
    if(is_hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if(tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", tooltip);
    }
    return is_clicked;
}

void same_line_or_wrap(float next_width)
{
    ImGui::SameLine();
    if(ImGui::GetContentRegionAvail().x < next_width)
    {
        ImGui::NewLine();
    }
}

auto draw_icon_button(const char* id, const char* icon, const char* tooltip, bool is_enabled) -> bool
{
    const float side = ImGui::GetFrameHeight();
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(id, ImVec2(side, side));
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(), rect.GetCenter(), icon, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("%s", tooltip);
    return is_pressed;
}

auto draw_primary_button(const char* label, const char* tooltip, bool is_enabled) -> bool
{
    constexpr float ACTIVE_SHADE = 0.85f;
    const ImVec4 accent = imgui_style::get_accent_color();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accent.x * ACTIVE_SHADE, accent.y * ACTIVE_SHADE, accent.z * ACTIVE_SHADE, 1.0f));
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    ImGui::SetItemTooltipEx("%s", tooltip);
    return is_pressed;
}

void draw_field_label(const char* label, float column_width)
{
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(column_width);
}

auto get_display_path(const std::string& key) -> std::string
{
    constexpr hpp::string_view PROJECT_DATA = "app:/data/";
    if(key.rfind(PROJECT_DATA.data(), 0) == 0)
    {
        return key.substr(PROJECT_DATA.size());
    }
    return key;
}

auto draw_path_item(const char* id, const std::string& key, bool is_link, const char* tooltip) -> bool
{
    const auto shown = get_display_path(key);
    const auto slash = shown.find_last_of('/');
    const std::string folder = slash == std::string::npos ? std::string{} : shown.substr(0, slash + 1);
    const std::string name = slash == std::string::npos ? shown : shown.substr(slash + 1);
    const float folder_width = ImGui::CalcTextSize(folder.c_str()).x;
    const float name_width = ImGui::CalcTextSize(name.c_str()).x;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImMax(1.0f, ImMin(folder_width + name_width, ImGui::GetContentRegionAvail().x));
    const float height = ImGui::GetFrameHeight();
    const bool is_clicked = ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool is_hovered = is_link && ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float text_y = ImFloor(min.y + (height - ImGui::GetFontSize()) * 0.5f);
    const ImVec4 clip(min.x, min.y, min.x + width, min.y + height);
    // A long path gives way from its folder: the file name stays readable.
    const float name_x = ImMax(min.x, min.x + width - name_width);
    const float folder_x = name_x - folder_width;
    const ImVec4 folder_clip(min.x, min.y, name_x, min.y + height);
    draw_list->AddText(ImGui::GetFont(),
                       ImGui::GetFontSize(),
                       ImVec2(folder_x, text_y),
                       imgui_style::get_muted_text_color_u32(),
                       folder.c_str(),
                       nullptr,
                       0.0f,
                       &folder_clip);
    const ImU32 name_color = is_hovered ? ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color())
                                        : ImGui::GetColorU32(ImGuiCol_Text);
    draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(name_x, text_y), name_color, name.c_str(), nullptr, 0.0f, &clip);
    if(is_hovered)
    {
        draw_list->AddLine(ImVec2(name_x, text_y + ImGui::GetFontSize()),
                           ImVec2(name_x + name_width, text_y + ImGui::GetFontSize()),
                           name_color);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if(tooltip != nullptr && ImGui::IsItemHovered())
    {
        ImGui::SetItemTooltipEx("%s\n%s", key.c_str(), tooltip);
    }
    return is_clicked && is_link;
}

namespace
{

/// The handle's thickness, in font units.
constexpr float AUDIT_SPLIT_HANDLE = 0.5f;
/// The share a drag can take the first pane to, at most, before the minimums apply.
constexpr float AUDIT_SPLIT_MIN_SHARE = 0.05f;
constexpr float AUDIT_SPLIT_MAX_SHARE = 0.95f;

/// The first pane's size along the split: its share of what the handle leaves, kept between the minimums. When the
/// space cannot hold both minimums, the panes share it by their minimums.
auto get_split_size(float total, float share, float min_first, float min_second) -> float
{
    const float usable = ImMax(0.0f, total - to_pixels(AUDIT_SPLIT_HANDLE));
    if(usable < min_first + min_second)
    {
        return ImFloor(usable * min_first / ImMax(1.0f, min_first + min_second));
    }
    return ImFloor(ImClamp(usable * share, min_first, usable - min_second));
}

} // namespace

pane_split::pane_split(const char* id, bool is_side_by_side, float& share, float min_first, float min_second)
    : id_(id)
    , is_side_by_side_(is_side_by_side)
    , share_(share)
    , min_first_(min_first)
    , min_second_(min_second)
    , start_(ImGui::GetCursorScreenPos())
    , avail_(ImGui::GetContentRegionAvail())
{
    first_ = get_split_size(is_side_by_side_ ? avail_.x : avail_.y, share_, min_first_, min_second_);
}

auto pane_split::get_first_size() const -> ImVec2
{
    return is_side_by_side_ ? ImVec2(first_, avail_.y) : ImVec2(avail_.x, first_);
}

void pane_split::draw_handle()
{
    const float thickness = to_pixels(AUDIT_SPLIT_HANDLE);
    const ImVec2 min = is_side_by_side_ ? ImVec2(start_.x + first_, start_.y) : ImVec2(start_.x, start_.y + first_);
    const ImVec2 size = is_side_by_side_ ? ImVec2(thickness, avail_.y) : ImVec2(avail_.x, thickness);
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton(id_, size);
    const bool is_hovered = ImGui::IsItemHovered();
    const bool is_active = ImGui::IsItemActive();
    if(is_hovered || is_active)
    {
        ImGui::SetMouseCursor(is_side_by_side_ ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    }
    const float total = is_side_by_side_ ? avail_.x : avail_.y;
    const float usable = total - thickness;
    if(is_active && usable > 0.0f)
    {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        const float dragged = first_ + (is_side_by_side_ ? delta.x : delta.y);
        share_ = ImClamp(dragged / usable, AUDIT_SPLIT_MIN_SHARE, AUDIT_SPLIT_MAX_SHARE);
    }
    const ImU32 color = is_active    ? ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color())
                        : is_hovered ? ImGui::GetColorU32(ImGuiCol_Text, 0.35f)
                                     : ImGui::GetColorU32(ImGuiCol_Separator);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if(is_side_by_side_)
    {
        const float x = ImFloor(min.x + thickness * 0.5f);
        draw_list->AddLine(ImVec2(x, min.y), ImVec2(x, min.y + avail_.y), color, is_active ? 2.0f : 1.0f);
    }
    else
    {
        const float y = ImFloor(min.y + thickness * 0.5f);
        draw_list->AddLine(ImVec2(min.x, y), ImVec2(min.x + avail_.x, y), color, is_active ? 2.0f : 1.0f);
    }
    ImGui::SetCursorScreenPos(is_side_by_side_ ? ImVec2(min.x + thickness, start_.y) : ImVec2(start_.x, min.y + thickness));
}

void draw_spinner(const ImVec2& center, float radius, ImU32 color)
{
    screen_art::draw_spinner(ImGui::GetWindowDrawList(), center, radius, radius * AUDIT_SPINNER_THICKNESS, color);
}

} // namespace unravel::audit_widgets
