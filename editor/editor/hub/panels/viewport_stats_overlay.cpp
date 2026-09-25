#include "viewport_stats_overlay.h"
#include "panel_section.h"
#include "panel_toolbar.h"
#include "editor/format/format_bytes.h"
#include "editor/hub/panels/inspector_panel/inspectors/inspector_container_widgets.h"
#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"
#include "editor/imgui/integration/imgui.h"
#include "editor/imgui/integration/imgui_style.h"
#include "editor/system/project_manager.h"
#include "imgui_widgets/tooltips.h"
#include "imgui_widgets/utils.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <engine/settings/settings.h>
#include <graphics/eviction.h>
#include <graphics/graphics.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <string>

namespace unravel::viewport_stats_overlay
{
namespace
{
using container_widgets::to_pixels;

// Sizes are in units of the font size, so the overlay follows the UI scale of the editor.
constexpr float OVERLAY_WIDTH = 21.0f;
constexpr float OVERLAY_MIN_HEIGHT = 6.0f;
constexpr float HEADER_HEIGHT = 1.6f;
constexpr float TITLE_ICON_GAP = 0.4f;
constexpr float FRAME_RATE_SCALE = 2.0f;
constexpr float UNIT_GAP = 0.3f;
constexpr float GRAPH_HEIGHT = 2.6f;
constexpr float GRAPH_LINE_THICKNESS = 0.09f;
constexpr float CARD_ROUNDING = 0.3f;
constexpr float CARD_PADDING = 0.45f;
constexpr float ITEM_GAP = 0.4f;
constexpr float ROW_SPACING = 0.22f;
constexpr float CAPTION_SCALE = 0.85f;
constexpr float PAIR_COLUMN_WIDTH = 4.5f;
constexpr float METER_HEIGHT = 0.3f;
constexpr float SWITCH_WIDTH = 1.9f;
constexpr float SWITCH_HEIGHT = 1.05f;
constexpr float SWITCH_KNOB_INSET = 0.14f;

constexpr ImU32 WASH_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 METER_TRACK_COLOR = IM_COL32(255, 255, 255, 24);
/// The tick of a second level on a meter: the watermark paging evicts down to.
constexpr ImU32 METER_MARKER_COLOR = IM_COL32(255, 255, 255, 130);
constexpr ImU32 GUIDE_COLOR = IM_COL32(255, 255, 255, 48);
constexpr ImU32 SWITCH_OFF_COLOR = IM_COL32(255, 255, 255, 46);
constexpr ImU32 SWITCH_OFF_HOVERED_COLOR = IM_COL32(255, 255, 255, 70);
constexpr ImU32 SWITCH_KNOB_COLOR = IM_COL32(255, 255, 255, 240);
constexpr float SWITCH_ON_ALPHA = 0.85f;
constexpr float GRAPH_FILL_ALPHA = 0.18f;

// Status colors of the readouts, and the softer fills of the meters.
constexpr ImVec4 COLOR_GOOD{0.45f, 0.80f, 0.45f, 1.0f};
constexpr ImVec4 COLOR_WARNING{0.95f, 0.70f, 0.25f, 1.0f};
constexpr ImVec4 COLOR_BAD{0.95f, 0.40f, 0.40f, 1.0f};
constexpr ImVec4 METER_GOOD{0.30f, 0.49f, 0.32f, 1.0f};
constexpr ImVec4 METER_WARNING{0.58f, 0.45f, 0.20f, 1.0f};
constexpr ImVec4 METER_BAD{0.55f, 0.28f, 0.28f, 1.0f};

constexpr float FPS_SMOOTH = 55.0f;
constexpr float FPS_PLAYABLE = 30.0f;
constexpr float MEMORY_WARNING_PERCENT = 60.0f;
constexpr float MEMORY_BAD_PERCENT = 80.0f;
constexpr float PERCENT = 100.0f;
constexpr float MILLISECONDS_PER_SECOND = 1000.0f;
/// The graph marks the frame time of 60 FPS, and grows its range for anything slower.
constexpr float TARGET_FRAME_MS = MILLISECONDS_PER_SECOND / 60.0f;
constexpr float GRAPH_MIN_RANGE_MS = TARGET_FRAME_MS * 1.5f;
constexpr float GRAPH_HEADROOM = 1.1f;
/// From here on the toolbar reserves a fourth digit for its frame rate readout.
constexpr float FPS_FOUR_DIGITS = 999.5f;
constexpr std::uint32_t THOUSAND = 1000;
constexpr std::uint32_t MILLION = 1000000;

auto get_accent_color(float alpha = 1.0f) -> ImU32
{
    ImVec4 accent = imgui_style::get_accent_color();
    accent.w *= alpha;
    return ImGui::GetColorU32(accent);
}

auto get_fps_color(float fps) -> ImVec4
{
    if(fps < FPS_PLAYABLE)
    {
        return COLOR_BAD;
    }
    if(fps < FPS_SMOOTH)
    {
        return COLOR_WARNING;
    }
    return COLOR_GOOD;
}

auto get_memory_color(float percent) -> ImVec4
{
    if(percent > MEMORY_BAD_PERCENT)
    {
        return COLOR_BAD;
    }
    if(percent > MEMORY_WARNING_PERCENT)
    {
        return COLOR_WARNING;
    }
    return COLOR_GOOD;
}

auto get_memory_meter_color(float percent) -> ImVec4
{
    if(percent > MEMORY_BAD_PERCENT)
    {
        return METER_BAD;
    }
    if(percent > MEMORY_WARNING_PERCENT)
    {
        return METER_WARNING;
    }
    return METER_GOOD;
}

auto format_count(std::uint32_t count) -> std::string
{
    if(count >= MILLION)
    {
        return fmt::format("{:.1f}M", static_cast<double>(count) / static_cast<double>(MILLION));
    }
    if(count >= THOUSAND)
    {
        return fmt::format("{:.1f}k", static_cast<double>(count) / static_cast<double>(THOUSAND));
    }
    return fmt::format("{}", count);
}

/// Text drawn with its baseline on the given one, so a small unit sits on a large number.
void draw_text_on_baseline(ImDrawList* draw_list, float x, float baseline, ImU32 color, const char* text)
{
    draw_list->AddText(ImVec2(x, baseline - ImGui::GetFontBaked()->Ascent), color, text);
}

struct header_request
{
    bool is_profiler_pressed{};
    bool is_close_pressed{};
};

/// The title, with the buttons that open the profiler and hide the overlay at its right end.
auto draw_header() -> header_request
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = to_pixels(HEADER_HEIGHT);
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float center_y = min.y + height * 0.5f;
    ImGui::PushFont(ImGui::Font::Bold);
    const float icon_width = ImGui::GetFontSize();
    ImGui::RenderIconCentered(draw_list, ImVec2(min.x + icon_width * 0.5f, center_y), ICON_MDI_CHART_LINE, get_accent_color());
    draw_list->AddText(ImVec2(min.x + icon_width + to_pixels(TITLE_ICON_GAP), center_y - ImGui::GetTextLineHeight() * 0.5f),
                       ImGui::GetColorU32(ImGuiCol_Text),
                       "Statistics");
    ImGui::PopFont();

