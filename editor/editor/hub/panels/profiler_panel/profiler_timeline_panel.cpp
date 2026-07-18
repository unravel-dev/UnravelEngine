#include "profiler_timeline_panel.h"

#include "gpu_frame_stats_widgets.h"
#include "profiler_gpu_resources_section.h"
#include "profiler_eviction_section.h"
#include "../panel.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <editor/format/format_bytes.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <base/platform/process_memory.hpp>
#include <engine/settings/settings.h>
#include <graphics/graphics.h>
#include <dotnetpp/dotnetpp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace unravel
{

namespace
{

constexpr float row_height = 20.0f;
constexpr float lane_header_width = 120.0f;
/// Total vertical space reserved for the frame histogram row (background + bars).
constexpr float frame_bar_height = 92.0f;
/// Stacked rows for managed heap and GPU memory (aligned to frame index axis).
constexpr float memory_hist_row_height = 72.0f;
constexpr float memory_hist_top_pad = 10.0f;
constexpr float megabyte_divisor = 1024.0f * 1024.0f;
constexpr ImU32 cpu_heap_hist_color = IM_COL32(200, 140, 70, 220);
constexpr ImU32 gpu_mem_hist_color = IM_COL32(70, 130, 210, 220);
constexpr ImU32 process_rss_hist_color = IM_COL32(140, 200, 120, 220);

enum class memory_histogram_metric : uint8_t
{
    managed_heap_mb,
    gpu_memory_mb,
    process_rss_mb
};

auto memory_mb_from_snapshot(const frame_snapshot* snap, memory_histogram_metric metric) -> float
{
    if(snap == nullptr)
    {
        return 0.0f;
    }
    switch(metric)
    {
    case memory_histogram_metric::managed_heap_mb:
        return static_cast<float>(snap->cpu_heap_used_bytes) / megabyte_divisor;
    case memory_histogram_metric::gpu_memory_mb:
        return static_cast<float>(snap->gpu_memory_used_bytes) / megabyte_divisor;
    case memory_histogram_metric::process_rss_mb:
        return static_cast<float>(snap->process_resident_bytes) / megabyte_divisor;
    }
    return 0.0f;
}

auto memory_hist_inner_height() -> float
{
    return memory_hist_row_height - memory_hist_top_pad;
}

/// Empty band at the top of the histogram so the tallest bar does not touch the container edge.
constexpr float histogram_plot_top_pad = 14.0f;
/// Vertical span used to map @c scale_max ms to bar height (below the top pad).
constexpr float histogram_inner_height = frame_bar_height - histogram_plot_top_pad;
constexpr float ruler_height = 22.0f;
constexpr float min_visible_width_px = 1.0f;

constexpr float target_60fps_ms = 16.667f;

constexpr double min_view_duration_ns = 100'000.0;
constexpr double max_view_duration_ns = 2'000'000'000.0;

/// Hard cap so a single lane cannot dominate the scroll range (bad profiler depth, etc.).
constexpr uint16_t max_timeline_lane_depth = 48;
constexpr float thread_lane_spacing = 4.0f;
constexpr float timeline_wheel_scroll_px = 40.0f;

auto lane_height_for_depth(uint16_t max_depth) -> float
{
    const uint16_t d = std::min(max_depth, max_timeline_lane_depth);
    return (static_cast<float>(d) + 1.0f) * row_height + 2.0f;
}

/// Max nesting depth for lane height from timestamps, not stored @c ev.depth.
/// Imbalanced profile_begin/end (e.g. GPU/gfx callbacks) can inflate @c buf.depth while intervals
/// still nest only a few levels; max(ev.depth) then reserves dozens of empty rows.
auto max_nesting_depth_for_thread_in_view(const std::vector<const frame_snapshot*>& frames,
                                          uint16_t thread_index,
                                          double view_start_ns,
                                          double view_end_ns) -> uint16_t
{
    struct span
    {
        int64_t s{};
        int64_t e{};
    };

    std::vector<span> spans;
    spans.reserve(512);

    for(const frame_snapshot* frame : frames)
    {
        if(!frame)
        {
            continue;
        }
        for(const auto& ts : frame->threads)
        {
            if(ts.thread_index != thread_index)
            {
                continue;
            }
            for(const auto& ev : ts.events)
            {
                if(ev.end_ns <= ev.start_ns)
                {
                    continue;
                }
                const double es = static_cast<double>(ev.start_ns);
                const double ee = static_cast<double>(ev.end_ns);
                if(ee < view_start_ns || es > view_end_ns)
                {
                    continue;
                }
                spans.push_back({ev.start_ns, ev.end_ns});
            }
        }
    }

    if(spans.empty())
    {
        return 0;
    }

    std::sort(spans.begin(), spans.end(), [](const span& a, const span& b) -> bool
        {
            if(a.s != b.s)
            {
                return a.s < b.s;
            }
            return a.e > b.e;
        });

    std::vector<int64_t> active_ends;
    active_ends.reserve(spans.size());
    uint16_t max_d = 0;

    for(const span& sp : spans)
    {
        while(!active_ends.empty() && active_ends.back() <= sp.s)
        {
            active_ends.pop_back();
        }
        const auto d = static_cast<uint16_t>(active_ends.size());
        if(d > max_d)
        {
            max_d = d;
        }
        active_ends.push_back(sp.e);
    }

    return max_d;
}

auto color_from_hash(uint32_t hash) -> ImU32
{
    float h = static_cast<float>(hash % 360) / 360.0f;
    float s = 0.5f + static_cast<float>((hash >> 12) % 30) / 100.0f;
    float v = 0.6f + static_cast<float>((hash >> 20) % 30) / 100.0f;

    float r{}, g{}, b{};
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return IM_COL32(
        static_cast<int>(r * 255),
        static_cast<int>(g * 255),
        static_cast<int>(b * 255),
        220);
}

auto dim_color(ImU32 col) -> ImU32
{
    int r = static_cast<int>((col >> IM_COL32_R_SHIFT) & 0xFF) * 2 / 5;
    int g = static_cast<int>((col >> IM_COL32_G_SHIFT) & 0xFF) * 2 / 5;
    int b = static_cast<int>((col >> IM_COL32_B_SHIFT) & 0xFF) * 2 / 5;
    return IM_COL32(r, g, b, 140);
}

auto format_time(float ms) -> std::string
{
    if(ms < 0.001f)
    {
        return fmt::format("{:.0f} ns", ms * 1'000'000.0f);
    }
    if(ms < 1.0f)
    {
        return fmt::format("{:.1f} us", ms * 1'000.0f);
    }
    return fmt::format("{:.2f} ms", ms);
}

using lane_context = profiler_timeline_panel::lane_context;

struct event_lane_geom
{
    bool ok{};
    float x0{};
    float y0{};
    float x1{};
    float y1{};
};

auto compute_event_geom(const lane_context& lc, const profile_event& ev) -> event_lane_geom
{
    event_lane_geom g{};
    const double ev_start = static_cast<double>(ev.start_ns);
    const double ev_end = static_cast<double>(ev.end_ns);

    if(ev_end < lc.view_start_ns || ev_start > lc.view_end_ns)
    {
        return g;
    }

    g.x0 = lc.canvas_pos.x + static_cast<float>((ev_start - lc.view_start_ns) / lc.ns_per_pixel);
    g.x1 = lc.canvas_pos.x + static_cast<float>((ev_end - lc.view_start_ns) / lc.ns_per_pixel);
    g.x0 = std::max(g.x0, lc.canvas_pos.x);
    g.x1 = std::min(g.x1, lc.canvas_pos.x + lc.lane_content_width);

    if((g.x1 - g.x0) < min_visible_width_px)
    {
        return g;
    }

    g.y0 = lc.canvas_pos.y + static_cast<float>(ev.depth) * row_height;
    g.y1 = g.y0 + row_height - 1.0f;
    g.ok = true;
    return g;
}

auto hist_index_of(performance_profiler* profiler, const frame_snapshot* snap) -> uint32_t
{
    if(!profiler || !snap)
    {
        return UINT32_MAX;
    }
    const uint32_t n = profiler->get_frame_count();
    for(uint32_t i = 0; i < n; ++i)
    {
        if(profiler->get_frame_snapshot(i) == snap)
        {
            return i;
        }
    }
    return UINT32_MAX;
}

constexpr ImU32 wait_color_selected = IM_COL32(50, 50, 55, 200);
constexpr ImU32 wait_color_dimmed = IM_COL32(30, 30, 35, 140);

auto compute_cpu_ratio(const profile_event& ev) -> float
{
    int64_t wall = ev.end_ns - ev.start_ns;
    int64_t cpu = ev.cpu_end_ns - ev.cpu_start_ns;
    if(wall <= 0 || cpu <= 0)
    {
        return 1.0f;
    }
    return std::clamp(static_cast<float>(cpu) / static_cast<float>(wall), 0.0f, 1.0f);
}

auto histogram_bar_color(float ms) -> ImU32
{
    if(ms > 33.333f)
    {
        return IM_COL32(200, 50, 50, 200);
    }
    if(ms > 16.667f)
    {
        return IM_COL32(200, 160, 50, 200);
    }
    return IM_COL32(60, 150, 60, 200);
}

/// Draw one histogram column (optionally with CPU/wait split and outline).
void draw_histogram_column(ImDrawList* draw_list,
                           float x0,
                           float x1,
                           float bottom_y,
                           float ms,
                           float cpu_ratio,
                           float scale_max,
                           bool draw_cpu_split,
                           bool draw_outline)
{
    if(ms <= 0.0f || scale_max <= 0.0f)
    {
        return;
    }
    const float h_frac = std::clamp(ms / scale_max, 0.01f, 1.0f);
    const float bar_h = histogram_inner_height * h_frac;
    const float y_top = bottom_y - bar_h;
    const ImU32 cpu_col = histogram_bar_color(ms);
    // Full bar height = frame wall time. Base fill must contrast with the chart background
    // (30,30,30) so wait time is visible; the old wait color matched the bg and looked empty.
    constexpr ImU32 hist_wait_fill = IM_COL32(72, 72, 88, 255);
    if(draw_cpu_split)
    {
        draw_list->AddRectFilled(ImVec2(x0, y_top), ImVec2(x1, bottom_y), hist_wait_fill);
        const float cpu_h = bar_h * std::clamp(cpu_ratio, 0.0f, 1.0f);
        if(cpu_h >= min_visible_width_px)
        {
            draw_list->AddRectFilled(ImVec2(x0, bottom_y - cpu_h), ImVec2(x1, bottom_y), cpu_col);
        }
    }
    else
    {
        draw_list->AddRectFilled(ImVec2(x0, y_top), ImVec2(x1, bottom_y), cpu_col);
    }
    if(draw_outline && (x1 - x0) >= 2.0f)
    {
        draw_list->AddRect(ImVec2(x0 + 0.5f, y_top + 0.5f), ImVec2(x1 - 0.5f, bottom_y - 0.5f),
                           IM_COL32(140, 140, 160, 140), 0.0f, 0, 1.0f);
    }
}

void render_histogram_bars(ImDrawList* draw_list,
                           performance_profiler* profiler,
                           int32_t first_frame,
                           int32_t last_frame,
                           ImVec2 canvas_pos,
                           float bottom_y,
                           float bar_width,
                           float hist_start,
                           float entry_w,
                           float scale_max)
{
    draw_list->PushClipRect(canvas_pos,
                            ImVec2(canvas_pos.x + bar_width, canvas_pos.y + frame_bar_height), true);

    // When many frames share a pixel, max-pool into one column so spikes stay visible
    // without emitting thousands of overlapping ImGui primitives.
    constexpr float min_column_px = 1.0f;
    const bool use_pixel_buckets = entry_w < min_column_px;
    // Always show busy vs wait (matches Frame Loop). Skip outlines when columns are thin.
    constexpr bool draw_cpu_split = true;
    const bool draw_outline = entry_w >= 4.0f;

    if(use_pixel_buckets && entry_w > 0.0f)
    {
        const int32_t col_count = std::max(1, static_cast<int32_t>(std::floor(bar_width)));
        for(int32_t col = 0; col < col_count; ++col)
        {
            const float i0f = hist_start + static_cast<float>(col) / entry_w;
            const float i1f = hist_start + static_cast<float>(col + 1) / entry_w;
            int32_t i0 = std::max(first_frame, static_cast<int32_t>(std::floor(i0f)));
            int32_t i1 = std::min(last_frame, static_cast<int32_t>(std::ceil(i1f)) - 1);
            if(i1 < i0)
            {
                continue;
            }
            float peak_ms = 0.0f;
            float peak_cpu = 1.0f;
            for(int32_t i = i0; i <= i1; ++i)
            {
                const auto* snap = profiler->get_frame_snapshot(static_cast<uint32_t>(i));
                if(snap == nullptr || snap->frame_wall_ms <= 0.0f)
                {
                    continue;
                }
                if(snap->frame_wall_ms > peak_ms)
                {
                    peak_ms = snap->frame_wall_ms;
                    peak_cpu = snap->frame_cpu_ratio;
                }
            }
            const float x0 = canvas_pos.x + static_cast<float>(col);
            const float x1 = canvas_pos.x + static_cast<float>(col + 1);
            draw_histogram_column(draw_list, x0, x1, bottom_y, peak_ms, peak_cpu, scale_max, draw_cpu_split, false);
        }
    }
    else
    {
        for(int32_t i = first_frame; i <= last_frame; ++i)
        {
            const auto* snap = profiler->get_frame_snapshot(static_cast<uint32_t>(i));
            if(snap == nullptr || snap->frame_wall_ms <= 0.0f)
            {
                continue;
            }
            const float x0 = canvas_pos.x + (static_cast<float>(i) - hist_start) * entry_w;
            const float x1 = canvas_pos.x + (static_cast<float>(i + 1) - hist_start) * entry_w;
            draw_histogram_column(draw_list,
                                  x0,
                                  x1,
                                  bottom_y,
                                  snap->frame_wall_ms,
                                  snap->frame_cpu_ratio,
                                  scale_max,
                                  draw_cpu_split,
                                  draw_outline);
        }
    }

    draw_list->PopClipRect();
}

void render_histogram_guides(ImDrawList* draw_list,
                              ImVec2 canvas_pos,
                              float bar_width,
                              float bottom_y,
                              float scale_max)
{
    constexpr float target_30fps_ms = 33.333f;

    float line_60_y = bottom_y - histogram_inner_height * (target_60fps_ms / scale_max);
    draw_list->AddLine(ImVec2(canvas_pos.x, line_60_y),
                       ImVec2(canvas_pos.x + bar_width, line_60_y),
                       IM_COL32(0, 200, 0, 100));
    draw_list->AddText(ImVec2(canvas_pos.x + 2.0f, line_60_y - 13.0f),
                       IM_COL32(0, 200, 0, 180), "16ms (60 FPS)");

    if(target_30fps_ms < scale_max)
    {
        float line_30_y = bottom_y - histogram_inner_height * (target_30fps_ms / scale_max);
        draw_list->AddLine(ImVec2(canvas_pos.x, line_30_y),
                           ImVec2(canvas_pos.x + bar_width, line_30_y),
                           IM_COL32(200, 100, 0, 100));
        draw_list->AddText(ImVec2(canvas_pos.x + 2.0f, line_30_y - 13.0f),
                           IM_COL32(200, 100, 0, 180), "33ms (30 FPS)");
    }
}

/// Horizontal scale guides for a memory row (same idea as @ref render_histogram_guides for frame ms).
void render_memory_histogram_guides(ImDrawList* draw_list,
                                    ImVec2 row_top_left,
                                    float bar_width,
                                    float scale_max_mb)
{
    if(scale_max_mb <= 0.001f)
    {
        return;
    }
    const float inner_h = memory_hist_inner_height();
    const float bottom_y = row_top_left.y + memory_hist_row_height;
    constexpr ImU32 line_col = IM_COL32(130, 130, 155, 95);
    constexpr ImU32 text_col = IM_COL32(200, 200, 220, 175);

    auto bytes_for_mb_frac = [](float mb, float frac) -> uint64_t
    {
        const double b = static_cast<double>(mb) * static_cast<double>(megabyte_divisor) * static_cast<double>(frac);
        if(b <= 0.0)
        {
            return 0u;
        }
        return static_cast<uint64_t>(b);
    };

    // Mid-scale reference (50% of current vertical max).
    {
        constexpr float frac = 0.5f;
        const float y = bottom_y - inner_h * frac;
        draw_list->AddLine(ImVec2(row_top_left.x, y), ImVec2(row_top_left.x + bar_width, y), line_col);
        const auto pretty = format_bytes(bytes_for_mb_frac(scale_max_mb, frac), 0);
        draw_list->AddText(ImVec2(row_top_left.x + 2.0f, y - 13.0f), text_col, pretty.c_str());
    }

    // Top of plot = max of scale (bars map to this row height).
    {
        const float y = bottom_y - inner_h;
        draw_list->AddLine(ImVec2(row_top_left.x, y), ImVec2(row_top_left.x + bar_width, y), line_col);
        const auto pretty = format_bytes(bytes_for_mb_frac(scale_max_mb, 1.0f), 0);
        const std::string label = fmt::format("max {}", pretty);
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        draw_list->AddText(ImVec2(row_top_left.x + bar_width - ts.x - 4.0f, y - 13.0f), text_col,
                           label.c_str());
    }
}

void render_memory_mb_row(ImDrawList* draw_list,
                          performance_profiler* profiler,
                          int32_t first_frame,
                          int32_t last_frame,
                          ImVec2 row_top_left,
                          float bar_width,
                          float hist_start,
                          float entry_w,
                          float scale_max_mb,
                          memory_histogram_metric metric,
                          ImU32 fill_col)
{
    const float inner_h = memory_hist_inner_height();
    const float bottom_y = row_top_left.y + memory_hist_row_height;
    draw_list->PushClipRect(row_top_left,
                            ImVec2(row_top_left.x + bar_width, row_top_left.y + memory_hist_row_height),
                            true);

    constexpr float min_column_px = 1.0f;
    const bool use_pixel_buckets = entry_w < min_column_px && entry_w > 0.0f;
    const bool draw_outline = entry_w >= 4.0f;

    auto draw_mem_col = [&](float x0, float x1, float mb)
    {
        const float h_frac =
            (scale_max_mb > 0.001f) ? std::clamp(mb / scale_max_mb, 0.02f, 1.0f) : 0.02f;
        const float bar_h = inner_h * h_frac;
        const float y_top = bottom_y - bar_h;
        draw_list->AddRectFilled(ImVec2(x0, y_top), ImVec2(x1, bottom_y), fill_col);
        if(draw_outline && (x1 - x0) >= 2.0f)
        {
            draw_list->AddRect(ImVec2(x0 + 0.5f, y_top + 0.5f), ImVec2(x1 - 0.5f, bottom_y - 0.5f),
                               IM_COL32(100, 100, 120, 100), 0.0f, 0, 1.0f);
        }
    };

    if(use_pixel_buckets)
    {
        const int32_t col_count = std::max(1, static_cast<int32_t>(std::floor(bar_width)));
        for(int32_t col = 0; col < col_count; ++col)
        {
            const float i0f = hist_start + static_cast<float>(col) / entry_w;
            const float i1f = hist_start + static_cast<float>(col + 1) / entry_w;
            int32_t i0 = std::max(first_frame, static_cast<int32_t>(std::floor(i0f)));
            int32_t i1 = std::min(last_frame, static_cast<int32_t>(std::ceil(i1f)) - 1);
            if(i1 < i0)
            {
                continue;
            }
            float peak_mb = 0.0f;
            for(int32_t i = i0; i <= i1; ++i)
            {
                const auto* snap = profiler->get_frame_snapshot(static_cast<uint32_t>(i));
                if(snap != nullptr)
                {
                    peak_mb = std::max(peak_mb, memory_mb_from_snapshot(snap, metric));
                }
            }
            draw_mem_col(row_top_left.x + static_cast<float>(col),
                         row_top_left.x + static_cast<float>(col + 1),
                         peak_mb);
        }
    }
    else
    {
        for(int32_t i = first_frame; i <= last_frame; ++i)
        {
            const auto* snap = profiler->get_frame_snapshot(static_cast<uint32_t>(i));
            if(snap == nullptr)
            {
                continue;
            }
            const float x0 = row_top_left.x + (static_cast<float>(i) - hist_start) * entry_w;
            const float x1 = row_top_left.x + (static_cast<float>(i + 1) - hist_start) * entry_w;
            draw_mem_col(x0, x1, memory_mb_from_snapshot(snap, metric));
        }
    }

    draw_list->PopClipRect();
}

void render_live_sample_row(ImDrawList* draw_list,
                            ImVec2 row_top_left,
                            float bar_width,
                            const sample_data& samples,
                            float scale_max,
                            ImU32 fill_col,
                            bool is_frame_ms_row,
                            const sample_data* busy_samples = nullptr)
{
    const int n = static_cast<int>(sample_data::num_samples);
    if(n <= 0 || scale_max <= 0.0f)
    {
        return;
    }
    const float entry_w = bar_width / static_cast<float>(n);
    const float inner_h = is_frame_ms_row ? histogram_inner_height : memory_hist_inner_height();
    const float bottom_y = row_top_left.y + (is_frame_ms_row ? frame_bar_height : memory_hist_row_height);

    draw_list->PushClipRect(row_top_left,
                            ImVec2(row_top_left.x + bar_width,
                                   row_top_left.y + (is_frame_ms_row ? frame_bar_height : memory_hist_row_height)),
                            true);

    const ImU32 row_bg = is_frame_ms_row ? IM_COL32(30, 30, 30, 255) : IM_COL32(26, 26, 28, 255);
    draw_list->AddRectFilled(row_top_left, ImVec2(row_top_left.x + bar_width, bottom_y), row_bg);

    const int offset = samples.get_offset();
    const float* vals = samples.get_values();
    const float* busy_vals = (busy_samples != nullptr) ? busy_samples->get_values() : nullptr;
    const int busy_offset = (busy_samples != nullptr) ? busy_samples->get_offset() : 0;
    const bool draw_busy_split = is_frame_ms_row && busy_vals != nullptr;
    const bool draw_outline = entry_w >= 4.0f;
    const bool use_pixel_buckets = entry_w < 1.0f;

    auto busy_ratio_at = [&](int sample_col, float wall_ms) -> float
    {
        if(!draw_busy_split || wall_ms <= 0.001f)
        {
            return 1.0f;
        }
        const int bidx = (busy_offset + sample_col) % n;
        return std::clamp(busy_vals[bidx] / wall_ms, 0.0f, 1.0f);
    };

    if(use_pixel_buckets)
    {
        const int col_count = std::max(1, static_cast<int>(std::floor(bar_width)));
        const float samples_per_col = static_cast<float>(n) / static_cast<float>(col_count);
        for(int col = 0; col < col_count; ++col)
        {
            const int s0 = static_cast<int>(std::floor(static_cast<float>(col) * samples_per_col));
            int s1 = static_cast<int>(std::floor(static_cast<float>(col + 1) * samples_per_col)) - 1;
            s1 = std::max(s0, std::min(s1, n - 1));
            float peak = 0.0f;
            float peak_ratio = 1.0f;
            for(int s = s0; s <= s1; ++s)
            {
                const int idx = (offset + s) % n;
                const float v = vals[idx];
                if(v > peak)
                {
                    peak = v;
                    peak_ratio = busy_ratio_at(s, v);
                }
            }
            const float x0 = row_top_left.x + static_cast<float>(col);
            const float x1 = row_top_left.x + static_cast<float>(col + 1);
            if(is_frame_ms_row)
            {
                draw_histogram_column(draw_list, x0, x1, bottom_y, peak, peak_ratio, scale_max, draw_busy_split,
                                      false);
            }
            else
            {
                const float h_frac = std::clamp(peak / scale_max, 0.02f, 1.0f);
                const float bar_h = inner_h * h_frac;
                const float y_top = bottom_y - bar_h;
                draw_list->AddRectFilled(ImVec2(x0, y_top), ImVec2(x1, bottom_y), fill_col);
            }
        }
    }
    else
    {
        for(int col = 0; col < n; ++col)
        {
            const int idx = (offset + col) % n;
            const float v = vals[idx];
            const float x0 = row_top_left.x + static_cast<float>(col) * entry_w;
            const float x1 = row_top_left.x + static_cast<float>(col + 1) * entry_w;
            if(is_frame_ms_row)
            {
                draw_histogram_column(draw_list,
                                      x0,
                                      x1,
                                      bottom_y,
                                      v,
                                      busy_ratio_at(col, v),
                                      scale_max,
                                      draw_busy_split,
                                      draw_outline);
            }
            else
            {
                const float h_frac = std::clamp(v / scale_max, 0.02f, 1.0f);
                const float bar_h = inner_h * h_frac;
                const float y_top = bottom_y - bar_h;
                draw_list->AddRectFilled(ImVec2(x0, y_top), ImVec2(x1, bottom_y), fill_col);
                if(draw_outline)
                {
                    draw_list->AddRect(ImVec2(x0 + 0.5f, y_top + 0.5f), ImVec2(x1 - 0.5f, bottom_y - 0.5f),
                                       IM_COL32(100, 100, 120, 80), 0.0f, 0, 1.0f);
                }
            }
        }
    }

    draw_list->PopClipRect();
}

void render_histogram_cursor(ImDrawList* draw_list,
                              float plot_top_y,
                              float bottom_y,
                              float bar_width,
                              int32_t selected_idx,
                              float hist_start,
                              float entry_w,
                              float canvas_x)
{
    float cursor_x = canvas_x + (static_cast<float>(selected_idx) + 0.5f - hist_start) * entry_w;
    if(cursor_x < canvas_x - 10.0f || cursor_x > canvas_x + bar_width + 10.0f)
    {
        return;
    }

    draw_list->AddLine(ImVec2(cursor_x, plot_top_y),
                       ImVec2(cursor_x, bottom_y),
                       IM_COL32(255, 255, 255, 220), 1.5f);

    constexpr float tri_half = 5.0f;
    constexpr float tri_h = 7.0f;
    draw_list->AddTriangleFilled(
        ImVec2(cursor_x - tri_half, plot_top_y),
        ImVec2(cursor_x + tri_half, plot_top_y),
        ImVec2(cursor_x, plot_top_y + tri_h),
        IM_COL32(255, 255, 255, 230));
}

} // namespace

void profiler_timeline_panel::timeline_render_event_block(const lane_context& lc,
                                                          const profile_event& ev,
                                                          bool is_reference_frame,
                                                          const std::string& thread_name,
                                                          profiler_timeline_panel* panel,
                                                          uint32_t hist_frame_idx,
                                                          uint16_t thread_idx,
                                                          uint32_t event_idx)
{
    const event_lane_geom g = compute_event_geom(lc, ev);
    if(!g.ok)
    {
        return;
    }

    const float x0 = g.x0;
    const float y0 = g.y0;
    const float x1 = g.x1;
    const float y1 = g.y1;

    const bool is_scope_selected =
        panel != nullptr && panel->is_timeline_scope_selected(hist_frame_idx, thread_idx, event_idx);

    if(panel != nullptr && hist_frame_idx != UINT32_MAX &&
       ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_None))
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        if(mouse.x >= x0 && mouse.x <= x1 && mouse.y >= y0 && mouse.y <= y1)
        {
            panel->set_timeline_scope_selection(hist_frame_idx, thread_idx, event_idx, ev.name());
        }
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImU32 cpu_color = color_from_hash(ev.color_hash);
    if(!is_reference_frame)
    {
        cpu_color = dim_color(cpu_color);
    }

    const float cpu_ratio = compute_cpu_ratio(ev);
    const float rect_w = x1 - x0;
    const float cpu_x1 = x0 + rect_w * cpu_ratio;

    draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(cpu_x1, y1), cpu_color);

    if(cpu_ratio < 0.99f)
    {
        const ImU32 wait_col = is_reference_frame ? wait_color_selected : wait_color_dimmed;
        draw_list->AddRectFilled(ImVec2(cpu_x1, y0), ImVec2(x1, y1), wait_col);
    }

    draw_list->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(0, 0, 0, 80));

    if(is_scope_selected)
    {
        draw_list->AddRect(ImVec2(x0 - 1.0f, y0 - 1.0f), ImVec2(x1 + 1.0f, y1 + 1.0f),
                           IM_COL32(255, 230, 90, 255), 0.0f, 0, 2.5f);
    }

    const float rect_h = y1 - y0;
    constexpr float min_text_width_px = 8.0f;
    if(rect_w > min_text_width_px)
    {
        ImVec2 text_size = ImGui::CalcTextSize(ev.name());
        float fit_ratio = std::clamp(rect_w / std::max(text_size.x + 4.0f, 1.0f), 0.0f, 1.0f);
        int base_alpha = is_reference_frame ? 240 : 180;
        int alpha = static_cast<int>(static_cast<float>(base_alpha) * fit_ratio);
        ImU32 text_col = is_reference_frame
            ? IM_COL32(255, 255, 255, alpha)
            : IM_COL32(180, 180, 180, alpha);

        float tx = x0 + std::max(0.0f, (rect_w - text_size.x) * 0.5f);
        float ty = y0 + (rect_h - text_size.y) * 0.5f;

        draw_list->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1), true);
        draw_list->AddText(ImVec2(tx, ty), text_col, ev.name());
        draw_list->PopClipRect();
    }

    const ImVec2 mouse = ImGui::GetMousePos();
    if(mouse.x >= x0 && mouse.x <= x1 && mouse.y >= y0 && mouse.y <= y1)
    {
        const float wall_ms = static_cast<float>(ev.end_ns - ev.start_ns) / 1'000'000.0f;
        const float cpu_ms = static_cast<float>(ev.cpu_end_ns - ev.cpu_start_ns) / 1'000'000.0f;
        const float wait_ms = std::max(0.0f, wall_ms - cpu_ms);

        ImGui::SetNextWindowViewportToCurrent();
        ImGui::BeginTooltip();
        ImGui::Text("%s", ev.name());
        ImGui::Text("Wall:   %s", format_time(wall_ms).c_str());
        ImGui::Text("Busy:   %s (%.0f%%)", format_time(cpu_ms).c_str(), cpu_ratio * 100.0f);
        if(wait_ms > 0.0001f)
        {
            ImGui::Text("Idle:   %s (%.0f%%)", format_time(wait_ms).c_str(),
                        (1.0f - cpu_ratio) * 100.0f);
        }
        ImGui::Text("Depth:  %d", ev.depth);
        ImGui::Text("Thread: %s", thread_name.c_str());
        ImGui::EndTooltip();
    }
}

