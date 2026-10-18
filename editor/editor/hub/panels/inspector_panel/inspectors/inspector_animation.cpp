#include "inspector_animation.h"
#include "inspectors.h"

#include <editor/editing/animation_retarget_check.h>
#include <editor/editing/editing_manager.h>
#include <engine/animation/animation.h>
#include <engine/animation/animation_humanoid.h>
#include <engine/animation/animation_player.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/animation/ecs/systems/animation_system.h>
#include <engine/play_mode.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace unravel
{
namespace
{
// ---------------------------------------------------------------------------
// Layout metrics of the player section.
// ---------------------------------------------------------------------------
constexpr float STRIP_HEIGHT = 26.0f;
constexpr float STRIP_ROUNDING = 4.0f;
constexpr float STRIP_TEXT_PADDING = 8.0f;
constexpr float WEIGHT_BAR_HEIGHT = 3.0f;
constexpr float GRAPH_CURVE_HEIGHT = 46.0f;
constexpr float GRAPH_LABEL_HEIGHT = 18.0f;
constexpr float TRACE_HEIGHT = 24.0f;
constexpr float CARD_ROUNDING = 6.0f;
constexpr float PILL_PAD_X = 9.0f;
constexpr float PILL_DOT_RADIUS = 3.5f;
constexpr float PILL_DOT_GAP = 6.0f;
constexpr int CURVE_SAMPLE_COUNT = 64;
/// Matches the frame step used by play-mode frame skipping.
constexpr float EDITOR_STEP_SECONDS = 1.0f / 60.0f;
constexpr double TRACE_SAMPLE_INTERVAL = 1.0 / 30.0;

// ---------------------------------------------------------------------------
// Palette. Fixed semantic colors that read well on all shipped dark themes.
// ---------------------------------------------------------------------------
constexpr ImU32 COL_PLAYING = IM_COL32(70, 200, 120, 255);
constexpr ImU32 COL_PAUSED = IM_COL32(255, 185, 80, 255);
constexpr ImU32 COL_STOPPED = IM_COL32(145, 150, 160, 255);
/// Crossfade source (fading out).
constexpr ImU32 COL_SOURCE = IM_COL32(250, 150, 70, 255);
/// Crossfade target (fading in).
constexpr ImU32 COL_TARGET = IM_COL32(80, 190, 230, 255);
/// Steady single-state playback.
constexpr ImU32 COL_ACTIVE = IM_COL32(110, 160, 235, 255);
constexpr ImU32 COL_TRACK_BG = IM_COL32(14, 16, 20, 200);
constexpr ImU32 COL_TRACK_BORDER = IM_COL32(255, 255, 255, 26);
constexpr ImU32 COL_TRACK_BORDER_HOVER = IM_COL32(255, 255, 255, 80);
constexpr ImU32 COL_GRID = IM_COL32(255, 255, 255, 16);
constexpr ImU32 COL_PLAYHEAD = IM_COL32(255, 255, 255, 210);
constexpr ImU32 COL_TEXT = IM_COL32(235, 238, 242, 255);
constexpr ImU32 COL_TEXT_DIM = IM_COL32(255, 255, 255, 120);
constexpr ImU32 COL_CARD_BG = IM_COL32(255, 255, 255, 9);

auto with_alpha(ImU32 col, float alpha) -> ImU32
{
    const float a = float((col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f);
    return (col & ~IM_COL32_A_MASK) | (ImU32(a) << IM_COL32_A_SHIFT);
}

void format_state_time(char* buf, size_t buf_size, float elapsed, float duration)
{
    if(duration > 0.0f)
    {
        ImFormatString(buf, int(buf_size), "%.2f / %.2f s", elapsed, duration);
    }
    else
    {
        ImFormatString(buf, int(buf_size), "%.2f s", elapsed);
    }
}

auto calc_status_pill_width(const char* label) -> float
{
    return PILL_PAD_X * 2.0f + PILL_DOT_RADIUS * 2.0f + PILL_DOT_GAP + ImGui::CalcTextSize(label).x;
}

void draw_status_pill(const char* label, ImU32 color, bool pulse)
{
    auto* dl = ImGui::GetWindowDrawList();
    const float height = ImGui::GetFrameHeight();
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const float width = calc_status_pill_width(label);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    const ImVec2 max(min.x + width, min.y + height);
    dl->AddRectFilled(min, max, with_alpha(color, 0.16f), height * 0.5f);
    dl->AddRect(min, max, with_alpha(color, 0.55f), height * 0.5f);
    const ImVec2 dot(min.x + PILL_PAD_X + PILL_DOT_RADIUS, (min.y + max.y) * 0.5f);
    const float dot_alpha = pulse ? 0.775f + 0.225f * float(std::sin(ImGui::GetTime() * 5.0)) : 1.0f;
    dl->AddCircleFilled(dot, PILL_DOT_RADIUS, with_alpha(color, std::clamp(dot_alpha, 0.0f, 1.0f)));
    dl->AddText(ImVec2(dot.x + PILL_DOT_RADIUS + PILL_DOT_GAP, (min.y + max.y) * 0.5f - text_size.y * 0.5f),
                color,
                label);
}

/// Everything a playback strip needs to render one state of a layer.
struct strip_desc
{
    const char* id{};
    /// "A" / "B" role tag while crossfading, nullptr otherwise.
    const char* role_letter{};
    std::string name;
    std::string badges;
    std::string tooltip;
    float progress{};
    float elapsed{};
    /// <= 0 means unknown (e.g. blend space before its first sample).
    float duration{};
    /// Contribution to the final layer pose, 0..1.
    float weight{1.0f};
    bool show_weight{};
    bool seekable{};
    bool seek_target{};
    size_t layer_index{};
    ImU32 accent{COL_ACTIVE};
};

void draw_clip_strip(animation_player& player, const strip_desc& d)
{
    auto* dl = ImGui::GetWindowDrawList();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 60.0f);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float total_height = STRIP_HEIGHT + (d.show_weight ? WEIGHT_BAR_HEIGHT + 2.0f : 0.0f);
    ImGui::InvisibleButton(d.id, ImVec2(width, total_height));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 max(min.x + width, min.y + STRIP_HEIGHT);
    const float progress = std::clamp(d.progress, 0.0f, 1.0f);
    dl->AddRectFilled(min, max, COL_TRACK_BG, STRIP_ROUNDING);
    if(progress > 0.0f)
    {
        const float fill_end = min.x + width * progress;
        const ImDrawFlags corners = progress > 0.995f ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft;
        dl->AddRectFilled(min, ImVec2(fill_end, max.y), with_alpha(d.accent, 0.25f), STRIP_ROUNDING, corners);
        dl->AddLine(ImVec2(fill_end, min.y + 1.0f), ImVec2(fill_end, max.y - 1.0f), with_alpha(d.accent, 0.9f), 2.0f);
    }
    for(int i = 1; i < 4; ++i)
    {
        const float x = min.x + width * 0.25f * float(i);
        dl->AddLine(ImVec2(x, max.y - 5.0f), ImVec2(x, max.y - 1.0f), COL_GRID, 1.0f);
    }
    const float font_size = ImGui::GetFontSize();
    const float text_y = min.y + (STRIP_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f;
    float text_x = min.x + STRIP_TEXT_PADDING;
    if(d.role_letter != nullptr)
    {
        const float chip = 15.0f;
        const ImVec2 chip_min(text_x, min.y + (STRIP_HEIGHT - chip) * 0.5f);
        const ImVec2 chip_max(chip_min.x + chip, chip_min.y + chip);
        dl->AddRectFilled(chip_min, chip_max, with_alpha(d.accent, 0.35f), 3.0f);
        dl->AddRect(chip_min, chip_max, with_alpha(d.accent, 0.8f), 3.0f);
        const ImVec2 letter_size = ImGui::CalcTextSize(d.role_letter);
        dl->AddText(ImVec2(chip_min.x + (chip - letter_size.x) * 0.5f, chip_min.y + (chip - letter_size.y) * 0.5f),
                    COL_TEXT,
                    d.role_letter);
        text_x = chip_max.x + 6.0f;
    }
    char time_buf[48];
    if(d.duration > 0.0f)
    {
        format_state_time(time_buf, sizeof(time_buf), d.elapsed, d.duration);
    }
    else
    {
        ImFormatString(time_buf, int(sizeof(time_buf)), "--");
    }
    ImFont* mono_font = ImGui::GetFont(ImGui::Font::Mono);
    const ImVec2 time_size = mono_font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, time_buf);
    const float time_x = max.x - STRIP_TEXT_PADDING - time_size.x;
    dl->PushClipRect(ImVec2(text_x, min.y), ImVec2(std::max(time_x - 8.0f, text_x), max.y), true);
    dl->AddText(ImVec2(text_x, text_y), COL_TEXT, d.name.c_str());
    if(!d.badges.empty())
    {
        const float name_width = ImGui::CalcTextSize(d.name.c_str()).x;
        dl->AddText(ImVec2(text_x + name_width + 8.0f, text_y), COL_TEXT_DIM, d.badges.c_str());
    }
    dl->PopClipRect();
    dl->AddText(mono_font, font_size, ImVec2(time_x, text_y), COL_TEXT_DIM, time_buf);
    dl->AddRect(min, max, hovered ? COL_TRACK_BORDER_HOVER : COL_TRACK_BORDER, STRIP_ROUNDING);
    if(d.show_weight)
    {
        const float weight = std::clamp(d.weight, 0.0f, 1.0f);
        const float bar_y = max.y + 2.0f;
        dl->AddRectFilled(ImVec2(min.x, bar_y),
                          ImVec2(max.x, bar_y + WEIGHT_BAR_HEIGHT),
                          with_alpha(d.accent, 0.12f),
                          WEIGHT_BAR_HEIGHT * 0.5f);
        if(weight > 0.0f)
        {
            dl->AddRectFilled(ImVec2(min.x, bar_y),
                              ImVec2(min.x + width * weight, bar_y + WEIGHT_BAR_HEIGHT),
                              with_alpha(d.accent, 0.85f),
                              WEIGHT_BAR_HEIGHT * 0.5f);
        }
    }
    if(d.seekable && hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        const float mouse_t = std::clamp((ImGui::GetIO().MousePos.x - min.x) / width, 0.0f, 1.0f);
        const float ghost_x = min.x + width * mouse_t;
        dl->AddLine(ImVec2(ghost_x, min.y + 1.0f), ImVec2(ghost_x, max.y - 1.0f), with_alpha(COL_PLAYHEAD, 0.35f), 1.0f);
        if(d.tooltip.empty())
        {
            ImGui::SetTooltip("%s\n%.2f s - drag to seek", d.name.c_str(), mouse_t * d.duration);
        }
        else
        {
            ImGui::SetTooltip("%s\n%s\n%.2f s - drag to seek", d.name.c_str(), d.tooltip.c_str(), mouse_t * d.duration);
        }
    }
    else if(hovered && !d.tooltip.empty())
    {
        ImGui::SetTooltip("%s\n%s", d.name.c_str(), d.tooltip.c_str());
    }
    if(d.seekable && active)
    {
        const float mouse_t = std::clamp((ImGui::GetIO().MousePos.x - min.x) / width, 0.0f, 1.0f);
        player.seek(d.layer_index, mouse_t, d.seek_target);
        player.request_pose_refresh();
    }
}

/// Plots the crossfade: both weight curves sampled from the player's actual
/// easing function, a playhead at the current blend progress, and live
/// in / out weight readouts.
void draw_crossfade_graph(const animation_player& player,
                          const animation_player::animation_layer& layer,
                          const std::string& from_name,
                          const std::string& to_name)
{
    auto* dl = ImGui::GetWindowDrawList();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 60.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float total_height = GRAPH_LABEL_HEIGHT * 2.0f + GRAPH_CURVE_HEIGHT;
    ImGui::Dummy(ImVec2(width, total_height));
    const ImVec2 panel_min = pos;
    const ImVec2 panel_max(pos.x + width, pos.y + total_height);
    const float progress = std::clamp(player.get_blend_progress(layer), 0.0f, 1.0f);
    const float factor = player.get_blend_weight(layer);
    dl->AddRectFilled(panel_min, panel_max, COL_TRACK_BG, STRIP_ROUNDING);
    const float border_pulse = 0.35f + 0.25f * (0.5f + 0.5f * float(std::sin(ImGui::GetTime() * 4.0)));
    dl->AddRect(panel_min, panel_max, with_alpha(COL_TARGET, border_pulse), STRIP_ROUNDING);
    const ImVec2 gmin(panel_min.x + 1.0f, panel_min.y + GRAPH_LABEL_HEIGHT);
    const ImVec2 gmax(panel_max.x - 1.0f, panel_min.y + GRAPH_LABEL_HEIGHT + GRAPH_CURVE_HEIGHT);
    const float gw = gmax.x - gmin.x;
    const float gh = gmax.y - gmin.y;
    for(int i = 1; i < 4; ++i)
    {
        const float y = gmin.y + gh * 0.25f * float(i);
        dl->AddLine(ImVec2(gmin.x, y), ImVec2(gmax.x, y), COL_GRID, 1.0f);
        const float x = gmin.x + gw * 0.25f * float(i);
        dl->AddLine(ImVec2(x, gmin.y), ImVec2(x, gmax.y), COL_GRID, 1.0f);
    }
    ImVec2 target_points[CURVE_SAMPLE_COUNT + 1];
    ImVec2 source_points[CURVE_SAMPLE_COUNT + 1];
    for(int i = 0; i <= CURVE_SAMPLE_COUNT; ++i)
    {
        const float t = float(i) / float(CURVE_SAMPLE_COUNT);
        const float eased = player.compute_blend_factor(layer, t);
        target_points[i] = ImVec2(gmin.x + gw * t, gmax.y - gh * eased);
        source_points[i] = ImVec2(gmin.x + gw * t, gmax.y - gh * (1.0f - eased));
    }
    for(int i = 0; i < CURVE_SAMPLE_COUNT; ++i)
    {
        const float t_mid = (float(i) + 0.5f) / float(CURVE_SAMPLE_COUNT);
        const bool consumed = t_mid <= progress;
        const float x0 = target_points[i].x;
        const float x1 = target_points[i + 1].x;
        const float target_y = (target_points[i].y + target_points[i + 1].y) * 0.5f;
        const float source_y = (source_points[i].y + source_points[i + 1].y) * 0.5f;
        dl->AddRectFilled(ImVec2(x0, target_y), ImVec2(x1, gmax.y), with_alpha(COL_TARGET, consumed ? 0.28f : 0.10f));
        dl->AddRectFilled(ImVec2(x0, source_y), ImVec2(x1, gmax.y), with_alpha(COL_SOURCE, consumed ? 0.22f : 0.08f));
    }
    dl->AddPolyline(source_points, CURVE_SAMPLE_COUNT + 1, with_alpha(COL_SOURCE, 0.9f), ImDrawFlags_None, 1.5f);
    dl->AddPolyline(target_points, CURVE_SAMPLE_COUNT + 1, with_alpha(COL_TARGET, 0.95f), ImDrawFlags_None, 2.0f);
    const float playhead_x = gmin.x + gw * progress;
    dl->AddLine(ImVec2(playhead_x, gmin.y), ImVec2(playhead_x, gmax.y), COL_PLAYHEAD, 1.5f);
    const ImVec2 target_dot(playhead_x, gmax.y - gh * factor);
    const ImVec2 source_dot(playhead_x, gmax.y - gh * (1.0f - factor));
    dl->AddCircleFilled(source_dot, 3.5f, COL_SOURCE);
    dl->AddCircle(source_dot, 3.5f, COL_PLAYHEAD);
    dl->AddCircleFilled(target_dot, 3.5f, COL_TARGET);
    dl->AddCircle(target_dot, 3.5f, COL_PLAYHEAD);
    // Corner labels: names on top, weights below, timing in the bottom center.
    const float font_size = ImGui::GetFontSize();
    ImFont* mono_font = ImGui::GetFont(ImGui::Font::Mono);
    const float label_pad = 6.0f;
    const float top_y = panel_min.y + (GRAPH_LABEL_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f;
    const float bottom_y = panel_max.y - GRAPH_LABEL_HEIGHT + (GRAPH_LABEL_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f;
    const float half_width = width * 0.5f - label_pad * 2.0f;
    dl->PushClipRect(ImVec2(panel_min.x + label_pad, panel_min.y), ImVec2(panel_min.x + label_pad + half_width, gmin.y), true);
    dl->AddText(ImVec2(panel_min.x + label_pad, top_y), with_alpha(COL_SOURCE, 0.95f), from_name.c_str());
    dl->PopClipRect();
    const float to_width = ImGui::CalcTextSize(to_name.c_str()).x;
    const float to_x = std::max(panel_max.x - label_pad - to_width, panel_min.x + width * 0.5f + label_pad);
    dl->PushClipRect(ImVec2(panel_min.x + width * 0.5f, panel_min.y), ImVec2(panel_max.x - label_pad, gmin.y), true);
    dl->AddText(ImVec2(to_x, top_y), with_alpha(COL_TARGET, 0.95f), to_name.c_str());
    dl->PopClipRect();
    char buf[64];
    ImFormatString(buf, int(sizeof(buf)), "OUT %3.0f%%", (1.0f - factor) * 100.0f);
    dl->AddText(mono_font, font_size, ImVec2(panel_min.x + label_pad, bottom_y), with_alpha(COL_SOURCE, 0.95f), buf);
    ImFormatString(buf, int(sizeof(buf)), "IN %3.0f%%", factor * 100.0f);
    const float in_width = mono_font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf).x;
    dl->AddText(mono_font, font_size, ImVec2(panel_max.x - label_pad - in_width, bottom_y), with_alpha(COL_TARGET, 0.95f), buf);
    const auto* over_time = std::get_if<blend_over_time>(&layer.blending_state.state);
    const auto* over_param = std::get_if<blend_over_param>(&layer.blending_state.state);
    if(over_time != nullptr)
    {
        ImFormatString(buf,
                       int(sizeof(buf)),
                       "%.2f / %.2f s",
                       float(over_time->elapsed.count()),
                       float(over_time->duration.count()));
    }
    else if(over_param != nullptr)
    {
        ImFormatString(buf, int(sizeof(buf)), "param %.2f", over_param->param);
    }
    else
    {
        buf[0] = '\0';
    }
    if(buf[0] != '\0')
    {
        const float mid_width = mono_font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf).x;
        dl->AddText(mono_font,
                    font_size,
                    ImVec2(panel_min.x + (width - mid_width) * 0.5f, bottom_y),
                    COL_TEXT_DIM,
                    buf);
    }
    if(ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Crossfade progress %.0f%% (weight %.0f%% after easing).\n"
                          "Curves are sampled from the player's actual easing function.",
                          progress * 100.0f,
                          factor * 100.0f);
    }
}

void draw_blend_space_panel(const animation_player::animation_layer_state& layer_state)
{
    const auto& state = layer_state.state;
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_TEXT_DIM), ICON_MDI_GRID " Blend Space");
    if(!layer_state.parameters.empty())
    {
        ImGui::SameLine();
        ImGui::PushFont(ImGui::Font::Mono);
        std::string params_text = "[";
        char buf[32];
        for(size_t i = 0; i < layer_state.parameters.size(); ++i)
        {
            ImFormatString(buf, int(sizeof(buf)), i == 0 ? "%.2f" : ", %.2f", layer_state.parameters[i]);
            params_text += buf;
        }
        params_text += "]";
        ImGui::TextDisabled("%s", params_text.c_str());
        ImGui::PopFont();
        ImGui::SetItemTooltipEx("Current blend-space parameters.");
    }
    if(state.blend_clips.empty())
    {
        ImGui::TextDisabled("   Waiting for the first sample...");
        return;
    }
    size_t dominant = 0;
    for(size_t i = 1; i < state.blend_clips.size(); ++i)
    {
        if(state.blend_clips[i].weight > state.blend_clips[dominant].weight)
        {
            dominant = i;
        }
    }
    auto* dl = ImGui::GetWindowDrawList();
    ImFont* mono_font = ImGui::GetFont(ImGui::Font::Mono);
    const float font_size = ImGui::GetFontSize();
    const float row_height = ImGui::GetTextLineHeight() + 4.0f;
    for(size_t i = 0; i < state.blend_clips.size(); ++i)
    {
        const auto& clip_weight = state.blend_clips[i];
        const float weight = std::clamp(clip_weight.weight, 0.0f, 1.0f);
        auto name = clip_weight.clip.name();
        if(name.empty())
        {
            name = "Clip";
        }
        ImGui::PushID(int(i));
        const float width = std::max(ImGui::GetContentRegionAvail().x, 60.0f);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(width, row_height));
        const float name_width = width * 0.42f;
        const float pct_width = mono_font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, "100.0%").x;
        const float bar_x = min.x + name_width + 8.0f;
        const float bar_end = min.x + width - pct_width - 8.0f;
        const float text_y = min.y + (row_height - ImGui::GetTextLineHeight()) * 0.5f;
        const ImU32 accent = i == dominant ? COL_TARGET : COL_ACTIVE;
        dl->PushClipRect(min, ImVec2(min.x + name_width, min.y + row_height), true);
        dl->AddText(ImVec2(min.x, text_y), i == dominant ? COL_TEXT : COL_TEXT_DIM, name.c_str());
        dl->PopClipRect();
        const float bar_h = 5.0f;
        const float bar_y = min.y + (row_height - bar_h) * 0.5f;
        dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_end, bar_y + bar_h), with_alpha(accent, 0.12f), bar_h * 0.5f);
        if(weight > 0.0f)
        {
            dl->AddRectFilled(ImVec2(bar_x, bar_y),
                              ImVec2(bar_x + (bar_end - bar_x) * weight, bar_y + bar_h),
                              with_alpha(accent, i == dominant ? 0.9f : 0.55f),
                              bar_h * 0.5f);
        }
        char pct_buf[16];
        ImFormatString(pct_buf, int(sizeof(pct_buf)), "%5.1f%%", weight * 100.0f);
        dl->AddText(mono_font,
                    font_size,
                    ImVec2(min.x + width - pct_width, text_y),
                    i == dominant ? COL_TEXT : COL_TEXT_DIM,
                    pct_buf);
        if(ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s - weight %.3f", name.c_str(), clip_weight.weight);
        }
        ImGui::PopID();
    }
}