    header_request request{};
    const ImVec2 button_size(height, height);
    const ImVec2 close_min(min.x + width - height, min.y);
    request.is_close_pressed =
        container_widgets::draw_icon_button({"##close", ICON_MDI_CLOSE, "Hide the statistics"},
                                            ImRect(close_min, close_min + button_size));
    const ImVec2 profiler_min(close_min.x - height, min.y);
    request.is_profiler_pressed =
        container_widgets::draw_icon_button({"##profiler", ICON_MDI_CHART_GANTT, "Open the profiler"},
                                            ImRect(profiler_min, profiler_min + button_size));
    return request;
}

void record_frame_time(state& overlay_state)
{
    const int frame = ImGui::GetFrameCount();
    if(overlay_state.last_sample_frame == frame)
    {
        return;
    }
    if(overlay_state.last_sample_frame != frame - 1)
    {
        overlay_state.frame_cursor = 0;
        overlay_state.frame_count = 0;
    }
    overlay_state.last_sample_frame = frame;
    overlay_state.frame_times_ms[overlay_state.frame_cursor] = ImGui::GetIO().DeltaTime * MILLISECONDS_PER_SECOND;
    overlay_state.frame_cursor = (overlay_state.frame_cursor + 1) % FRAME_HISTORY_SIZE;
    overlay_state.frame_count = std::min(overlay_state.frame_count + 1, FRAME_HISTORY_SIZE);
}

/// The frame rate as a large number in its status color, with the frame time at the right end.
void draw_frame_rate()
{
    const float fps = ImGui::GetIO().Framerate;
    const float frame_ms = fps > 0.0f ? MILLISECONDS_PER_SECOND / fps : 0.0f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const std::string value = fmt::format("{:.0f}", fps);
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushWindowFontScale(FRAME_RATE_SCALE);
    const ImVec2 value_size = ImGui::CalcTextSize(value.c_str());
    const float baseline = min.y + ImGui::GetFontBaked()->Ascent;
    draw_list->AddText(min, ImGui::GetColorU32(get_fps_color(fps)), value.c_str());
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    draw_text_on_baseline(draw_list, min.x + value_size.x + to_pixels(UNIT_GAP), baseline, imgui_style::get_muted_text_color_u32(), "FPS");

    const std::string frame_text = fmt::format("{:.2f} ms", frame_ms);
    ImGui::PushFont(ImGui::Font::Mono);
    const float frame_text_width = ImGui::CalcTextSize(frame_text.c_str()).x;
    draw_text_on_baseline(draw_list, min.x + width - frame_text_width, baseline, imgui_style::get_muted_text_color_u32(), frame_text.c_str());
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(width, value_size.y));
    ImGui::SetItemTooltipEx("%s",
                            "Frames per second, averaged over the last 60 frames, and the time of one frame.\n"
                            "Green above 55 FPS, yellow from 30, red below 30.");
}