void profiler_timeline_panel::validate_timeline_scope_selection(uint32_t frame_count)
{
    if(!has_timeline_scope_selection_)
    {
        return;
    }
    if(frame_count == 0 || selected_scope_hist_frame_ >= frame_count)
    {
        has_timeline_scope_selection_ = false;
        selected_scope_label_.clear();
        return;
    }

    auto* profiler = get_app_profiler();
    const frame_snapshot* snap = profiler->get_frame_snapshot(selected_scope_hist_frame_);
    if(!snap)
    {
        has_timeline_scope_selection_ = false;
        selected_scope_label_.clear();
        return;
    }

    for(const auto& ts : snap->threads)
    {
        if(ts.thread_index != selected_scope_thread_index_)
        {
            continue;
        }
        if(selected_scope_event_index_ >= ts.events.size())
        {
            has_timeline_scope_selection_ = false;
            selected_scope_label_.clear();
        }
        return;
    }

    has_timeline_scope_selection_ = false;
    selected_scope_label_.clear();
}

profiler_timeline_panel::profiler_timeline_panel(imgui_panels* parent, const char* name)
    : name_(name)
    , parent_(parent)
{
}

void profiler_timeline_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(show_request_)
    {
        show_request_ = false;
        show_ = true;
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * 0.5f, ImGuiCond_Once);
    }

    if(!show_)
    {
        return;
    }

    if(ImGui::Begin(name, &show_))
    {
        draw_ui(ctx);
    }
    ImGui::End();
}