auto make_state_strip(const animation_player::animation_layer_state& layer_state, size_t layer_index) -> strip_desc
{
    const auto& state = layer_state.state;
    strip_desc d;
    d.layer_index = layer_index;
    d.duration = float(animation_player::get_state_duration(state).count());
    d.progress = state.get_progress();
    d.elapsed = d.progress * d.duration;
    d.seekable = d.duration > 0.0f;
    char buf[64];
    if(state.blend_space)
    {
        d.name = "Blend Space";
        ImFormatString(buf, int(sizeof(buf)), "%d %s", int(state.blend_clips.size()), state.blend_clips.size() == 1 ? "clip" : "clips");
        d.tooltip = buf;
        if(state.loop)
        {
            d.badges = ICON_MDI_SYNC;
        }
    }
    else if(state.clip)
    {
        d.name = state.clip.name();
        if(d.name.empty())
        {
            d.name = "Animation Clip";
        }
        if(state.loop)
        {
            if(state.loop_count > 0)
            {
                ImFormatString(buf, int(sizeof(buf)), ICON_MDI_SYNC " x%llu", (unsigned long long)state.loop_count);
                d.badges = buf;
            }
            else
            {
                d.badges = ICON_MDI_SYNC;
            }
        }
        auto clip_ptr = state.clip.get(false);
        if(clip_ptr)
        {
            if(clip_ptr->root_motion.get_apply_root_motion())
            {
                d.badges += d.badges.empty() ? "RM" : "  RM";
            }
            ImFormatString(buf,
                           int(sizeof(buf)),
                           "%d channels, %s%s",
                           int(clip_ptr->channels.size()),
                           state.loop ? "looping" : "one shot",
                           clip_ptr->root_motion.get_apply_root_motion() ? ", root motion" : "");
            d.tooltip = buf;
        }
        else
        {
            d.badges += d.badges.empty() ? "(loading)" : "  (loading)";
            d.seekable = false;
        }
    }
    else
    {
        // A mid-blend retarget froze the previously blended pose as the new
        // source: there is no clip behind it, only a static pose.
        d.name = "Pose Snapshot";
        d.tooltip = "Frozen blended pose kept as the crossfade source (retarget mid-blend).";
        d.progress = 0.0f;
        d.duration = 0.0f;
        d.seekable = false;
    }
    return d;
}

