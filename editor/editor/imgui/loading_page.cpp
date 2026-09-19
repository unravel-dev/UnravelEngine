#include "loading_page.h"
#include "integration/imgui_style.h"
#include "screen_card.h"

#include <base/platform/process_memory.hpp>
#include <editor/format/format_bytes.h>
#include <graphics/graphics.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <logging/logging.h>
#include <version/version.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace unravel::loading_page
{
namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size, see screen_card.
constexpr float LOADING_PAGE_WIDTH = 34.0f;
constexpr float LOADING_PAGE_TITLE_SCALE = 1.3f;
constexpr float LOADING_PAGE_TITLE_GAP = 1.6f;
constexpr float LOADING_PAGE_PROGRESS_GAP = 0.65f;
constexpr float LOADING_PAGE_PROGRESS_HEIGHT = 0.4f;
constexpr float LOADING_PAGE_SECTION_GAP = 1.3f;
constexpr float LOADING_PAGE_SEPARATOR_HEIGHT = 1.0f;
constexpr float LOADING_PAGE_METER_GAP = 0.9f;
constexpr float LOADING_PAGE_METER_BAR_GAP = 0.4f;
constexpr float LOADING_PAGE_METER_BAR_HEIGHT = 0.25f;
constexpr float LOADING_PAGE_TEXT_GAP = 1.0f;
constexpr float LOADING_PAGE_SPINNER_GAP = 0.55f;
constexpr float LOADING_PAGE_SPINNER_THICKNESS = 0.14f;
constexpr float LOADING_PAGE_SPINNER_ARC = IM_PI * 1.4f;
constexpr float LOADING_PAGE_SPINNER_SPEED = 6.0f;
// Where the baseline sits in the height of a line: texts of two sizes on one row share it.
constexpr float LOADING_PAGE_BASELINE_RATIO = 0.8f;
// The narrowest a share of a bar gets, in pixels: a process that holds 1% of the memory still shows.
constexpr float LOADING_PAGE_MIN_SHARE_WIDTH = 2.0f;
constexpr float LOADING_PAGE_MUTED_ALPHA = 0.5f;
constexpr float LOADING_PAGE_VALUE_ALPHA = 0.72f;
constexpr ImU32 LOADING_PAGE_TRACK_COLOR = IM_COL32(255, 255, 255, 16);
constexpr ImU32 LOADING_PAGE_SEPARATOR_COLOR = IM_COL32(255, 255, 255, 18);
constexpr ImU32 LOADING_PAGE_OTHER_RAM_COLOR = IM_COL32(145, 118, 72, 230);
constexpr ImU32 LOADING_PAGE_PROCESS_RAM_COLOR = IM_COL32(88, 168, 196, 255);
constexpr size_t LOADING_PAGE_METER_COUNT = 2;

struct bar_share
{
    float fraction{};
    ImU32 color{};
};

/// One memory readout: a label, its value, and a bar of up to two shares stacked from the left.
struct memory_meter
{
    bool is_shown{};
    const char* label{};
    std::string value_text{};
    std::string tooltip{};
    std::array<bar_share, 2> shares{};
};

/// Where the next row goes. The page is laid out by hand: the card needs its height before it
/// is drawn, and no spacing of a theme gets into it.
struct page_cursor
{
    /// Null measures the page without drawing it.
    ImDrawList* draw_list{};
    float min_x{};
    float max_x{};
    float y{};
};

auto get_text_color(float alpha) -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Text, alpha);
}

auto get_accent_fill_color() -> ImU32
{
    ImVec4 accent = imgui_style::get_accent_color();
    accent.w = 1.0f;
    return ImGui::GetColorU32(accent);
}

auto calc_fraction(int64_t part, int64_t whole) -> float
{
    if(whole <= 0)
    {
        return 0.0f;
    }
    return std::clamp(static_cast<float>(part) / static_cast<float>(whole), 0.0f, 1.0f);
}

auto read_gpu_meter() -> memory_meter
{
    memory_meter meter{};
    const auto* stats = gfx::get_stats();
    if(stats == nullptr || stats->gpuMemoryUsed <= 0)
    {
        return meter;
    }
    meter.is_shown = true;
    meter.label = "GPU";
    meter.tooltip = "Video memory allocated by the renderer, of the budget of the device.";
    meter.value_text = format_bytes(stats->gpuMemoryUsed);
    if(stats->gpuMemoryMax > 0)
    {
        const float used = calc_fraction(stats->gpuMemoryUsed, stats->gpuMemoryMax);
        meter.value_text = fmt::format("{} / {} ({:.0f}%)",
                                       format_bytes(stats->gpuMemoryUsed),
                                       format_bytes(stats->gpuMemoryMax),
                                       static_cast<double>(used * 100.0f));
        meter.shares[0] = {used, get_accent_fill_color()};
    }
    return meter;
}