void profiler_timeline_panel::show(bool s)
{
    show_request_ = s;
}

void profiler_timeline_panel::draw_ui(rtti::context& ctx)
{
    draw_recording_toolbar();

    ImGui::Separator();

    draw_frame_selector_bar();

    ImGui::Separator();

    if(has_timeline_scope_selection_ && !selected_scope_label_.empty())
    {
        ImGui::TextDisabled("Selected:");
        ImGui::SameLine();
        ImGui::TextUnformatted(selected_scope_label_.c_str());
    }

    draw_timeline();

    ImGui::Separator();

    draw_aggregate_section();

    draw_profiler_bottom_sections(ctx);
}

// ============================================================================
// Recording toolbar
// ============================================================================

void profiler_timeline_panel::draw_recording_toolbar()
{
    auto* profiler = get_app_profiler();
    const bool is_recording = profiler->get_recording_state() == recording_state::recording;

    if(is_recording)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    }
    if(ImGui::Button(ICON_MDI_RECORD " Record"))
    {
        if(is_recording)
        {
            profiler->set_recording_state(recording_state::paused);
            auto_follow_ = false;
            const uint32_t count = profiler->get_frame_count();
            if(count > 0)
            {
                selected_frame_ = static_cast<int32_t>(count - 1);
                last_centered_frame_ = -2;
            }
        }
        else
        {
            profiler->set_recording_state(recording_state::recording);
            auto_follow_ = true;
            last_centered_frame_ = -2;
        }
    }
    if(is_recording)
    {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    if(ImGui::Button(ICON_MDI_DELETE " Clear"))
    {
        profiler->clear_history();
        selected_frame_ = -1;
        last_centered_frame_ = -2;
        has_timeline_scope_selection_ = false;
        selected_scope_label_.clear();
    }

    uint32_t frame_count = profiler->get_frame_count();

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    if(frame_count > 0)
    {
        int32_t display_idx = auto_follow_ ? static_cast<int32_t>(frame_count - 1) : selected_frame_;
        if(display_idx >= 0)
        {
            ImGui::Text("Frame %d / %u", display_idx + 1, frame_count);
        }
        else
        {
            ImGui::Text("%u frames", frame_count);
        }
    }
    else
    {
        ImGui::TextDisabled("No frames captured");
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    if(ImGui::Button(ICON_MDI_FIT_TO_PAGE " Fit"))
    {
        last_centered_frame_ = -2;
        view_duration_ns_ = 20'000'000.0;
        hist_start_ = 0.0f;
        hist_range_ = 0.0f;
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    {
        static constexpr uint32_t history_presets[] = {64, 128, 256, 512, 1024, 2000};
        static constexpr const char* history_labels[] = {"64", "128", "256", "512", "1024", "2000"};
        const uint32_t current_cap = profiler->get_max_frame_history();
        int current_idx = 2; // default 256
        for(int i = 0; i < static_cast<int>(IM_ARRAYSIZE(history_presets)); ++i)
        {
            if(history_presets[i] == current_cap)
            {
                current_idx = i;
                break;
            }
        }
        ImGui::SetNextItemWidth(80.0f);
        if(ImGui::Combo("History", &current_idx, history_labels, IM_ARRAYSIZE(history_labels)))
        {
            profiler->set_max_frame_history(history_presets[current_idx]);
            if(selected_frame_ >= 0)
            {
                const uint32_t count = profiler->get_frame_count();
                if(count == 0)
                {
                    selected_frame_ = -1;
                }
                else
                {
                    selected_frame_ = std::min(selected_frame_, static_cast<int32_t>(count) - 1);
                }
            }
            last_centered_frame_ = -2;
        }
        ImGui::SetItemTooltipEx("Max captured frames. Lower values reduce histogram cost.");
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    double visible_ms = view_duration_ns_ / 1'000'000.0;
    if(visible_ms >= 1.0)
    {
        ImGui::Text("%.1f ms visible", visible_ms);
    }
    else
    {
        ImGui::Text("%.0f us visible", visible_ms * 1000.0);
    }
}

// ============================================================================
// Frame selector histogram
// ============================================================================

auto profiler_timeline_panel::histogram_stack_height() const -> float
{
    float h = frame_bar_height;
    if(show_histogram_managed_heap_)
    {
        h += memory_hist_row_height;
    }
    if(show_histogram_gpu_memory_)
    {
        h += memory_hist_row_height;
    }
    if(show_histogram_process_rss_)
    {
        h += memory_hist_row_height;
    }
    return h;
}

void profiler_timeline_panel::draw_profiler_bottom_sections(rtti::context& ctx)
{
    if(parent_ == nullptr)
    {
        return;
    }
    ImGui::PushID("profiler_bottom");
    if(ImGui::CollapsingHeader(ICON_MDI_CHIP "\tRender Passes"))
    {
        ImGui::PushFont(ImGui::Font::Mono);
        draw_gpu_submit_profiler_ui(gfx::get_stats(), &parent_->gpu_profiler_enabled());
        ImGui::PopFont();
    }
    profiler_draw_gpu_resources_section();
    if(ctx.has<settings>())
    {
        profiler_draw_eviction_section(ctx.get<settings>().graphics.eviction);
    }
    ImGui::PopID();
}

void profiler_timeline_panel::draw_live_histogram_stack(float bar_width)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float max_ms = std::max(frame_time_history_.get_max(), target_60fps_ms) * 1.1f;
    const float max_cpu_mb = std::max(cpu_heap_mb_history_.get_max(), 1.0f) * 1.1f;
    const float max_gpu_mb = std::max(gpu_memory_mb_history_.get_max(), 1.0f) * 1.1f;
    const float max_rss_mb = std::max(process_rss_mb_history_.get_max(), 1.0f) * 1.1f;

    render_live_sample_row(dl, pos, bar_width, frame_time_history_, max_ms, 0, true, &frame_busy_ms_history_);
    const float frame_bottom = pos.y + frame_bar_height;
    render_histogram_guides(dl, pos, bar_width, frame_bottom, max_ms);
    dl->AddText(ImVec2(pos.x + 4, pos.y + 2),
                IM_COL32(180, 180, 200, 200),
                "Frame wall (ms) — color: busy, gray: wait");

    float row_y = frame_bottom;
    if(show_histogram_managed_heap_)
    {
        const ImVec2 row(pos.x, row_y);
        render_live_sample_row(dl, row, bar_width, cpu_heap_mb_history_, max_cpu_mb, cpu_heap_hist_color, false);
        dl->AddText(ImVec2(row.x + 4, row.y + 2), IM_COL32(180, 180, 200, 200), "Managed heap (MB)");
        render_memory_histogram_guides(dl, row, bar_width, max_cpu_mb);
        row_y += memory_hist_row_height;
    }
    if(show_histogram_gpu_memory_)
    {
        const ImVec2 row(pos.x, row_y);
        render_live_sample_row(dl, row, bar_width, gpu_memory_mb_history_, max_gpu_mb, gpu_mem_hist_color, false);
        dl->AddText(ImVec2(row.x + 4, row.y + 2), IM_COL32(180, 180, 200, 200), "GPU memory (MB)");
        render_memory_histogram_guides(dl, row, bar_width, max_gpu_mb);
        row_y += memory_hist_row_height;
    }
    if(show_histogram_process_rss_)
    {
        const ImVec2 row(pos.x, row_y);
        render_live_sample_row(dl, row, bar_width, process_rss_mb_history_, max_rss_mb, process_rss_hist_color, false);
        dl->AddText(ImVec2(row.x + 4, row.y + 2), IM_COL32(180, 180, 200, 200), "Process RSS (MB)");
        render_memory_histogram_guides(dl, row, bar_width, max_rss_mb);
    }

    ImGui::Dummy(ImVec2(bar_width, histogram_stack_height()));
}

void profiler_timeline_panel::draw_frame_selector_bar()
{
    auto* profiler = get_app_profiler();

    auto frame_start = profiler->get_frame_start_ns();
    auto frame_end = profiler->get_frame_end_ns();
    float frame_ms = 0.0f;
    if(frame_end > frame_start)
    {
        frame_ms = static_cast<float>(frame_end - frame_start) / 1'000'000.0f;
    }
    frame_time_history_.push_sample(frame_ms);

    const uint32_t frame_count = profiler->get_frame_count();
    float frame_busy_ms = frame_ms;
    if(frame_count > 0)
    {
        const frame_snapshot* latest = profiler->get_frame_snapshot(frame_count - 1);
        if(latest != nullptr)
        {
            frame_busy_ms = latest->frame_busy_ms;
        }
    }
    frame_busy_ms_history_.push_sample(frame_busy_ms);

    ImGui::Checkbox("Managed heap", &show_histogram_managed_heap_);
    ImGui::SameLine();
    ImGui::Checkbox("GPU memory", &show_histogram_gpu_memory_);
    ImGui::SameLine();
    ImGui::Checkbox("Process RSS", &show_histogram_process_rss_);

    // Use captured snapshots whenever available (recording and paused) so bar count
    // matches History capacity. Live rolling samples are only for the empty pre-record state.
    if(frame_count == 0)
    {
        const float cpu_mb = static_cast<float>(dotnet::gc_get_used_size()) / megabyte_divisor;
        cpu_heap_mb_history_.push_sample(cpu_mb);
        float gpu_mb = 0.0f;
        auto* stats = gfx::get_stats();
        if(stats)
        {
            gpu_mb = static_cast<float>(stats->gpuMemoryUsed) / megabyte_divisor;
        }
        gpu_memory_mb_history_.push_sample(gpu_mb);
        const float rss_mb =
            static_cast<float>(platform::get_process_resident_set_bytes()) / megabyte_divisor;
        process_rss_mb_history_.push_sample(rss_mb);
        auto region = ImGui::GetContentRegionAvail();
        draw_live_histogram_stack(region.x);
        return;
    }

    auto region = ImGui::GetContentRegionAvail();
    float bar_width = region.x;
    if(bar_width < 10.0f)
    {
        return;
    }

    draw_frame_histogram(profiler, frame_count, bar_width);
}

void profiler_timeline_panel::draw_frame_histogram(performance_profiler* profiler,
                                                    uint32_t frame_count,
                                                    float bar_width)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    const float frame_bottom_y = canvas_pos.y + frame_bar_height;

    float fc_f = static_cast<float>(frame_count);
    float eff_range = (hist_range_ <= 0.0f) ? fc_f : std::min(hist_range_, fc_f);
    float eff_start = std::clamp(hist_start_, 0.0f, std::max(0.0f, fc_f - eff_range));

    if(auto_follow_)
    {
        eff_start = std::max(0.0f, fc_f - eff_range);
        hist_start_ = eff_start;
    }

    float entry_w = bar_width / eff_range;

    int32_t first_vis = std::max(0, static_cast<int32_t>(std::floor(eff_start)));
    int32_t last_vis = std::min(static_cast<int32_t>(frame_count) - 1,
                                static_cast<int32_t>(std::ceil(eff_start + eff_range)));

    draw_list->AddRectFilled(canvas_pos,
                             ImVec2(canvas_pos.x + bar_width, frame_bottom_y),
                             IM_COL32(30, 30, 30, 255));

    float max_frame_ms = target_60fps_ms;
    float max_cpu_mb = 1.0f;
    float max_gpu_mb = 1.0f;
    float max_rss_mb = 1.0f;
    for(int32_t i = first_vis; i <= last_vis; ++i)
    {
        const auto* snap = profiler->get_frame_snapshot(static_cast<uint32_t>(i));
        if(snap == nullptr)
        {
            continue;
        }
        max_frame_ms = std::max(max_frame_ms, snap->frame_wall_ms);
        if(show_histogram_managed_heap_)
        {
            max_cpu_mb =
                std::max(max_cpu_mb, static_cast<float>(snap->cpu_heap_used_bytes) / megabyte_divisor);
        }
        if(show_histogram_gpu_memory_)
        {
            max_gpu_mb =
                std::max(max_gpu_mb, static_cast<float>(snap->gpu_memory_used_bytes) / megabyte_divisor);
        }
        if(show_histogram_process_rss_)
        {
            max_rss_mb =
                std::max(max_rss_mb, static_cast<float>(snap->process_resident_bytes) / megabyte_divisor);
        }
    }
    float scale_max = max_frame_ms * 1.1f;
    const float scale_cpu_mb = max_cpu_mb * 1.1f;
    const float scale_gpu_mb = max_gpu_mb * 1.1f;
    const float scale_rss_mb = max_rss_mb * 1.1f;

    int32_t effective_selected = auto_follow_
        ? static_cast<int32_t>(frame_count) - 1
        : selected_frame_;
    effective_selected = std::clamp(effective_selected, 0, static_cast<int32_t>(frame_count) - 1);

    draw_list->AddText(ImVec2(canvas_pos.x + 4, canvas_pos.y + 2),
                       IM_COL32(180, 180, 200, 200),
                       "Frame wall (ms) — color: busy, gray: wait");

    render_histogram_bars(draw_list, profiler, first_vis, last_vis,
                          canvas_pos, frame_bottom_y, bar_width, eff_start, entry_w, scale_max);
    render_histogram_guides(draw_list, canvas_pos, bar_width, frame_bottom_y, scale_max);

    float mem_row_y = frame_bottom_y;
    if(show_histogram_managed_heap_)
    {
        const ImVec2 cpu_row_top(canvas_pos.x, mem_row_y);
        draw_list->AddRectFilled(cpu_row_top,
                                 ImVec2(canvas_pos.x + bar_width, mem_row_y + memory_hist_row_height),
                                 IM_COL32(26, 26, 28, 255));
        draw_list->AddText(ImVec2(cpu_row_top.x + 4, cpu_row_top.y + 2),
                           IM_COL32(180, 180, 200, 200), "Managed heap (MB)");
        render_memory_mb_row(draw_list, profiler, first_vis, last_vis, cpu_row_top, bar_width, eff_start,
                             entry_w, scale_cpu_mb, memory_histogram_metric::managed_heap_mb, cpu_heap_hist_color);
        render_memory_histogram_guides(draw_list, cpu_row_top, bar_width, scale_cpu_mb);
        mem_row_y += memory_hist_row_height;
    }
    if(show_histogram_gpu_memory_)
    {
        const ImVec2 gpu_row_top(canvas_pos.x, mem_row_y);
        draw_list->AddRectFilled(gpu_row_top,
                                 ImVec2(canvas_pos.x + bar_width, mem_row_y + memory_hist_row_height),
                                 IM_COL32(26, 26, 28, 255));
        draw_list->AddText(ImVec2(gpu_row_top.x + 4, gpu_row_top.y + 2),
                           IM_COL32(180, 180, 200, 200), "GPU memory (MB)");
        render_memory_mb_row(draw_list, profiler, first_vis, last_vis, gpu_row_top, bar_width, eff_start,
                             entry_w, scale_gpu_mb, memory_histogram_metric::gpu_memory_mb, gpu_mem_hist_color);
        render_memory_histogram_guides(draw_list, gpu_row_top, bar_width, scale_gpu_mb);
        mem_row_y += memory_hist_row_height;
    }
    if(show_histogram_process_rss_)
    {
        const ImVec2 rss_row_top(canvas_pos.x, mem_row_y);
        draw_list->AddRectFilled(rss_row_top,
                                 ImVec2(canvas_pos.x + bar_width, mem_row_y + memory_hist_row_height),
                                 IM_COL32(26, 26, 28, 255));
        draw_list->AddText(ImVec2(rss_row_top.x + 4, rss_row_top.y + 2),
                           IM_COL32(180, 180, 200, 200), "Process RSS (MB)");
        render_memory_mb_row(draw_list, profiler, first_vis, last_vis, rss_row_top, bar_width, eff_start,
                             entry_w, scale_rss_mb, memory_histogram_metric::process_rss_mb, process_rss_hist_color);
        render_memory_histogram_guides(draw_list, rss_row_top, bar_width, scale_rss_mb);
    }

    const float stack_bottom_y = canvas_pos.y + histogram_stack_height();
    const float plot_top_y = canvas_pos.y + histogram_plot_top_pad;
    render_histogram_cursor(draw_list, plot_top_y, stack_bottom_y, bar_width,
                            effective_selected, eff_start, entry_w, canvas_pos.x);

    ImGui::InvisibleButton("##frame_histogram", ImVec2(bar_width, this->histogram_stack_height()));

    handle_histogram_input(profiler, frame_count, canvas_pos, bar_width, eff_start, eff_range);
}

void profiler_timeline_panel::handle_histogram_input(performance_profiler* profiler,
                                                      uint32_t frame_count,
                                                      ImVec2 canvas_pos,
                                                      float bar_width,
                                                      float eff_start,
                                                      float eff_range)
{
    float entry_w = bar_width / eff_range;

    handle_histogram_zoom_pan(frame_count, canvas_pos, bar_width, eff_start, eff_range);

    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    if(hovered || active)
    {
        float mouse_x = ImGui::GetMousePos().x - canvas_pos.x;
        int32_t hover_idx = static_cast<int32_t>(eff_start + mouse_x / entry_w);
        hover_idx = std::clamp(hover_idx, 0, static_cast<int32_t>(frame_count) - 1);

        if(hovered)
        {
            const auto* hsnap = profiler->get_frame_snapshot(static_cast<uint32_t>(hover_idx));
            if(hsnap && hsnap->frame_wall_ms > 0.0f)
            {
                const float hms = hsnap->frame_wall_ms;
                const float busy_ms = hsnap->frame_busy_ms;
                const float wait_ms = std::max(0.0f, hms - busy_ms);
                const float busy_pct = (hms > 0.001f) ? (busy_ms / hms) * 100.0f : 0.0f;

                ImGui::SetNextWindowViewportToCurrent();
                ImGui::BeginTooltip();
                ImGui::Text("Frame %d / %u", hover_idx + 1, frame_count);
                ImGui::Text("Wall:       %.2f ms (%.0f FPS)", hms, hms > 0.001f ? 1000.0f / hms : 0.0f);
                ImGui::Text("Busy:  %.2f ms (%.0f%%)", busy_ms, busy_pct);
                if(wait_ms > 0.001f)
                {
                    ImGui::Text("Wait:  %.2f ms (%.0f%%)", wait_ms, 100.0f - busy_pct);
                }
                const auto heap_pretty = format_bytes(
                    static_cast<std::uint64_t>(std::max<int64_t>(0, hsnap->cpu_heap_used_bytes)), 0);
                const auto gpu_pretty = format_bytes(
                    static_cast<std::uint64_t>(std::max<int64_t>(0, hsnap->gpu_memory_used_bytes)), 0);
                const auto rss_pretty = format_bytes(
                    static_cast<std::uint64_t>(std::max<int64_t>(0, hsnap->process_resident_bytes)), 0);
                if(show_histogram_managed_heap_)
                {
                    ImGui::Text("Managed heap: %s", heap_pretty.c_str());
                }
                if(show_histogram_gpu_memory_)
                {
                    ImGui::Text("GPU memory: %s", gpu_pretty.c_str());
                }
                if(show_histogram_process_rss_)
                {
                    ImGui::Text("Process RSS: %s", rss_pretty.c_str());
                }
                ImGui::EndTooltip();
            }
        }

        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) || is_dragging_cursor_)
        {
            selected_frame_ = hover_idx;
            auto_follow_ = false;
            last_centered_frame_ = -2;
            is_dragging_cursor_ = true;
        }
    }

    if(is_dragging_cursor_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        is_dragging_cursor_ = false;
    }

    if(ImGui::IsItemFocused())
    {
        if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && selected_frame_ > 0)
        {
            selected_frame_--;
            auto_follow_ = false;
            last_centered_frame_ = -2;
        }
        if(ImGui::IsKeyPressed(ImGuiKey_RightArrow) &&
           selected_frame_ < static_cast<int32_t>(frame_count) - 1)
        {
            selected_frame_++;
            auto_follow_ = false;
            last_centered_frame_ = -2;
        }
    }
}