struct player_status
{
    const char* label{};
    ImU32 color{};
};

auto get_player_status(const animation_player& player) -> player_status
{
    if(player.is_playing())
    {
        return {"PLAYING", COL_PLAYING};
    }
    if(player.is_paused())
    {
        return {"PAUSED", COL_PAUSED};
    }
    return {"STOPPED", COL_STOPPED};
}

void draw_transport_controls(rtti::context& ctx, animation_component& data)
{
    auto& player = data.get_player();
    const auto& layers = player.get_layers();
    const bool playing = player.is_playing();
    const bool paused = player.is_paused();
    bool has_state = false;
    for(const auto& layer : layers)
    {
        has_state |= layer.current_state.is_valid() || layer.target_state.is_valid();
    }
    const bool can_seed = bool(data.get_animation());
    const ImVec2 button_size(ImGui::GetFrameHeight() * 1.6f, ImGui::GetFrameHeight());
    ImGui::BeginDisabled(playing || (!paused && !has_state && !can_seed));
    if(ImGui::Button(ICON_MDI_PLAY, button_size))
    {
        if(paused)
        {
            player.resume();
        }
        else
        {
            if(!has_state)
            {
                player.blend_to(0, data.get_animation());
            }
            player.play();
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("%s", paused ? "Resume playback." : "Play. Seeds the assigned clip when the player is empty.");
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::BeginDisabled(!playing);
    if(ImGui::Button(ICON_MDI_PAUSE, button_size))
    {
        player.pause();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("Pause playback. The pose stays on screen.");
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::BeginDisabled(!playing && !paused);
    if(ImGui::Button(ICON_MDI_STOP, button_size))
    {
        player.stop();
        player.request_pose_refresh();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("Stop playback and rewind every layer to its start.");
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::BeginDisabled(playing || !has_state);
    if(ImGui::Button(ICON_MDI_STEP_FORWARD, button_size))
    {
        player.update_time(animation_player::seconds_t(EDITOR_STEP_SECONDS * data.get_speed()), true);
        player.request_pose_refresh();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("Step one frame (1/60 s scaled by Speed) while stopped or paused.");
    const auto status = get_player_status(player);
    const bool scene_paused = ctx.get_cached<play_mode>().is_paused();
    char speed_buf[32];
    ImFormatString(speed_buf, int(sizeof(speed_buf)), ICON_MDI_PLAY_SPEED " x%.2f", data.get_speed());
    float right_width = calc_status_pill_width(status.label) + ImGui::GetStyle().ItemSpacing.x +
                        ImGui::CalcTextSize(speed_buf).x;
    if(scene_paused)
    {
        right_width += calc_status_pill_width("SCENE PAUSED") + ImGui::GetStyle().ItemSpacing.x;
    }
    ImGui::SameLine();
    ImGui::AlignedItem(1.0f,
                       ImGui::GetContentRegionAvail().x,
                       right_width,
                       [&]()
                       {
                           if(scene_paused)
                           {
                               draw_status_pill("SCENE PAUSED", COL_PAUSED, false);
                               ImGui::SetItemTooltipEx("Play mode is paused - animation time is frozen globally.");
                               ImGui::SameLine();
                           }
                           draw_status_pill(status.label, status.color, player.is_playing());
                           ImGui::SameLine();
                           ImGui::AlignTextToFramePadding();
                           ImGui::TextDisabled("%s", speed_buf);
                           ImGui::SetItemTooltipEx("Playback speed from the Speed property.");
                       });
}

/// The humanoid roles Rest Aligned retargeting found on this model: clips whose bone names differ
/// from the model's land on bones by these roles.

constexpr ImU32 COL_WARNING = IM_COL32(255, 185, 80, 255);
constexpr ImU32 COL_OK = IM_COL32(70, 200, 120, 255);
/// Width of a Retarget Check table's first column, in font sizes.
constexpr float RETARGET_NAME_COLUMN_FONTS = 10.0f;

/// Counts per kind of match that occurred, as "name 40, role 12".
auto describe_match_counts(const animation_retarget_check& check) -> std::string
{
    std::string text;
    for(size_t kind = 1; kind < RETARGET_MATCH_KIND_COUNT; ++kind)
    {
        if(check.match_counts[kind] > 0)
        {
            text += (text.empty() ? "" : ", ") + std::string(get_match_kind_name(animation_binding_report::match_kind(kind))) +
                    " " + std::to_string(check.match_counts[kind]);
        }
    }
    return text;
}

void draw_retarget_summary(const animation_retarget_check& check)
{
    const auto& report = check.report;
    ImGui::Text("Pose Retargeting in effect: %s", report.is_rest_aligned ? "Rest Aligned" : "Copy");
    ImGui::SetItemTooltipEx("How the clip's poses reach the bones: copied as authored, or mapped rest to rest. "
                            "Index Based, or a clip without a rest pose, plays copied whatever the setting.");
    if(report.is_rest_aligned)
    {
        ImGui::Text("Retarget pose: %s, %zu offsets", check.aligns_to_t_pose ? "T-Pose" : "Bind Pose", check.pose_offset_count);
        ImGui::SetItemTooltipEx("The pose motion is measured from and applied to (Retarget Pose and Retarget Pose "
                                "Offsets).");
    }
    ImGui::Text("%zu of %zu channels reach a bone (%s)",
                count_matched_channels(check),
                report.channels.size(),
                describe_match_counts(check).c_str());
    ImGui::Text("%zu bones unreached: they keep their bind pose", report.unreached_bones.size());
    if(report.is_rest_aligned)
    {
        ImGui::Text("Height ratio %.3f, frame turn %.0f deg about (%.2f, %.2f, %.2f)",
                    report.height_ratio,
                    report.frame_degrees,
                    report.frame_axis.x,
                    report.frame_axis.y,
                    report.frame_axis.z);
        ImGui::SetItemTooltipEx("Height ratio: this skeleton's hip height over the clip's. Motion scales by it.\n"
                                "Frame turn: how the clip's skeleton stands turned against this one (a Z-up export "
                                "turns 90 degrees about X).");
    }
    if(report.root_position_channel >= 0)
    {
        ImGui::Text("Root motion: %s -> %s",
                    report.channels[size_t(report.root_position_channel)].name.c_str(),
                    get_retarget_bone_name(check, report.root_position_bone).c_str());
    }
    for(const auto& warning : check.warnings)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_WARNING);
        ImGui::TextWrapped(ICON_MDI_ALERT " %s", warning.c_str());
        ImGui::PopStyleColor();
    }
    if(check.warnings.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_OK);
        ImGui::TextUnformatted(ICON_MDI_CHECK_CIRCLE_OUTLINE " No problems found.");
        ImGui::PopStyleColor();
    }
}

void draw_retarget_channels(const animation_retarget_check& check, bool& shows_unmatched_only)
{
    const auto& channels = check.report.channels;
    const std::string title = "Channels (" + std::to_string(channels.size()) + ")###retarget_channels";
    if(!ImGui::TreeNode(title.c_str()))
    {
        return;
    }
    ImGui::Checkbox("Unmatched only", &shows_unmatched_only);
    if(ImGui::BeginTable("retarget_channels_table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * RETARGET_NAME_COLUMN_FONTS);
        ImGui::TableSetupColumn("Bone");
        ImGui::TableSetupColumn("Match", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * RETARGET_NAME_COLUMN_FONTS * 0.6f);
        ImGui::TableHeadersRow();
        for(const auto& channel : channels)
        {
            const bool is_matched = channel.match != animation_binding_report::match_kind::none;
            if(shows_unmatched_only && is_matched)
            {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(channel.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(get_retarget_bone_name(check, channel.bone).c_str());
            ImGui::TableNextColumn();
            if(is_matched)
            {
                ImGui::TextUnformatted(get_match_kind_name(channel.match));
            }
            else
            {
                ImGui::TextDisabled("%s", get_match_kind_name(channel.match));
            }
        }
        ImGui::EndTable();
    }
    ImGui::TreePop();
}

void draw_retarget_unreached(const animation_retarget_check& check)
{
    const auto& bones = check.report.unreached_bones;
    const std::string title = "Unreached bones (" + std::to_string(bones.size()) + ")###retarget_unreached";
    if(!ImGui::TreeNode(title.c_str()))
    {
        return;
    }
    for(const int bone : bones)
    {
        ImGui::BulletText("%s", get_retarget_bone_name(check, bone).c_str());
    }
    ImGui::TreePop();
}

void draw_retarget_rest_poses(const animation_retarget_check& check)
{
    const auto& differences = check.report.rest_differences;
    if(differences.empty() || !ImGui::TreeNode("Rest poses###retarget_rest"))
    {
        return;
    }
    ImGui::TextWrapped("How far each limb's rest direction differs between the clip's skeleton and this one. Above "
                       "%.0f degrees the two rest in different poses.",
                       REST_POSE_WARNING_DEGREES);
    if(ImGui::BeginTable("retarget_rest_table", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * RETARGET_NAME_COLUMN_FONTS);
        ImGui::TableSetupColumn("Difference");
        ImGui::TableHeadersRow();
        for(const auto& difference : differences)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(get_humanoid_bone_name(difference.role));
            ImGui::TableNextColumn();
            const bool is_far = difference.degrees > REST_POSE_WARNING_DEGREES;
            if(is_far)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, COL_WARNING);
            }
            ImGui::Text("%.1f deg", difference.degrees);
            if(is_far)
            {
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndTable();
    }
    ImGui::TreePop();
}

} // namespace

void inspector_animation_component::draw_humanoid_section(rtti::context& ctx, animation_component& data)
{
    auto owner = data.get_owner();
    auto* model = owner && owner.valid() ? owner.try_get<model_component>() : nullptr;
    if(model == nullptr || data.get_pose_retargeting() != animation_pose_retargeting::rest_aligned)
    {
        return;
    }
    const auto& skeleton = animation_system::get_target_skeleton(data, *model);
    if(!skeleton)
    {
        return;
    }
    const size_t found = count_humanoid_bones(skeleton->humanoid);
    ImGui::Spacing();
    const std::string title = std::string(ICON_MDI_HUMAN " Humanoid Mapping (") + std::to_string(found) + " of " +
                              std::to_string(HUMANOID_BONE_COUNT) + ")###humanoid_mapping";
    if(!ImGui::CollapsingHeader(title.c_str()))
    {
        return;
    }
    const std::string model_key = get_editable_model_key(*model);
    ImGui::TextWrapped("Clips whose bone names differ from this skeleton's (Mixamo, Rigify, VRM, Biped...) land on "
                       "these bones by role. Found from the skeleton's shape. Names only tell left from right.");
    ImGui::TextWrapped("%s",
                       model_key.empty() ? "This model's roles cannot be set by hand: it is not a model file of the project."
                                         : "Set a role by hand with its " ICON_MDI_PENCIL " button. It is saved in the "
                                           "model's import settings and holds for its clips too.");
    if(!ImGui::BeginTable("humanoid_roles", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
    {
        return;
    }
    const auto overrides = model_key.empty() ? std::vector<humanoid_bone_override>{} : get_model_humanoid_overrides(ctx, model_key);
    ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9.0f);
    ImGui::TableSetupColumn("Bone");
    ImGui::TableSetupColumn("##set", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    for(size_t role = 0; role < HUMANOID_BONE_COUNT; ++role)
    {
        ImGui::PushID(int(role));
        const int bone = skeleton->humanoid[role];
        const bool is_set_by_hand = std::any_of(overrides.begin(),
                                                overrides.end(),
                                                [role](const humanoid_bone_override& entry)
                                                {
                                                    return entry.role == humanoid_bone(role);
                                                });
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(get_humanoid_bone_name(humanoid_bone(role)));
        ImGui::TableNextColumn();
        if(bone >= 0 && size_t(bone) < skeleton->names.size())
        {
            // Target skeleton indices are armature indices: a click selects the bone.
            if(ImGui::Selectable(skeleton->names[size_t(bone)].c_str()))
            {
                if(auto entity = model->get_armature_by_index(size_t(bone)))
                {
                    ctx.get_cached<editing_manager>().select(entity);
                }
            }
            ImGui::SetItemTooltipEx("Select the bone");
        }
        else
        {
            ImGui::TextDisabled("-");
        }
        if(is_set_by_hand)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(set by hand)");
        }
        ImGui::TableNextColumn();
        if(!model_key.empty())
        {
            if(ImGui::SmallButton(ICON_MDI_PENCIL))
            {
                role_bone_filter_.Clear();
                ImGui::OpenPopup("role_menu");
            }
            const ImVec2 anchor = ImGui::GetItemRectMax();
            ImGui::SetItemTooltipEx("Set this role by hand");
            draw_role_menu(ctx, model_key, humanoid_bone(role), *skeleton, anchor);
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void inspector_animation_component::draw_role_menu(rtti::context& ctx,
                                                   const std::string& model_key,
                                                   humanoid_bone role,
                                                   const animation_target_skeleton& skeleton,
                                                   const ImVec2& anchor)
{
    // The buttons sit at the inspector's right edge: opening leftwards keeps the menu over it.
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
    if(!ImGui::BeginPopup("role_menu"))
    {
        return;
    }
    ImGui::TextDisabled("%s", get_humanoid_bone_name(role));
    std::optional<std::optional<std::string>> choice;
    if(ImGui::Selectable("Automatic (found from the shape)"))
    {
        choice.emplace(std::nullopt);
    }
    if(ImGui::Selectable("None (this skeleton has no such bone)"))
    {
        choice.emplace(std::string{});
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    if(ImGui::InputTextWithHint("##filter",
                                ICON_MDI_MAGNIFY " Filter bones",
                                role_bone_filter_.InputBuf,
                                IM_ARRAYSIZE(role_bone_filter_.InputBuf)))
    {
        role_bone_filter_.Build();
    }
    if(ImGui::BeginChild("##bones", ImVec2(ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 16.0f)))
    {
        for(const auto& name : skeleton.names)
        {
            if(!role_bone_filter_.PassFilter(name.c_str()))
            {
                continue;
            }
            if(ImGui::Selectable(name.c_str()))
            {
                choice.emplace(name);
            }
        }
    }
    ImGui::EndChild();
    if(choice)
    {
        if(!set_model_humanoid_override(ctx, model_key, role, *choice))
        {
            APPLOG_ERROR("Could not write the import settings of {}", model_key);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void inspector_animation_component::draw_retarget_section(rtti::context& ctx, animation_component& data)
{
    auto owner = data.get_owner();
    auto* model = owner && owner.valid() ? owner.try_get<model_component>() : nullptr;
    if(model == nullptr)
    {
        return;
    }
    ImGui::Spacing();
    if(!ImGui::CollapsingHeader(ICON_MDI_CLIPBOARD_CHECK_OUTLINE " Retarget Check###retarget_check"))
    {
        return;
    }
    ImGui::PushID("retarget_check");
    // A clip picked for another character does not carry over.
    if(retarget_key_.component != &data)
    {
        retarget_clip_ = {};
    }
    {
        inspect_without_undo_scope no_undo;
        property_layout layout("Clip",
                               "The clip to check. Empty: the clip the base layer plays, else the one the controller "
                               "starts in, else the Animation clip.");
        entt::meta_any clip_var = entt::forward_as_meta(retarget_clip_);
        var_info clip_info;
        clip_info.is_property = true;
        inspect_var(ctx, clip_var, make_proxy(clip_var), clip_info);
    }
    const auto handle = retarget_clip_ ? retarget_clip_ : get_default_retarget_clip(data);
    const auto clip = handle ? handle.try_get() : nullptr;
    if(!clip || asset_handle<animation_clip>::is_empty(clip))
    {
        ImGui::TextDisabled("%s", handle ? "Loading the clip..." : "Pick a clip, or play one, to see how it lands on this skeleton.");
        retarget_key_.component = &data;
        ImGui::PopID();
        return;
    }
    const retarget_key key{&data,
                           handle.uid(),
                           handle.version(),
                           animation_system::get_skeleton_version(*model),
                           data.get_retargeting_mode(),
                           data.get_pose_retargeting()};
    if(!(key == retarget_key_))
    {
        retarget_check_ = check_animation_retarget(data, *model, clip);
        retarget_key_ = key;
    }
    if(!retarget_clip_)
    {
        ImGui::TextDisabled("Checking %s, the clip this character plays.", handle.name().c_str());
    }
    draw_retarget_summary(retarget_check_);
    draw_retarget_channels(retarget_check_, shows_unmatched_only_);
    draw_retarget_unreached(retarget_check_);
    draw_retarget_rest_poses(retarget_check_);
    ImGui::PopID();
}

auto inspector_animation_component::inspect(rtti::context& ctx,
                                            entt::meta_any& var,
                                            const meta_any_proxy& var_proxy,
                                            const var_info& info,
                                            const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<animation_component&>();
    ImGui::PushID("animation_player_section");
    draw_player_section(ctx, data);
    ImGui::PopID();
    draw_humanoid_section(ctx, data);
    draw_retarget_section(ctx, data);
    return result;
}

void inspector_animation_component::draw_player_section(rtti::context& ctx, animation_component& data)
{
    auto& player = data.get_player();
    update_traces(player);
    ImGui::Spacing();
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::SeparatorText(ICON_MDI_ANIMATION_PLAY " Player");
    ImGui::PopFont();
    draw_transport_controls(ctx, data);
    ImGui::Spacing();
    const auto& layers = player.get_layers();
    bool has_state = false;
    for(const auto& layer : layers)
    {
        has_state |= layer.current_state.is_valid() || layer.target_state.is_valid();
    }
    if(!has_state)
    {
        const char* hint = "No active animation states";
        ImGui::AlignedItem(0.5f,
                           ImGui::GetContentRegionAvail().x,
                           ImGui::CalcTextSize(hint).x,
                           [&]()
                           {
                               ImGui::TextDisabled("%s", hint);
                           });
        const char* sub_hint = "Press Play, or enter play mode with Auto Play enabled.";
        ImGui::AlignedItem(0.5f,
                           ImGui::GetContentRegionAvail().x,
                           ImGui::CalcTextSize(sub_hint).x,
                           [&]()
                           {
                               ImGui::TextDisabled("%s", sub_hint);
                           });
        return;
    }
    const bool playing = player.is_playing();
    for(size_t i = 0; i < layers.size(); ++i)
    {
        const auto& layer = layers[i];
        const bool crossfading = layer.target_state.is_valid();
        const auto& current = layer.current_state;
        ImGui::PushID(int(i));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, CARD_ROUNDING);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_CARD_BG);
        ImGui::BeginChild("layer_card",
                          ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        {
            ImU32 dot_color = COL_STOPPED;
            if(crossfading)
            {
                dot_color = COL_TARGET;
            }
            else if(current.is_valid() && playing)
            {
                dot_color = COL_PLAYING;
            }
            auto* dl = ImGui::GetWindowDrawList();
            const ImVec2 dot_pos = ImGui::GetCursorScreenPos();
            const float line_height = ImGui::GetTextLineHeight();
            dl->AddCircleFilled(ImVec2(dot_pos.x + 4.0f, dot_pos.y + line_height * 0.5f), 3.5f, dot_color);
            ImGui::Dummy(ImVec2(12.0f, line_height));
            ImGui::SameLine();
            ImGui::PushFont(ImGui::Font::SemiBold);
            ImGui::Text("LAYER %d", int(i));
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", i == 0 ? "BASE" : "ADDITIVE");
            if(i != 0)
            {
                ImGui::SetItemTooltipEx("Layers above the base are blended additively over the layers below.");
            }
            char state_buf[64];
            ImU32 state_color = COL_TEXT_DIM;
            if(crossfading)
            {
                ImFormatString(state_buf,
                               int(sizeof(state_buf)),
                               ICON_MDI_SWAP_HORIZONTAL " Crossfade %.0f%%",
                               player.get_blend_weight(layer) * 100.0f);
                state_color = COL_TARGET;
            }
            else if(current.state.blend_space)
            {
                ImFormatString(state_buf, int(sizeof(state_buf)), ICON_MDI_GRID " Blend Space");
            }
            else if(current.is_valid())
            {
                ImFormatString(state_buf, int(sizeof(state_buf)), "%s", playing ? ICON_MDI_PLAY " Playing" : "Ready");
            }
            else
            {
                ImFormatString(state_buf, int(sizeof(state_buf)), "Idle");
            }
            ImGui::SameLine();
            ImGui::AlignedItem(1.0f,
                               ImGui::GetContentRegionAvail().x,
                               ImGui::CalcTextSize(state_buf).x,
                               [&]()
                               {
                                   ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(state_color), "%s", state_buf);
                               });
        }
        ImGui::Spacing();
        if(crossfading)
        {
            const float factor = player.get_blend_weight(layer);
            auto source_strip = make_state_strip(current, i);
            source_strip.id = "source_strip";
            source_strip.role_letter = "A";
            source_strip.accent = COL_SOURCE;
            source_strip.weight = 1.0f - factor;
            source_strip.show_weight = true;
            draw_clip_strip(player, source_strip);
            ImGui::Spacing();
            auto target_strip = make_state_strip(layer.target_state, i);
            target_strip.id = "target_strip";
            target_strip.role_letter = "B";
            target_strip.accent = COL_TARGET;
            target_strip.weight = factor;
            target_strip.show_weight = true;
            target_strip.seek_target = true;
            draw_crossfade_graph(player, layer, source_strip.name, target_strip.name);
            ImGui::Spacing();
            draw_clip_strip(player, target_strip);
        }
        else if(current.is_valid())
        {
            auto strip = make_state_strip(current, i);
            strip.id = "current_strip";
            strip.accent = playing ? COL_ACTIVE : COL_STOPPED;
            draw_clip_strip(player, strip);
        }
        else
        {
            ImGui::TextDisabled("Idle - no clip or blend space bound.");
        }
        if(current.state.blend_space)
        {
            ImGui::Spacing();
            draw_blend_space_panel(current);
        }
        draw_trace(i);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::PopID();
        ImGui::Spacing();
    }
}

void inspector_animation_component::update_traces(const animation_player& player)
{
    if(traced_player_ != &player)
    {
        traced_player_ = &player;
        traces_.clear();
        last_trace_time_ = 0.0;
    }
    const auto& layers = player.get_layers();
    traces_.resize(layers.size());
    const double now = ImGui::GetTime();
    if(now - last_trace_time_ < TRACE_SAMPLE_INTERVAL)
    {
        return;
    }
    last_trace_time_ = now;
    for(size_t i = 0; i < layers.size(); ++i)
    {
        const auto& layer = layers[i];
        const float value = layer.target_state.is_valid() ? player.get_blend_weight(layer) : 0.0f;
        auto& trace = traces_[i];
        trace.samples[trace.head] = value;
        trace.head = (trace.head + 1) % blend_trace::capacity;
        trace.count = std::min(trace.count + 1, blend_trace::capacity);
    }
}

void inspector_animation_component::draw_trace(size_t layer_index) const
{
    if(layer_index >= traces_.size())
    {
        return;
    }
    const auto& trace = traces_[layer_index];
    if(trace.count == 0)
    {
        return;
    }
    bool any_activity = false;
    for(size_t i = 0; i < trace.count; ++i)
    {
        any_activity |= trace.samples[i] > 0.001f;
    }
    if(!any_activity)
    {
        return;
    }
    ImGui::Spacing();
    auto* dl = ImGui::GetWindowDrawList();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 60.0f);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, TRACE_HEIGHT));
    const ImVec2 max(min.x + width, min.y + TRACE_HEIGHT);
    dl->AddRectFilled(min, max, with_alpha(COL_TRACK_BG, 0.6f), 3.0f);
    const float slice_width = width / float(blend_trace::capacity);
    for(size_t k = 0; k < trace.count; ++k)
    {
        const size_t sample_index = (trace.head + blend_trace::capacity - trace.count + k) % blend_trace::capacity;
        const float value = std::clamp(trace.samples[sample_index], 0.0f, 1.0f);
        if(value <= 0.001f)
        {
            continue;
        }
        // Newest samples land at the right edge.
        const float x1 = max.x - float(trace.count - k - 1) * slice_width;
        const float x0 = x1 - slice_width;
        dl->AddRectFilled(ImVec2(x0, max.y - (TRACE_HEIGHT - 3.0f) * value - 1.0f),
                          ImVec2(x1, max.y - 1.0f),
                          with_alpha(COL_TARGET, 0.5f));
    }
    dl->AddRect(min, max, COL_TRACK_BORDER, 3.0f);
    dl->AddText(ImVec2(min.x + 5.0f, min.y + 2.0f), COL_TEXT_DIM, ICON_MDI_CHART_TIMELINE_VARIANT);
    if(ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Blend activity - crossfade weight over the last %.0f seconds.",
                          double(blend_trace::capacity) * TRACE_SAMPLE_INTERVAL);
    }
}

} // namespace unravel