auto read_ram_meter() -> memory_meter
{
    memory_meter meter{};
    const int64_t process_bytes = platform::get_process_resident_set_bytes();
    const int64_t total_bytes = platform::get_system_physical_memory_bytes();
    const int64_t used_bytes = platform::get_system_used_physical_memory_bytes();
    if(process_bytes <= 0 && used_bytes <= 0)
    {
        return meter;
    }
    meter.is_shown = true;
    meter.label = "RAM";
    if(total_bytes <= 0 || used_bytes <= 0)
    {
        meter.value_text = format_bytes(process_bytes);
        meter.tooltip = "Memory this process keeps resident.";
        return meter;
    }
    const int64_t other_bytes = std::max<int64_t>(0, used_bytes - process_bytes);
    meter.value_text = fmt::format("{} / {}  ({} this process)",
                                   format_bytes(used_bytes),
                                   format_bytes(total_bytes),
                                   format_bytes(process_bytes));
    meter.tooltip = fmt::format("The bar is the memory of the system.\n"
                                "Amber: used by other processes ({}).\n"
                                "Cyan: used by this process ({}).",
                                format_bytes(other_bytes),
                                format_bytes(process_bytes));
    meter.shares[0] = {calc_fraction(other_bytes, total_bytes), LOADING_PAGE_OTHER_RAM_COLOR};
    meter.shares[1] = {calc_fraction(process_bytes, total_bytes), LOADING_PAGE_PROCESS_RAM_COLOR};
    return meter;
}

void add_gap(page_cursor& cursor, float font_units)
{
    cursor.y += to_pixels(font_units);
}

void draw_text_right(const page_cursor& cursor, float y, ImU32 color, const char* text)
{
    const float width = ImGui::CalcTextSize(text).x;
    cursor.draw_list->AddText(ImVec2(ImFloor(cursor.max_x - width), y), color, text);
}

/// Text from min_x that ends in an ellipsis when it would run past max_x.
void draw_text_clipped(const page_cursor& cursor, float min_x, float max_x, ImU32 color, const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const ImVec2 pos_max(max_x, cursor.y + ImGui::GetFontSize());
    ImGui::RenderTextEllipsis(cursor.draw_list, ImVec2(min_x, cursor.y), pos_max, max_x, text, nullptr, nullptr);
    ImGui::PopStyleColor();
}

/// A track over the width of the page with the shares stacked on it from the left.
void draw_bar(const page_cursor& cursor, float height, const std::array<bar_share, 2>& shares)
{
    const ImVec2 track_min(cursor.min_x, cursor.y);
    const ImVec2 track_max(cursor.max_x, cursor.y + height);
    const float rounding = height * 0.5f;
    cursor.draw_list->AddRectFilled(track_min, track_max, LOADING_PAGE_TRACK_COLOR, rounding);
    const auto is_visible = [](const bar_share& share) -> bool { return share.fraction > 0.0f; };
    float share_min_x = track_min.x;
    for(size_t index = 0; index < shares.size(); ++index)
    {
        const bar_share& share = shares[index];
        if(!is_visible(share))
        {
            continue;
        }
        const float width = ImMax((track_max.x - track_min.x) * share.fraction, LOADING_PAGE_MIN_SHARE_WIDTH);
        const float share_max_x = ImMin(share_min_x + width, track_max.x);
        // The ends of the fill are round, the seam between two shares is not.
        const bool is_last = std::none_of(shares.begin() + index + 1, shares.end(), is_visible);
        const ImDrawFlags left_corners = share_min_x > track_min.x ? ImDrawFlags_None : ImDrawFlags_RoundCornersLeft;
        const ImDrawFlags right_corners = is_last ? ImDrawFlags_RoundCornersRight : ImDrawFlags_None;
        const ImDrawFlags corners = left_corners | right_corners;
        cursor.draw_list->AddRectFilled(ImVec2(share_min_x, track_min.y),
                                        ImVec2(share_max_x, track_max.y),
                                        share.color,
                                        corners == ImDrawFlags_None ? 0.0f : rounding,
                                        corners);
        share_min_x = share_max_x;
    }
}