/// The frame times of the last frames as a line over a filled area, newest at the right, with a
/// guide at the frame time of 60 FPS.
void draw_frame_graph(const state& overlay_state)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = to_pixels(GRAPH_HEIGHT);
    ImGui::Dummy(ImVec2(width, height));
    ImGui::SetItemTooltipEx("Frame time of the last %d frames. The line marks 60 FPS (%.1f ms).",
                            static_cast<int>(FRAME_HISTORY_SIZE),
                            static_cast<double>(TARGET_FRAME_MS));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float rounding = to_pixels(CARD_ROUNDING);
    draw_list->AddRectFilled(min, min + ImVec2(width, height), WASH_COLOR, rounding);
    // Kept clear of the rounded corners of the background.
    const ImRect plot(ImVec2(min.x + rounding, min.y), ImVec2(min.x + width - rounding, min.y + height));

    const std::size_t count = overlay_state.frame_count;
    const auto get_sample = [&](std::size_t age) -> float
    {
        const std::size_t size = FRAME_HISTORY_SIZE;
        return overlay_state.frame_times_ms[(overlay_state.frame_cursor + size - 1 - age) % size];
    };
    float range_ms = GRAPH_MIN_RANGE_MS;
    for(std::size_t age = 0; age < count; ++age)
    {
        range_ms = std::max(range_ms, get_sample(age) * GRAPH_HEADROOM);
    }
    const auto to_y = [&](float ms) -> float
    {
        return plot.Max.y - std::min(ms / range_ms, 1.0f) * plot.GetHeight();
    };
    const float guide_y = ImFloor(to_y(TARGET_FRAME_MS)) + 0.5f;
    draw_list->AddLine(ImVec2(plot.Min.x, guide_y), ImVec2(plot.Max.x, guide_y), GUIDE_COLOR);
    if(count < 2)
    {
        return;
    }

    // The samples, then the two corners that close the area under them.
    std::array<ImVec2, FRAME_HISTORY_SIZE + 2> points{};
    const float step = plot.GetWidth() / static_cast<float>(FRAME_HISTORY_SIZE - 1);
    for(std::size_t index = 0; index < count; ++index)
    {
        const std::size_t age = count - 1 - index;
        points[index] = ImVec2(plot.Max.x - static_cast<float>(age) * step, to_y(get_sample(age)));
    }
    points[count] = ImVec2(points[count - 1].x, plot.Max.y);
    points[count + 1] = ImVec2(points[0].x, plot.Max.y);
    draw_list->AddConcavePolyFilled(points.data(), static_cast<int>(count + 2), get_accent_color(GRAPH_FILL_ALPHA));
    draw_list->AddPolyline(points.data(),
                           static_cast<int>(count),
                           get_accent_color(),
                           ImDrawFlags_None,
                           ImGui::GetFontSize() * GRAPH_LINE_THICKNESS);
}

struct timing_tile
{
    const char* caption{};
    std::string value;
    const char* tooltip{};
};