void profiler_timeline_panel::handle_histogram_zoom_pan(uint32_t frame_count,
                                                         ImVec2 canvas_pos,
                                                         float bar_width,
                                                         float eff_start,
                                                         float eff_range)
{
    if(!ImGui::IsItemHovered())
    {
        return;
    }

    float fc_f = static_cast<float>(frame_count);
    float mouse_x = ImGui::GetMousePos().x - canvas_pos.x;
    float mouse_frac = std::clamp(mouse_x / bar_width, 0.0f, 1.0f);

    float wheel = ImGui::GetIO().MouseWheel;
    if(wheel != 0.0f)
    {
        if(ImGui::GetIO().KeyCtrl)
        {
            float mouse_frame = eff_start + mouse_frac * eff_range;
            float new_range = eff_range * (1.0f - wheel * 0.15f);
            new_range = std::clamp(new_range, 10.0f, fc_f);

            hist_start_ = mouse_frame - mouse_frac * new_range;
            hist_start_ = std::clamp(hist_start_, 0.0f, std::max(0.0f, fc_f - new_range));
            hist_range_ = new_range;
        }
    }

    if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        float frames_per_px = eff_range / bar_width;
        hist_start_ -= ImGui::GetIO().MouseDelta.x * frames_per_px;
        hist_start_ = std::clamp(hist_start_, 0.0f, std::max(0.0f, fc_f - eff_range));
    }
}

