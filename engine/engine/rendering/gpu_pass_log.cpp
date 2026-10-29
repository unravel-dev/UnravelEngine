#include "gpu_pass_log.h"

#include <logging/logging.h>

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

namespace unravel
{
namespace
{
/// Seconds of frames each log averages over.
constexpr double LOG_INTERVAL_SECONDS = 3.0;
/// Passes below this average are left out of the log.
constexpr double MIN_LOGGED_PASS_MS = 0.05;
constexpr double MS_PER_SECOND = 1000.0;
/// The scope the UI's layer stacks name their views with, followed by a layer id that grows every frame.
constexpr std::string_view LAYER_SCOPE = "Layer ";

/// A view's name with every UI layer id replaced by N: the ids differ from frame to frame, so without this one UI
/// pass would spread over a new name each frame and fall under MIN_LOGGED_PASS_MS.
auto collapse_layer_ids(std::string_view name) -> std::string
{
    std::string collapsed;
    collapsed.reserve(name.size());
    size_t position = 0;
    while(position < name.size())
    {
        const size_t scope = name.find(LAYER_SCOPE, position);
        if(scope == std::string_view::npos)
        {
            collapsed.append(name.substr(position));
            break;
        }
        const size_t id_begin = scope + LAYER_SCOPE.size();
        size_t id_end = id_begin;
        while(id_end < name.size() && std::isdigit(static_cast<unsigned char>(name[id_end])) != 0)
        {
            ++id_end;
        }
        collapsed.append(name.substr(position, id_begin - position));
        if(id_end > id_begin)
        {
            collapsed.push_back('N');
        }
        position = id_end;
    }
    return collapsed;
}
} // namespace

void gpu_pass_log::collect()
{
    const auto now = std::chrono::steady_clock::now();
    if(frames_ == 0 && pass_gpu_ms_.empty())
    {
        window_start_ = now;
    }
    const bgfx::Stats* stats = bgfx::getStats();
    if(stats == nullptr || stats->gpuTimerFreq <= 0)
    {
        return;
    }
    const double gpu_to_ms = MS_PER_SECOND / static_cast<double>(stats->gpuTimerFreq);
    for(uint16_t i = 0; i < stats->numViews; ++i)
    {
        const bgfx::ViewStats& view = stats->viewStats[i];
        pass_gpu_ms_[collapse_layer_ids(view.name)] += static_cast<double>(view.gpuTimeEnd - view.gpuTimeBegin) * gpu_to_ms;
    }
    frame_gpu_ms_ += static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * gpu_to_ms;
    collect_pacing(*stats, gpu_to_ms);
    ++frames_;
    const double window_seconds = std::chrono::duration<double>(now - window_start_).count();
    if(window_seconds >= LOG_INTERVAL_SECONDS)
    {
        report(window_seconds);
        pass_gpu_ms_.clear();
        frame_gpu_ms_ = 0.0;
        cpu_frame_ms_ = 0.0;
        wait_render_ms_ = 0.0;
        wait_submit_ms_ = 0.0;
        render_submit_ms_ = 0.0;
        gpu_idle_ms_ = 0.0;
        gpu_idle_frames_ = 0;
        frames_ = 0;
        window_start_ = now;
    }
}

void gpu_pass_log::collect_pacing(const bgfx::Stats& stats, double gpu_to_ms)
{
    if(stats.cpuTimerFreq > 0)
    {
        const double cpu_to_ms = MS_PER_SECOND / static_cast<double>(stats.cpuTimerFreq);
        cpu_frame_ms_ += static_cast<double>(stats.cpuTimeFrame) * cpu_to_ms;
        wait_render_ms_ += static_cast<double>(stats.waitRender) * cpu_to_ms;
        wait_submit_ms_ += static_cast<double>(stats.waitSubmit) * cpu_to_ms;
        render_submit_ms_ += static_cast<double>(stats.cpuTimeEnd - stats.cpuTimeBegin) * cpu_to_ms;
    }
    if(stats.gpuFrameNum == last_gpu_frame_num_)
    {
        return;
    }
    if(stats.gpuFrameNum == last_gpu_frame_num_ + 1 && last_gpu_time_end_ != 0 && stats.gpuTimeBegin > last_gpu_time_end_)
    {
        gpu_idle_ms_ += static_cast<double>(stats.gpuTimeBegin - last_gpu_time_end_) * gpu_to_ms;
        ++gpu_idle_frames_;
    }
    last_gpu_frame_num_ = stats.gpuFrameNum;
    last_gpu_time_end_ = stats.gpuTimeEnd;
}

void gpu_pass_log::report(double window_seconds)
{
    if(frames_ == 0)
    {
        return;
    }
    std::vector<std::pair<std::string, double>> passes;
    passes.reserve(pass_gpu_ms_.size());
    double passes_ms = 0.0;
    for(const auto& [name, gpu_ms] : pass_gpu_ms_)
    {
        const double average_ms = gpu_ms / frames_;
        passes_ms += average_ms;
        passes.emplace_back(name, average_ms);
    }
    std::sort(passes.begin(),
              passes.end(),
              [](const auto& lhs, const auto& rhs)
              {
                  return lhs.second > rhs.second;
              });
    APPLOG_INFO("GPU passes: {} frames, {:.2f} ms per frame, GPU frame {:.2f} ms, passes {:.2f} ms",
                frames_,
                window_seconds * MS_PER_SECOND / frames_,
                frame_gpu_ms_ / frames_,
                passes_ms);
    APPLOG_INFO("Frame pacing: CPU frame {:.2f} ms, render thread submit {:.2f} ms, main thread waits for it {:.2f} ms, "
                "render thread waits for the next frame {:.2f} ms, GPU idle between frames {:.2f} ms",
                cpu_frame_ms_ / frames_,
                render_submit_ms_ / frames_,
                wait_render_ms_ / frames_,
                wait_submit_ms_ / frames_,
                gpu_idle_frames_ > 0 ? gpu_idle_ms_ / gpu_idle_frames_ : 0.0);
    for(const auto& [name, average_ms] : passes)
    {
        if(average_ms >= MIN_LOGGED_PASS_MS)
        {
            APPLOG_INFO("GPU pass {:7.2f} ms  {}", average_ms, name);
        }
    }
}

} // namespace unravel