/// Tiles side by side: a muted caption over the value.
void draw_tiles(const std::array<timing_tile, 3>& tiles)
{
    const float gap = to_pixels(ITEM_GAP);
    const float count = static_cast<float>(tiles.size());
    const float width = ImFloor((ImGui::GetContentRegionAvail().x - gap * (count - 1.0f)) / count);
    const float padding = to_pixels(CARD_PADDING);
    ImGui::PushWindowFontScale(CAPTION_SCALE);
    const float caption_height = ImGui::GetTextLineHeight();
    ImGui::PopWindowFontScale();
    const float height = padding * 2.0f + caption_height + ImGui::GetTextLineHeight();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for(std::size_t index = 0; index < tiles.size(); ++index)
    {
        if(index > 0)
        {
            ImGui::SameLine(0.0f, gap);
        }
        ImGui::Dummy(ImVec2(width, height));
        ImGui::SetItemTooltipEx("%s", tiles[index].tooltip);
        const ImVec2 min = ImGui::GetItemRectMin();
        draw_list->AddRectFilled(min, ImGui::GetItemRectMax(), WASH_COLOR, to_pixels(CARD_ROUNDING));
        ImGui::PushWindowFontScale(CAPTION_SCALE);
        draw_list->AddText(min + ImVec2(padding, padding), imgui_style::get_muted_text_color_u32(), tiles[index].caption);
        ImGui::PopWindowFontScale();
        ImGui::PushFont(ImGui::Font::Mono);
        draw_list->AddText(ImVec2(min.x + padding, min.y + padding + caption_height),
                           ImGui::GetColorU32(ImGuiCol_Text),
                           tiles[index].value.c_str());
        ImGui::PopFont();
    }
}

void draw_timing_tiles()
{
    const auto* stats = bgfx::getStats();
    const double to_cpu_ms = static_cast<double>(MILLISECONDS_PER_SECOND) / static_cast<double>(stats->cpuTimerFreq);
    const double to_gpu_ms = static_cast<double>(MILLISECONDS_PER_SECOND) / static_cast<double>(stats->gpuTimerFreq);
    const double cpu_ms = static_cast<double>(stats->cpuTimeEnd - stats->cpuTimeBegin) * to_cpu_ms;
    const double gpu_ms = static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * to_gpu_ms;
    const int latency = static_cast<int>(stats->maxGpuLatency);
    draw_tiles({{
        {"CPU", fmt::format("{:.2f} ms", cpu_ms), "Time the CPU spent submitting render commands to the driver.\n"
                                                  "High values point to a CPU bound frame."},
        {"GPU", fmt::format("{:.2f} ms", gpu_ms), "Time the GPU spent executing the frame.\n"
                                                  "High values point to a GPU bound frame: shaders or fill rate."},
        {"Latency", fmt::format("{} {}", latency, latency == 1 ? "frame" : "frames"),
         "How many frames the GPU runs behind the CPU, typically 1 to 3.\n"
         "More latency means more input lag, but can raise throughput."},
    }});
}

struct stat_row
{
    const char* label{};
    std::string value;
    const char* tooltip{};
    /// 0 for the text color.
    ImU32 value_color{};
};

/// A muted label, and the value right aligned in the mono font so the digits line up.
void draw_row(const stat_row& row)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetTextLineHeight();
    ImGui::Dummy(ImVec2(width, height));
    if(row.tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", row.tooltip);
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddText(ImVec2(min.x + panel_section::get_row_indent(), min.y), imgui_style::get_muted_text_color_u32(), row.label);
    ImGui::PushFont(ImGui::Font::Mono);
    const ImVec2 value_size = ImGui::CalcTextSize(row.value.c_str());
    const ImU32 value_color = row.value_color != 0 ? row.value_color : ImGui::GetColorU32(ImGuiCol_Text);
    draw_list->AddText(ImVec2(min.x + width - value_size.x, min.y + (height - value_size.y) * 0.5f),
                       value_color,
                       row.value.c_str());
    ImGui::PopFont();
}

struct pair_row
{
    const char* label{};
    std::uint32_t first{};
    std::uint32_t second{};
    const char* tooltip{};
};

/// A row with two right aligned columns, under captions drawn by draw_pair_captions().
void draw_pair_row(const pair_row& row)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetTextLineHeight();
    ImGui::Dummy(ImVec2(width, height));
    if(row.tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", row.tooltip);
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddText(ImVec2(min.x + panel_section::get_row_indent(), min.y), imgui_style::get_muted_text_color_u32(), row.label);
    ImGui::PushFont(ImGui::Font::Mono);
    const std::string first = fmt::format("{}", row.first);
    const std::string second = fmt::format("{}", row.second);
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    const float first_right = min.x + width - to_pixels(PAIR_COLUMN_WIDTH);
    draw_list->AddText(ImVec2(first_right - ImGui::CalcTextSize(first.c_str()).x, min.y), color, first.c_str());
    draw_list->AddText(ImVec2(min.x + width - ImGui::CalcTextSize(second.c_str()).x, min.y), color, second.c_str());
    ImGui::PopFont();
}