// ============================================================================
// Time ruler
// ============================================================================

void profiler_timeline_panel::draw_time_ruler(double view_start_ns,
                                               double reference_ns,
                                               double ns_per_pixel,
                                               float ruler_width,
                                               ImVec2 canvas_pos)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(canvas_pos,
                             ImVec2(canvas_pos.x + ruler_width, canvas_pos.y + ruler_height),
                             IM_COL32(40, 40, 40, 255));

    double view_end_ns = view_start_ns + static_cast<double>(ruler_width) * ns_per_pixel;
    double visible_ms = (view_end_ns - view_start_ns) / 1'000'000.0;
    double raw_interval = visible_ms / 10.0;

    static constexpr std::array nice_intervals = {
        0.001, 0.002, 0.005, 0.01, 0.02, 0.05,
        0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0};

    double tick_ms = nice_intervals.back();
    for(double ni : nice_intervals)
    {
        if(ni >= raw_interval)
        {
            tick_ms = ni;
            break;
        }
    }

    double tick_ns = tick_ms * 1'000'000.0;

    double ref_offset = view_start_ns - reference_ns;
    double first_tick_offset = std::ceil(ref_offset / tick_ns) * tick_ns;

    for(double offset = first_tick_offset; ; offset += tick_ns)
    {
        double abs_ns = reference_ns + offset;
        if(abs_ns > view_end_ns)
        {
            break;
        }

        float x = canvas_pos.x + static_cast<float>((abs_ns - view_start_ns) / ns_per_pixel);
        if(x < canvas_pos.x)
        {
            continue;
        }

        draw_list->AddLine(ImVec2(x, canvas_pos.y + ruler_height * 0.5f),
                           ImVec2(x, canvas_pos.y + ruler_height),
                           IM_COL32(200, 200, 200, 180));

        double label_ms = offset / 1'000'000.0;
        std::string label;
        if(tick_ms >= 1.0)
        {
            label = fmt::format("{:.0f}ms", label_ms);
        }
        else if(tick_ms >= 0.01)
        {
            label = fmt::format("{:.2f}ms", label_ms);
        }
        else
        {
            label = fmt::format("{:.3f}ms", label_ms);
        }

        draw_list->AddText(ImVec2(x + 2.0f, canvas_pos.y + 2.0f),
                           IM_COL32(200, 200, 200, 220),
                           label.c_str());
    }
}