/// The name of the engine, the version it runs at the right end of its baseline.
void add_header_row(page_cursor& cursor)
{
    const float line_height = ImGui::GetFontSize();
    ImGui::PushFont(ImGui::Font::Black);
    ImGui::PushWindowFontScale(LOADING_PAGE_TITLE_SCALE);
    const float title_height = ImGui::GetFontSize();
    if(cursor.draw_list != nullptr)
    {
        cursor.draw_list->AddText(ImVec2(cursor.min_x, cursor.y), get_text_color(1.0f), "Unravel Engine");
    }
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    if(cursor.draw_list != nullptr)
    {
        const float version_y = ImFloor(cursor.y + (title_height - line_height) * LOADING_PAGE_BASELINE_RATIO);
        draw_text_right(cursor, version_y, get_text_color(LOADING_PAGE_MUTED_ALPHA), version::get_full().c_str());
    }
    cursor.y += title_height;
}

void draw_spinner(ImDrawList* draw_list, const ImVec2& center, float radius)
{
    const float thickness = ImMax(to_pixels(LOADING_PAGE_SPINNER_THICKNESS), 1.0f);
    const float start_angle = static_cast<float>(ImGui::GetTime()) * LOADING_PAGE_SPINNER_SPEED;
    draw_list->AddCircle(center, radius, LOADING_PAGE_TRACK_COLOR, 0, thickness);
    draw_list->PathArcTo(center, radius, start_angle, start_angle + LOADING_PAGE_SPINNER_ARC);
    draw_list->PathStroke(get_accent_fill_color(), ImDrawFlags_None, thickness);
}

/// A spinner and the stage, the share of it that is done at the right end.
void add_stage_row(page_cursor& cursor, const progress_info& info, float fraction)
{
    const float line_height = ImGui::GetFontSize();
    if(cursor.draw_list != nullptr)
    {
        const float radius = line_height * 0.5f - to_pixels(LOADING_PAGE_SPINNER_THICKNESS);
        draw_spinner(cursor.draw_list, ImVec2(cursor.min_x + line_height * 0.5f, cursor.y + line_height * 0.5f), radius);
        const std::string percent_text = info.total > 0 ? fmt::format("{:.0f}%", static_cast<double>(fraction * 100.0f)) : "";
        draw_text_right(cursor, cursor.y, get_text_color(LOADING_PAGE_VALUE_ALPHA), percent_text.c_str());
        const float text_min_x = cursor.min_x + line_height + to_pixels(LOADING_PAGE_SPINNER_GAP);
        const float text_max_x = cursor.max_x - ImGui::CalcTextSize(percent_text.c_str()).x - to_pixels(LOADING_PAGE_TEXT_GAP);
        ImGui::PushFont(ImGui::Font::SemiBold);
        draw_text_clipped(cursor, text_min_x, text_max_x, get_text_color(1.0f), info.stage.c_str());
        ImGui::PopFont();
    }
    cursor.y += line_height;
}

void add_progress_bar(page_cursor& cursor, float fraction)
{
    const float height = to_pixels(LOADING_PAGE_PROGRESS_HEIGHT);
    if(cursor.draw_list != nullptr)
    {
        draw_bar(cursor, height, {bar_share{fraction, get_accent_fill_color()}, bar_share{}});
    }
    cursor.y += height;
}

/// The item in work on one line, however long its name, and the count at the right end.
void add_job_row(page_cursor& cursor, const progress_info& info)
{
    if(cursor.draw_list != nullptr)
    {
        const std::string count_text = info.total > 0 ? fmt::format("{} / {}", info.completed, info.total) : "";
        draw_text_right(cursor, cursor.y, get_text_color(LOADING_PAGE_VALUE_ALPHA), count_text.c_str());
        const float text_max_x = cursor.max_x - ImGui::CalcTextSize(count_text.c_str()).x - to_pixels(LOADING_PAGE_TEXT_GAP);
        draw_text_clipped(cursor, cursor.min_x, text_max_x, get_text_color(LOADING_PAGE_MUTED_ALPHA), info.current_job.c_str());
    }
    cursor.y += ImGui::GetFontSize();
}