void draw_pair_captions(const char* first, const char* second)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushWindowFontScale(CAPTION_SCALE);
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float first_right = min.x + width - to_pixels(PAIR_COLUMN_WIDTH);
    draw_list->AddText(ImVec2(first_right - ImGui::CalcTextSize(first).x, min.y), imgui_style::get_muted_text_color_u32(), first);
    draw_list->AddText(ImVec2(min.x + width - ImGui::CalcTextSize(second).x, min.y), imgui_style::get_muted_text_color_u32(), second);
    ImGui::PopWindowFontScale();
}

/// A small caption over a group of rows inside a section.
void draw_subheading(const char* text)
{
    ImGui::Dummy(ImVec2(0.0f, to_pixels(ROW_SPACING)));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + panel_section::get_row_indent());
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::PushWindowFontScale(CAPTION_SCALE);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(imgui_style::get_muted_text_color_u32()), "%s", text);
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
}

/// A thin bar under a row, filled to fraction.
/// A thin bar under a row, filled to fraction. marker_fraction >= 0 puts a tick on the track, for
/// a second level worth seeing beside the fill - the watermark paging evicts down to.
void draw_meter(float fraction, const ImVec4& color, float marker_fraction = -1.0f)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = to_pixels(METER_HEIGHT);
    ImGui::Dummy(ImVec2(width, height));
    const float indent = panel_section::get_row_indent();
    const ImVec2 track_min(min.x + indent, min.y);
    const float track_width = width - indent;
    const float rounding = height * 0.5f;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(track_min, track_min + ImVec2(track_width, height), METER_TRACK_COLOR, rounding);
    if(fraction > 0.0f)
    {
        const float fill_width = std::max(track_width * std::min(fraction, 1.0f), height);
        draw_list->AddRectFilled(track_min, track_min + ImVec2(fill_width, height), ImGui::GetColorU32(color), rounding);
    }
    if(marker_fraction < 0.0f)
    {
        return;
    }
    const float marker_x = ImFloor(track_min.x + track_width * std::min(marker_fraction, 1.0f)) + 0.5f;
    draw_list->AddLine(ImVec2(marker_x, track_min.y - 1.0f),
                       ImVec2(marker_x, track_min.y + height + 1.0f),
                       METER_MARKER_COLOR);
}