// ============================================================================
// Timeline (multi-frame, shared time axis)
// ============================================================================

void profiler_timeline_panel::draw_timeline()
{
    auto* profiler = get_app_profiler();
    uint32_t frame_count = profiler->get_frame_count();
    validate_timeline_scope_selection(frame_count);
    if(profiler->get_recording_state() == recording_state::recording)
    {
        ImGui::TextDisabled("Recording...");
        return;
    }
    if(frame_count == 0)
    {
        ImGui::TextDisabled("No frame data to display");
        return;
    }

    int32_t frame_idx = auto_follow_
        ? static_cast<int32_t>(frame_count) - 1
        : selected_frame_;
    frame_idx = std::clamp(frame_idx, 0, static_cast<int32_t>(frame_count) - 1);

    const frame_snapshot* selected_snap = profiler->get_frame_snapshot(
        static_cast<uint32_t>(frame_idx));
    if(!selected_snap || selected_snap->frame_end_ns <= selected_snap->frame_start_ns)
    {
        ImGui::TextDisabled("No valid frame data");
        return;
    }

    double sel_start = static_cast<double>(selected_snap->frame_start_ns);
    double sel_end = static_cast<double>(selected_snap->frame_end_ns);
    const double sel_duration_ns = sel_end - sel_start;
    const bool selection_changed = (frame_idx != last_centered_frame_);

    // -- View positioning ------------------------------------------------
    if(selection_changed)
    {
        // Grow the visible window to fit a longer frame; do not shrink for short ones
        // (preserves manual zoom-out and avoids fighting zoom-in every redraw).
        constexpr double frame_fit_padding = 1.1;
        if(sel_duration_ns > view_duration_ns_)
        {
            view_duration_ns_ = std::clamp(sel_duration_ns * frame_fit_padding,
                                           min_view_duration_ns,
                                           max_view_duration_ns);
        }
        last_centered_frame_ = frame_idx;
    }
    if(auto_follow_)
    {
        view_start_ns_ = sel_end - view_duration_ns_ * 0.85;
    }
    else if(selection_changed)
    {
        const double center = (sel_start + sel_end) / 2.0;
        view_start_ns_ = center - view_duration_ns_ / 2.0;
    }

    // -- Layout ----------------------------------------------------------
    auto region = ImGui::GetContentRegionAvail();
    float lane_content_width = region.x - lane_header_width;
    if(lane_content_width < 10.0f)
    {
        return;
    }

    double ns_per_pixel = view_duration_ns_ / static_cast<double>(lane_content_width);
    double view_end_ns = view_start_ns_ + view_duration_ns_;

    // -- Gather visible frames -------------------------------------------
    std::vector<const frame_snapshot*> visible_frames;
    gather_visible_frames(profiler, frame_count, selected_snap, visible_frames);
    std::vector<uint32_t> visible_hist_indices;
    visible_hist_indices.reserve(visible_frames.size());
    for(const frame_snapshot* f : visible_frames)
    {
        visible_hist_indices.push_back(hist_index_of(profiler, f));
    }

    // -- Collect unique threads across visible frames --------------------
    std::vector<thread_entry> threads;
    collect_unique_threads(visible_frames, threads);

    if(threads.empty())
    {
        ImGui::TextDisabled("No thread data");
        return;
    }

    // -- Compute total height (must match layout below + spacing between lanes) --
    float total_height = ruler_height;
    for(const auto& t : threads)
    {
        const uint16_t layout_depth =
            max_nesting_depth_for_thread_in_view(visible_frames, t.index, view_start_ns_, view_end_ns);
        total_height += lane_height_for_depth(layout_depth);
    }
    if(threads.size() > 1)
    {
        total_height += thread_lane_spacing * static_cast<float>(threads.size() - 1);
    }

    float timeline_height = std::min(region.y * 0.65f,
                                     std::max(total_height + 10.0f, 100.0f));

    // -- Begin scrollable child ------------------------------------------
    ImGui::BeginChild("##timeline_scroll", ImVec2(0, timeline_height), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY,
                      ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 base_pos = ImGui::GetCursorScreenPos();

    // Time ruler
    draw_time_ruler(view_start_ns_, sel_start, ns_per_pixel,
                    lane_content_width,
                    ImVec2(base_pos.x + lane_header_width, base_pos.y));
    ImGui::Dummy(ImVec2(0, ruler_height));

    // -- Thread lanes ----------------------------------------------------
    for(size_t ti = 0; ti < threads.size(); ++ti)
    {
        const auto& thread = threads[ti];
        const uint16_t layout_depth =
            max_nesting_depth_for_thread_in_view(visible_frames, thread.index, view_start_ns_, view_end_ns);
        const float lane_height = lane_height_for_depth(layout_depth);

        ImGui::Text("%s", thread.name.c_str());
        ImGui::SameLine(lane_header_width);

        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();

        draw_list->AddRectFilled(canvas_pos,
            ImVec2(canvas_pos.x + lane_content_width, canvas_pos.y + lane_height),
            IM_COL32(30, 30, 30, 255));

        lane_context lc{canvas_pos, lane_content_width, lane_height,
                        view_start_ns_, view_end_ns, ns_per_pixel};

        draw_list->PushClipRect(canvas_pos,
                                ImVec2(canvas_pos.x + lane_content_width, canvas_pos.y + lane_height),
                                true);
        draw_lane_events(lc, visible_frames, visible_hist_indices, selected_snap,
                         thread.index, thread.name);
        draw_lane_frame_boundaries(lc, visible_frames, selected_snap);
        draw_list->PopClipRect();

        ImGui::Dummy(ImVec2(0, lane_height));
        if(ti + 1 < threads.size())
        {
            ImGui::Dummy(ImVec2(0, thread_lane_spacing));
        }
    }

    // Input while timeline child is active (correct scroll + zoom coordinates)
    ImGuiIO& io = ImGui::GetIO();
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
    {
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
            if(lane_content_width > 0)
            {
                const double ns_per_px = view_duration_ns_ / static_cast<double>(lane_content_width);
                view_start_ns_ -= static_cast<double>(io.MouseDelta.x) * ns_per_px;
            }
        }

        const float wheel = io.MouseWheel;
        if(wheel != 0.0f)
        {
            if(io.KeyCtrl)
            {
                const ImGuiWindow* win = ImGui::GetCurrentWindowRead();
                const double graph_left_d =
                    win != nullptr
                        ? static_cast<double>(win->InnerRect.Min.x + lane_header_width)
                        : static_cast<double>(io.MousePos.x);
                double mouse_x = static_cast<double>(io.MousePos.x) - graph_left_d;
                mouse_x = std::clamp(mouse_x, 0.0, static_cast<double>(lane_content_width));
                const double mouse_frac = mouse_x / static_cast<double>(lane_content_width);
                const double mouse_time = view_start_ns_ + mouse_frac * view_duration_ns_;

                view_duration_ns_ *= (1.0 - static_cast<double>(wheel) * 0.15);
                view_duration_ns_ =
                    std::clamp(view_duration_ns_, min_view_duration_ns, max_view_duration_ns);
                view_start_ns_ = mouse_time - mouse_frac * view_duration_ns_;
            }
            else
            {
                ImGui::SetScrollY(ImGui::GetScrollY() - wheel * timeline_wheel_scroll_px);
            }
        }
    }

    ImGui::EndChild();

    if(ImGui::IsItemHovered())
    {
        if(ImGui::IsKeyPressed(ImGuiKey_Escape) && has_timeline_scope_selection_)
        {
            has_timeline_scope_selection_ = false;
            selected_scope_label_.clear();
        }
    }
}