void add_separator(page_cursor& cursor)
{
    add_gap(cursor, LOADING_PAGE_SECTION_GAP);
    if(cursor.draw_list != nullptr)
    {
        const ImVec2 line_max(cursor.max_x, cursor.y + LOADING_PAGE_SEPARATOR_HEIGHT);
        cursor.draw_list->AddRectFilled(ImVec2(cursor.min_x, cursor.y), line_max, LOADING_PAGE_SEPARATOR_COLOR);
    }
    cursor.y += LOADING_PAGE_SEPARATOR_HEIGHT;
    add_gap(cursor, LOADING_PAGE_SECTION_GAP);
}

void add_meter(page_cursor& cursor, const memory_meter& meter)
{
    const float row_min_y = cursor.y;
    const float bar_height = to_pixels(LOADING_PAGE_METER_BAR_HEIGHT);
    if(cursor.draw_list != nullptr)
    {
        cursor.draw_list->AddText(ImVec2(cursor.min_x, cursor.y), get_text_color(LOADING_PAGE_MUTED_ALPHA), meter.label);
        draw_text_right(cursor, cursor.y, get_text_color(LOADING_PAGE_VALUE_ALPHA), meter.value_text.c_str());
    }
    cursor.y += ImGui::GetFontSize();
    add_gap(cursor, LOADING_PAGE_METER_BAR_GAP);
    if(cursor.draw_list != nullptr)
    {
        draw_bar(cursor, bar_height, meter.shares);
    }
    cursor.y += bar_height;
    const bool is_hovered = cursor.draw_list != nullptr &&
                            ImGui::IsMouseHoveringRect(ImVec2(cursor.min_x, row_min_y), ImVec2(cursor.max_x, cursor.y));
    if(is_hovered)
    {
        ImGui::SetTooltip("%s", meter.tooltip.c_str());
    }
}

/// The one place that knows the rows of the page and their order: it measures with a cursor
/// that has no draw list, and draws with one that has.
void layout_page(page_cursor& cursor,
                 const progress_info& info,
                 const std::array<memory_meter, LOADING_PAGE_METER_COUNT>& meters)
{
    const float fraction = calc_fraction(static_cast<int64_t>(info.completed), static_cast<int64_t>(info.total));
    add_header_row(cursor);
    add_gap(cursor, LOADING_PAGE_TITLE_GAP);
    add_stage_row(cursor, info, fraction);
    add_gap(cursor, LOADING_PAGE_PROGRESS_GAP);
    add_progress_bar(cursor, fraction);
    add_gap(cursor, LOADING_PAGE_PROGRESS_GAP);
    add_job_row(cursor, info);
    bool has_meter_above = false;
    for(const memory_meter& meter : meters)
    {
        if(!meter.is_shown)
        {
            continue;
        }
        if(has_meter_above)
        {
            add_gap(cursor, LOADING_PAGE_METER_GAP);
        }
        else
        {
            add_separator(cursor);
        }
        add_meter(cursor, meter);
        has_meter_above = true;
    }
}

auto measure_page(const progress_info& info, const std::array<memory_meter, LOADING_PAGE_METER_COUNT>& meters) -> float
{
    page_cursor cursor{};
    layout_page(cursor, info, meters);
    return cursor.y + 2.0f * screen_card::get_padding().y;
}
} // namespace

void draw(const progress_info& info)
{
    const std::array<memory_meter, LOADING_PAGE_METER_COUNT> meters{read_gpu_meter(), read_ram_meter()};
    // The meters come up one by one while the renderer starts. Centered by the height with all of
    // them, the card grows downwards and its rows stay where they are.
    std::array<memory_meter, LOADING_PAGE_METER_COUNT> all_meters = meters;
    std::for_each(all_meters.begin(), all_meters.end(), [](memory_meter& meter) -> void { meter.is_shown = true; });
    screen_card::card_layout layout{};
    layout.size = screen_card::fit_to_viewport(ImVec2(to_pixels(LOADING_PAGE_WIDTH), measure_page(info, meters)));
    layout.centered_height = measure_page(info, all_meters);
    if(screen_card::begin("##loading_page", layout))
    {
        page_cursor cursor{};
        cursor.draw_list = ImGui::GetWindowDrawList();
        cursor.min_x = ImGui::GetCursorScreenPos().x;
        cursor.max_x = cursor.min_x + ImGui::GetContentRegionAvail().x;
        cursor.y = ImGui::GetCursorScreenPos().y;
        layout_page(cursor, info, meters);
    }
    screen_card::end();
}
} // namespace unravel::loading_page