/// An on / off switch over the rectangle; it takes no room of its own. Returns true when clicked.
auto draw_switch(const char* id, const ImRect& bb, bool is_on, const char* tooltip) -> bool
{
    const ImGuiID item_id = ImGui::GetID(id);
    if(!ImGui::ItemAdd(bb, item_id))
    {
        return false;
    }
    bool is_hovered = false;
    bool is_held = false;
    const bool is_pressed = ImGui::ButtonBehavior(bb, item_id, &is_hovered, &is_held);
    const float radius = bb.GetHeight() * 0.5f;
    ImU32 track_color = is_hovered ? SWITCH_OFF_HOVERED_COLOR : SWITCH_OFF_COLOR;
    if(is_on)
    {
        track_color = get_accent_color(is_hovered ? 1.0f : SWITCH_ON_ALPHA);
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(bb.Min, bb.Max, track_color, radius);
    const float knob_x = is_on ? bb.Max.x - radius : bb.Min.x + radius;
    draw_list->AddCircleFilled(ImVec2(knob_x, bb.GetCenter().y), radius - to_pixels(SWITCH_KNOB_INSET), SWITCH_KNOB_COLOR);
    ImGui::SetItemTooltipEx("%s", tooltip);
    return is_pressed;
}

void draw_scene_section()
{
    if(!panel_section::draw_header("##scene", ICON_MDI_CUBE_OUTLINE, "Scene", true).is_open)
    {
        return;
    }
    const auto* stats = bgfx::getStats();
    const ImGuiIO& io = ImGui::GetIO();
    const std::uint32_t total_primitives = std::accumulate(std::begin(stats->numPrims), std::end(stats->numPrims), 0u);
    const std::uint32_t ui_primitives = std::min(static_cast<std::uint32_t>(io.MetricsRenderIndices / 3), total_primitives);
    const std::uint32_t total_calls = stats->numDraw;
    const std::uint32_t editor_calls = std::min(static_cast<std::uint32_t>(ImGui::GetDrawCalls()), total_calls);

    draw_row({"Triangles",
              format_count(total_primitives - ui_primitives),
              "Triangles rendered for the scene, without the editor UI.\nLODs and culling bring it down."});
    draw_row({"Draw Calls",
              fmt::format("{}", total_calls - editor_calls),
              "Draw commands sent to the GPU for the scene, without the editor UI.\n"
              "Fewer draw calls cost the CPU less."});
    draw_row({"Compute Calls", fmt::format("{}", stats->numCompute), "Compute shader dispatches of this frame."});
    draw_row({"Draw Calls Peak", fmt::format("{}", stats->numDrawCallsPeak), "Highest number of draw+compute calls requested in a single frame so far."});
    draw_row({"Blit Calls", fmt::format("{}", stats->numBlit), "Texture copies of this frame: resolves and render target transfers."});
    // draw_row({"Blit Calls Repacked", fmt::format("{}", stats->numBlitRepack), "Number of buffer to texture blit calls that had to be repacked,."});
    draw_row({"Render Passes",
              fmt::format("{}", gfx::render_pass::get_last_frame_max_pass_id()),
              "Passes of this frame: geometry, lighting, shadows and post processing."});
}

void draw_paging_rows()
{
    const auto evict_stats = gfx::eviction::get_stats();
    if(evict_stats.registered_count == 0)
    {
        return;
    }
    draw_subheading(ICON_MDI_SWAP_HORIZONTAL " Paging");
    ImGui::SetItemTooltipEx("%s",
                            "GPU resource eviction. Idle CPU backed resources leave GPU memory under pressure\n"
                            "and come back on their next use.");
    // The bar carries the pressure that drives paging: GPU memory against the budget, with the
    // target marked on it. The rows under it are the pool paging owns, which is a different and
    // much smaller quantity - keeping both as bars read as if one repeated the other.
    if(evict_stats.budget_bytes > 0)
    {
        const bool is_over_budget = evict_stats.budget_used_bytes > evict_stats.budget_bytes;
        const float percent = static_cast<float>(static_cast<double>(evict_stats.budget_used_bytes) /
                                                 static_cast<double>(evict_stats.budget_bytes)) *
                              PERCENT;
        draw_row({"Budget",
                  fmt::format("{} / {}{}",
                              format_bytes(static_cast<std::uint64_t>(evict_stats.budget_used_bytes)),
                              format_bytes(static_cast<std::uint64_t>(evict_stats.budget_bytes)),
                              is_over_budget ? " over" : ""),
                  "GPU memory used against the eviction budget. Past the budget, resources are\n"
                  "evicted down to the target, the mark on the bar. Red while over budget.",
                  ImGui::GetColorU32(is_over_budget ? COLOR_BAD : get_memory_color(percent))});
        const float target_fraction = static_cast<float>(static_cast<double>(evict_stats.target_bytes) /
                                                         static_cast<double>(evict_stats.budget_bytes));
        draw_meter(percent / PERCENT, get_memory_meter_color(percent), target_fraction);
    }
    draw_row({"Resident",
              fmt::format("{} ({})", format_bytes(static_cast<std::uint64_t>(evict_stats.resident_bytes)), evict_stats.resident_count),
              "Tracked resources still on the GPU, and how many: what a pass can free."});
    draw_row({"Evicted",
              fmt::format("{} ({})", format_bytes(static_cast<std::uint64_t>(evict_stats.evicted_bytes)), evict_stats.evicted_count),
              "Tracked resources paged out. They come back on their next use.",
              evict_stats.evicted_count > 0 ? ImGui::GetColorU32(COLOR_WARNING) : 0u});
    draw_row({"Lifetime",
              fmt::format("{} evicted / {} restored", evict_stats.total_evictions, evict_stats.total_restores),
              "Evictions and restores since start. An eviction count that climbs every frame means\n"
              "constant paging: raise the budget or the minimum age."});
    if(evict_stats.thrash_events > 0)
    {
        draw_row({"Thrash",
                  fmt::format("{}", evict_stats.thrash_events),
                  "Resources restored soon after their eviction: the budget is too tight.",
                  ImGui::GetColorU32(COLOR_WARNING)});
    }
}

void draw_memory_section()
{
    if(!panel_section::draw_header("##memory", ICON_MDI_MEMORY, "Memory", false).is_open)
    {
        return;
    }
    const auto* stats = bgfx::getStats();
    if(stats->gpuMemoryUsed > 0)
    {
        const std::string used = format_bytes(stats->gpuMemoryUsed);
        if(stats->gpuMemoryMax > 0)
        {
            const float percent =
                static_cast<float>(stats->gpuMemoryUsed) / static_cast<float>(stats->gpuMemoryMax) * PERCENT;
            draw_row({"GPU Memory",
                      fmt::format("{} / {}", used, format_bytes(stats->gpuMemoryMax)),
                      "GPU memory allocated against what the device has. Green below 60%,\n"
                      "yellow to 80%, red above: past that, expect stalls or out of memory errors.",
                      ImGui::GetColorU32(get_memory_color(percent))});
            draw_meter(percent / PERCENT, get_memory_meter_color(percent));
        }
        else
        {
            draw_row({"GPU Memory", used, "GPU memory allocated."});
        }
    }
    if(stats->textureMemoryUsed > 0)
    {
        draw_row({"Textures",
                  format_bytes(stats->textureMemoryUsed),
                  "GPU memory of the textures. Smaller or compressed textures bring it down."});
    }
    if(stats->rtMemoryUsed > 0)
    {
        draw_row({"Render Targets",
                  format_bytes(stats->rtMemoryUsed),
                  "GPU memory of the render targets. It scales with the resolution."});
    }
    draw_paging_rows();
}

void draw_pipeline_section(const rendering::pipeline_stats& pstats)
{
    if(!panel_section::draw_header("##pipeline", ICON_MDI_PIPE, "Pipeline", false).is_open)
    {
        return;
    }
    draw_pair_captions("Models", "Meshes");
    draw_pair_row({"Static",
                   pstats.drawn_models,
                   pstats.drawn_static_submeshes,
                   "Visible static models, and their submeshes left after per submesh culling."});
    draw_pair_row({"Skinned",
                   pstats.drawn_skinned_models,
                   pstats.drawn_skinned_submeshes,
                   "Visible skinned models, and their submeshes submitted for GPU skinning."});
    draw_pair_row({"Shadow Casters",
                   pstats.drawn_models_for_shadows + pstats.drawn_skinned_models_for_shadows,
                   pstats.drawn_submeshes_for_shadows + pstats.drawn_skinned_submeshes_for_shadows,
                   "Models and submeshes drawn into shadow maps. Cascades and cube faces draw a\n"
                   "mesh again, so this can exceed the meshes of the main pass."});
    draw_row({"Lights",
              fmt::format("{}", pstats.drawn_lights),
              "Lights evaluated for the scene. Each one adds shading cost."});
    draw_row({"Shadow Lights",
              fmt::format("{}", pstats.drawn_lights_casting_shadows),
              "Lights that cast shadows. Each one renders one or more shadow maps."});
}

void draw_particles_section(const rendering::pipeline_stats& pstats)
{
    if(!panel_section::draw_header("##particles", ICON_MDI_CREATION, "Particles", false).is_open)
    {
        return;
    }
    draw_pair_captions("Emitters", "Particles");
    draw_pair_row({"Active",
                   pstats.active_particle_emitters,
                   pstats.active_particles,
                   "Enabled emitters in the scene, and the particles alive in them."});
    draw_pair_row({"Simulated",
                   pstats.simulated_particle_emitters,
                   pstats.simulated_particles,
                   "Emitters simulated this frame, and their particles. Renderer-based culling freezes\n"
                   "the emitters no camera drew in the frame before."});
    draw_pair_row({"Drawn",
                   pstats.drawn_particle_emitters,
                   pstats.drawn_particles,
                   "Emitters this camera drew, and the particles it submitted."});
    draw_row({"Batches",
              fmt::format("{}", pstats.drawn_particles_batches),
              "Draw batches of the particles. Different textures or blend modes split them."});
}

/// Batching statistics, under a header whose switch turns the project's static mesh batching on
/// and off: the numbers above show what it saves.
void draw_batching_section(rtti::context& ctx, const batch_stats& batch)
{
    auto& pm = ctx.get_cached<project_manager>();
    settings::graphics_settings& graphics = pm.get_settings().graphics;
    const float switch_width = to_pixels(SWITCH_WIDTH);
    const panel_section::header header = panel_section::draw_header("##batching",
                                                      ICON_MDI_LAYERS_TRIPLE_OUTLINE,
                                                      "Batching",
                                                      true,
                                                      switch_width + to_pixels(ITEM_GAP));
    const ImVec2 switch_size(switch_width, to_pixels(SWITCH_HEIGHT));
    const ImVec2 switch_min(header.row.Max.x - switch_size.x, header.row.GetCenter().y - switch_size.y * 0.5f);
    if(draw_switch("##static_mesh_batching",
                   ImRect(switch_min, switch_min + switch_size),
                   graphics.static_mesh_batching,
                   "Static mesh batching: meshes that share a mesh and a material are drawn with\n"
                   "one instanced draw call. A project setting, also under Project Settings > Graphics."))
    {
        graphics.static_mesh_batching = !graphics.static_mesh_batching;
        pm.save_project_settings(ctx);
    }
    if(!header.is_open)
    {
        return;
    }
    if(!graphics.static_mesh_batching)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + panel_section::get_row_indent());
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(imgui_style::get_muted_text_color_u32()), "%s", "Off: every mesh is a draw call of its own.");
        ImGui::PopTextWrapPos();
        return;
    }
    draw_row({"Batches",
              fmt::format("{}", batch.total_batches),
              "Instanced draw calls that the batched meshes were drawn with."});
    draw_row({"Instances", fmt::format("{}", batch.total_instances), "Meshes drawn through the batches."});
    draw_row({"Per Batch",
              fmt::format("{:.1f}", static_cast<double>(batch.average_batch_size)),
              "Meshes per batch on average. Many unique materials keep it low."});
    draw_row({"Draw Calls Saved",
              fmt::format("{}", batch.draw_calls_saved),
              "Draw calls the batches replace: instances minus batches."});
}