// ============================================================================
// Timeline helpers
// ============================================================================

void profiler_timeline_panel::gather_visible_frames(performance_profiler* profiler,
                                                     uint32_t frame_count,
                                                     const frame_snapshot* selected_snap,
                                                     std::vector<const frame_snapshot*>& out)
{
    double view_end_ns = view_start_ns_ + view_duration_ns_;
    out.reserve(32);

    for(uint32_t i = 0; i < frame_count; ++i)
    {
        const auto* snap = profiler->get_frame_snapshot(i);
        if(!snap || snap->frame_end_ns <= snap->frame_start_ns)
        {
            continue;
        }
        double snap_start = static_cast<double>(snap->frame_start_ns);
        double snap_end = static_cast<double>(snap->frame_end_ns);
        if(snap_end >= view_start_ns_ && snap_start <= view_end_ns)
        {
            out.push_back(snap);
        }
    }

    if(out.empty())
    {
        out.push_back(selected_snap);
    }
}

void profiler_timeline_panel::collect_unique_threads(
    const std::vector<const frame_snapshot*>& frames,
    std::vector<thread_entry>& out)
{
    for(const auto* frame : frames)
    {
        for(const auto& ts : frame->threads)
        {
            uint16_t local_max = 0;
            for(const auto& ev : ts.events)
            {
                if(ev.end_ns > ev.start_ns)
                {
                    local_max = std::max(local_max, ev.depth);
                }
            }

            auto it = std::find_if(out.begin(), out.end(),
                [&](const thread_entry& e) -> bool { return e.index == ts.thread_index; });

            if(it != out.end())
            {
                it->max_depth = std::max(it->max_depth, local_max);
            }
            else
            {
                out.push_back({ts.name, ts.thread_index, local_max});
            }
        }
    }
}

