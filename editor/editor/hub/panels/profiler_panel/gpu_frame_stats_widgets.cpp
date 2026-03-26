#include "gpu_frame_stats_widgets.h"

#include "profiler_statistics_utils.h"
#include <bx/bx.h>
#include <imgui/imgui.h>

#include <bgfx/bgfx.h>

namespace unravel
{
namespace
{
constexpr float profiler_scale = 3.0f;
constexpr float profiler_max_width = 30.0f;
constexpr ImVec4 cpu_color{0.2f, 0.8f, 0.2f, 1.0f};
constexpr ImVec4 gpu_color{0.2f, 0.6f, 1.0f, 1.0f};
constexpr ImVec4 warning_color{1.0f, 0.7f, 0.0f, 1.0f};

void draw_encoder_stats(const gfx::stats* stats, float item_height, float item_height_with_spacing, double to_cpu_ms)
{
    if(ImGui::BeginListBox("Encoders##GpuBgfxProfiler",
                           ImVec2(ImGui::GetWindowWidth(), stats->numEncoders * item_height_with_spacing)))
    {
        ImGuiListClipper clipper;
        clipper.Begin(stats->numEncoders, item_height);

        while(clipper.Step())
        {
            for(int32_t pos = clipper.DisplayStart; pos < clipper.DisplayEnd; ++pos)
            {
                const bgfx::EncoderStats& encoder_stats = stats->encoderStats[pos];
                ImGui::PushID(pos);
                ImGui::Text("%3d", pos);
                ImGui::SameLine(64.0f);

                const float max_width = profiler_max_width * profiler_scale;
                const float cpu_ms =
                    static_cast<float>((encoder_stats.cpuTimeEnd - encoder_stats.cpuTimeBegin) * to_cpu_ms);
                const float cpu_width = bx::clamp(cpu_ms * profiler_scale, 1.0f, max_width);

                if(profiler_statistics_utils::draw_progress_bar(cpu_width, max_width, item_height, cpu_color))
                {
                    ImGui::SetItemTooltipEx(
                        "Encoder %d\nCPU submit (render thread): %.3f ms",
                        pos,
                        cpu_ms);
                }

                ImGui::PopID();
            }
        }
        ImGui::EndListBox();
    }
}

void draw_view_stats(const gfx::stats* stats,
                     float item_height,
                     float item_height_with_spacing,
                     double to_cpu_ms,
                     double to_gpu_ms)
{
    constexpr int lines_per_view = 3;
    if(ImGui::BeginListBox(
           "Views##GpuBgfxProfiler",
           ImVec2(ImGui::GetWindowWidth(), stats->numViews * lines_per_view * item_height_with_spacing)))
    {
        const float max_width = profiler_max_width * profiler_scale;

        for(uint16_t pos = 0; pos < stats->numViews; ++pos)
        {
            const bgfx::ViewStats& view_stats = stats->viewStats[pos];
            const float cpu_time_elapsed =
                static_cast<float>((view_stats.cpuTimeEnd - view_stats.cpuTimeBegin) * to_cpu_ms);
            const float gpu_time_elapsed =
                static_cast<float>((view_stats.gpuTimeEnd - view_stats.gpuTimeBegin) * to_gpu_ms);
            const float cpu_width = bx::clamp(cpu_time_elapsed * profiler_scale, 1.0f, max_width);
            const float gpu_width = bx::clamp(gpu_time_elapsed * profiler_scale, 1.0f, max_width);

            ImGui::PushID(pos);

            ImGui::Text("%3d.", view_stats.view);

            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::Text("%s", view_stats.name);

            ImGui::Text("CPU submit: %.3f ms", cpu_time_elapsed);
            ImGui::SameLine();
            ImGui::PushID("cpu");
            if(profiler_statistics_utils::draw_progress_bar(cpu_width, max_width, item_height, cpu_color))
            {
                ImGui::SetItemTooltipEx("CPU submit (render thread): %.3f ms", cpu_time_elapsed);
            }
            ImGui::PopID();
            ImGui::Text("GPU execute: %.3f ms", gpu_time_elapsed);
            ImGui::SameLine();
            ImGui::PushID("gpu");
            if(profiler_statistics_utils::draw_progress_bar(gpu_width, max_width, item_height, gpu_color))
            {
                ImGui::SetItemTooltipEx("GPU execute: %.3f ms", gpu_time_elapsed);
            }
            ImGui::PopID();

            ImGui::EndGroup();

            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
}

} // namespace

void draw_gpu_bgfx_submit_profiler_ui(const gfx::stats* stats, bool* enable_profiler)
{
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Bgfx view/encoder timing:");
    ImGui::SameLine();
    if(ImGui::Checkbox("Enable##GpuBgfxProfiler", enable_profiler))
    {
        gfx::set_debug(*enable_profiler ? BGFX_DEBUG_PROFILER : BGFX_DEBUG_NONE);
    }

    if(enable_profiler == nullptr)
    {
        return;
    }
    if(!*enable_profiler)
    {
        ImGui::TextColored(warning_color, "Enable to record per-view CPU submit and GPU execute times.");
        return;
    }

    if(!stats)
    {
        ImGui::TextColored(warning_color, "No stats.");
        return;
    }

    if(stats->numViews == 0)
    {
        ImGui::TextColored(warning_color, "No GPU profiling data yet (initializing or no views).");
        return;
    }

    const float item_height = ImGui::GetTextLineHeightWithSpacing();
    const float item_height_with_spacing = ImGui::GetFrameHeightWithSpacing();
    const double to_cpu_ms = 1000.0 / static_cast<double>(stats->cpuTimerFreq);
    const double to_gpu_ms = 1000.0 / static_cast<double>(stats->gpuTimerFreq);

    draw_encoder_stats(stats, item_height, item_height_with_spacing, to_cpu_ms);

    ImGui::Separator();

    draw_view_stats(stats, item_height, item_height_with_spacing, to_cpu_ms, to_gpu_ms);
}

} // namespace unravel