void draw_content(rtti::context& ctx, const rendering::pipeline_stats& pstats, state& overlay_state)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(to_pixels(ITEM_GAP), to_pixels(ROW_SPACING)));
    const header_request request = draw_header();
    if(request.is_close_pressed)
    {
        overlay_state.is_visible = false;
    }
    if(request.is_profiler_pressed)
    {
        overlay_state.open_profiler_requested = true;
    }
    draw_frame_rate();
    draw_frame_graph(overlay_state);
    draw_timing_tiles();
    draw_scene_section();
    draw_memory_section();
    draw_pipeline_section(pstats);
    draw_particles_section(pstats);
    draw_batching_section(ctx, pstats.batching_stats);
    ImGui::PopStyleVar();
}

struct fps_readout
{
    std::string text;
    /// The text changes every frame; this template sizes it, or an anchored bar would jitter.
    std::string width_text;
};

auto make_fps_readout() -> fps_readout
{
    const float fps = ImGui::GetIO().Framerate;
    const char* widest_value = fps < FPS_FOUR_DIGITS ? "000 FPS" : "0000 FPS";
    fps_readout readout{};
    readout.text = panel_toolbar::make_text(ICON_MDI_CHART_LINE, fmt::format("{:.0f} FPS", fps).c_str(), false);
    readout.width_text = panel_toolbar::make_text(ICON_MDI_CHART_LINE, widest_value, false);
    return readout;
}

} // namespace

