#include "profiler_submit_latency.h"

#include <graphics/graphics.h>
#include <imgui/imgui.h>

namespace unravel
{

void profiler_draw_submit_latency_row(const gfx::stats* stats)
{
    if(stats == nullptr)
    {
        return;
    }
    const double to_cpu_ms = 1000.0 / static_cast<double>(stats->cpuTimerFreq);
    const double to_gpu_ms = 1000.0 / static_cast<double>(stats->gpuTimerFreq);
    const double submit_cpu_ms = static_cast<double>(stats->cpuTimeEnd - stats->cpuTimeBegin) * to_cpu_ms;
    const double submit_gpu_ms = static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * to_gpu_ms;
    ImGui::Text("Submit: CPU (render thread) %.3f ms  |  GPU %.3f ms  |  Latency %d frames",
                submit_cpu_ms,
                submit_gpu_ms,
                static_cast<int>(stats->maxGpuLatency));
}

} // namespace unravel