void profiler_timeline_panel::draw_lane_events(
    const lane_context& lc,
    const std::vector<const frame_snapshot*>& visible_frames,
    const std::vector<uint32_t>& visible_hist_indices,
    const frame_snapshot* selected_snap,
    uint16_t thread_index,
    const std::string& thread_name)
{
    for(size_t fi = 0; fi < visible_frames.size(); ++fi)
    {
        const frame_snapshot* frame = visible_frames[fi];
        const uint32_t hidx =
            fi < visible_hist_indices.size() ? visible_hist_indices[fi] : UINT32_MAX;
        const bool is_reference_frame = (frame == selected_snap);

        for(const auto& ts : frame->threads)
        {
            if(ts.thread_index != thread_index)
            {
                continue;
            }

            for(uint32_t event_idx = 0; event_idx < ts.events.size(); ++event_idx)
            {
                const profile_event& ev = ts.events[event_idx];
                if(ev.end_ns > ev.start_ns)
                {
                    timeline_render_event_block(lc, ev, is_reference_frame, thread_name, this, hidx,
                                                thread_index, event_idx);
                }
            }
        }
    }
}

void profiler_timeline_panel::draw_lane_frame_boundaries(
    const lane_context& lc,
    const std::vector<const frame_snapshot*>& visible_frames,
    const frame_snapshot* selected_snap)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    for(const auto* frame : visible_frames)
    {
        double boundary = static_cast<double>(frame->frame_end_ns);
        float bx = lc.canvas_pos.x + static_cast<float>(
            (boundary - lc.view_start_ns) / lc.ns_per_pixel);

        if(bx > lc.canvas_pos.x && bx < lc.canvas_pos.x + lc.lane_content_width)
        {
            ImU32 line_col = (frame == selected_snap)
                ? IM_COL32(255, 200, 50, 100)
                : IM_COL32(255, 255, 255, 40);
            draw_list->AddLine(ImVec2(bx, lc.canvas_pos.y),
                               ImVec2(bx, lc.canvas_pos.y + lc.lane_height),
                               line_col);
        }
    }
}

// ============================================================================
// Aggregate data section
// ============================================================================

void profiler_timeline_panel::draw_aggregate_section()
{
    if(!ImGui::CollapsingHeader(ICON_MDI_CLOCK_OUTLINE "\tAggregate"))
    {
        return;
    }

    auto* profiler = get_app_profiler();
    const auto& data = profiler->get_per_frame_data_read();

    if(data.empty())
    {
        ImGui::TextDisabled("No profiler scopes recorded yet.");
        return;
    }

    ImGui::TextWrapped(
        "Each row is one scope name: wall ms summed over all matching spans per frame (same label merges across threads). "
        "Trend uses one Y scale for all rows (max of per-row history max).");
    ImGui::Spacing();

    static int sort_mode = 0;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Sort by");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    static constexpr std::array<const char*, 4> sort_labels = {
        "Average (hot first)", "Peak (max ms)", "This frame (partial)", "Name (A-Z)"};
    ImGui::Combo("##agg_sort", &sort_mode, sort_labels.data(), static_cast<int>(sort_labels.size()));

    using record_entry = performance_profiler::record_data_t::value_type;
    using entry_cptr = const record_entry*;
    std::vector<entry_cptr> rows;
    rows.reserve(data.size());
    for(const auto& e : data)
    {
        rows.push_back(&e);
    }

    const auto cmp_rows = [](entry_cptr a, entry_cptr b) -> bool
    {
        switch(sort_mode)
        {
        case 0:
            if(a->second.get_avg() != b->second.get_avg())
            {
                return a->second.get_avg() > b->second.get_avg();
            }
            break;
        case 1:
            if(a->second.get_max() != b->second.get_max())
            {
                return a->second.get_max() > b->second.get_max();
            }
            break;
        case 2:
            if(a->second.get_time_since_swap() != b->second.get_time_since_swap())
            {
                return a->second.get_time_since_swap() > b->second.get_time_since_swap();
            }
            break;
        default:
            break;
        }
        return a->first < b->first;
    };
    std::sort(rows.begin(), rows.end(), cmp_rows);

    float scale_avg = 0.0001f;
    float scale_hist_max = 0.0001f;
    for(entry_cptr ep : rows)
    {
        scale_avg = std::max(scale_avg, ep->second.get_avg());
        scale_hist_max = std::max(scale_hist_max, ep->second.get_max());
    }
    const float spark_ymax = scale_hist_max * 1.05f;

    constexpr float table_max_h = 320.0f;
    const float avail_h = ImGui::GetContentRegionAvail().y;
    const float table_h = std::max(120.0f, std::min(table_max_h, std::max(160.0f, avail_h)));

    ImGui::PushFont(ImGui::Font::Mono);
    constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH |
                                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                              ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
                                              ImGuiTableFlags_Reorderable;
    if(ImGui::BeginTable("##aggregate_scopes", 7, table_flags, ImVec2(-1.0f, table_h)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableSetupColumn("Trend", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, 88.0f);
        ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableHeadersRow();

        const float bar_h = ImGui::GetTextLineHeight();

        for(entry_cptr ep : rows)
        {
            const std::string& name = ep->first;
            const performance_profiler::per_frame_data& pfd = ep->second;

            ImGui::TableNextRow();
            ImGui::PushID(name.c_str());

            ImGui::TableNextColumn();
            const float bar_frac = scale_avg > 0.0f ? std::clamp(pfd.get_avg() / scale_avg, 0.0f, 1.0f) : 0.0f;
            ImGui::ProgressBar(bar_frac, ImVec2(-1.0f, bar_h), "");
            ImGui::SetItemTooltipEx("Average ms vs largest average in this table (%.3f ms).", scale_avg);
            

            ImGui::TableNextColumn();
            const sample_data& hist = pfd.get_history();
            ImGui::PlotLines("##spark",
                             hist.get_values(),
                             static_cast<int>(sample_data::num_samples),
                             hist.get_offset(),
                             nullptr,
                             0.0f,
                             spark_ymax,
                             ImVec2(-1.0f, 36.0f));
            ImGui::SetItemTooltipEx(
                    "Last %u frames: total ms per frame for this name (oldest left, newest right). Y max = %.3f ms.",
                    sample_data::num_samples,
                    spark_ymax);
            

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());
            ImGui::SetItemTooltipEx("%s", name.c_str());
            

            ImGui::TableNextColumn();
            ImGui::Text("%.3f\n%u", pfd.get_time_since_swap(), static_cast<unsigned>(pfd.get_samples_since_swap()));
            ImGui::SetItemTooltipEx("In-progress frame: summed ms so far and number of ended spans.");

            ImGui::TableNextColumn();
            ImGui::Text("%.3f", pfd.get_avg());

            ImGui::TableNextColumn();
            ImGui::Text("%.3f", pfd.get_max());

            ImGui::TableNextColumn();
            ImGui::Text("%.3f", pfd.get_min());

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopFont();
}

} // namespace unravel