void draw(rtti::context& ctx, const rendering::pipeline_stats& pstats, state& overlay_state, const char* id, float top_offset)
{
    if(!overlay_state.is_visible)
    {
        return;
    }
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window == nullptr || window->SkipItems)
    {
        return;
    }
    record_frame_time(overlay_state);

    const ImRect content_rect = window->ContentRegionRect;
    const float margin = panel_toolbar::get_bar_margin();
    const ImVec2 top_right(content_rect.Max.x - margin, content_rect.Min.y + top_offset + margin);
    const float max_height = std::max(content_rect.Max.y - margin - top_right.y, to_pixels(OVERLAY_MIN_HEIGHT));
    const std::string child_id = fmt::format("##viewport_stats_{}", id);
    if(panel_toolbar::begin_overlay(child_id.c_str(), top_right, to_pixels(OVERLAY_WIDTH), max_height))
    {
        draw_content(ctx, pstats, overlay_state);
    }
    panel_toolbar::end_overlay();
}

void draw_toolbar_toggle(state& overlay_state)
{
    const fps_readout readout = make_fps_readout();
    const char* tooltip = overlay_state.is_visible ? "Hide Statistics" : "Show Statistics";
    if(panel_toolbar::toggle("##stats", readout.text.c_str(), overlay_state.is_visible, tooltip, readout.width_text.c_str()))
    {
        overlay_state.is_visible = !overlay_state.is_visible;
    }
}

void draw_toolbar_readout(const panel_toolbar::bar_placement& placement)
{
    const fps_readout readout = make_fps_readout();
    panel_toolbar::draw_readout(placement, readout.text.c_str(), readout.width_text.c_str());
}
} // namespace unravel::viewport_stats_overlay
