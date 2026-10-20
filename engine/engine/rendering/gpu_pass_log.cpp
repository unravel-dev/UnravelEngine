#include "gpu_pass_log.h"

#include <logging/logging.h>

#include <bgfx/bgfx.h>

#include <algorithm>
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
        pass_gpu_ms_[view.name] += static_cast<double>(view.gpuTimeEnd - view.gpuTimeBegin) * gpu_to_ms;
    }
    frame_gpu_ms_ += static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * gpu_to_ms;
    ++frames_;
    const double window_seconds = std::chrono::duration<double>(now - window_start_).count();
    if(window_seconds >= LOG_INTERVAL_SECONDS)
    {
        report(window_seconds);
        pass_gpu_ms_.clear();
        frame_gpu_ms_ = 0.0;
        frames_ = 0;
        window_start_ = now;
    }
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
    for(const auto& [name, average_ms] : passes)
    {
        if(average_ms >= MIN_LOGGED_PASS_MS)
        {
            APPLOG_INFO("GPU pass {:7.2f} ms  {}", average_ms, name);
        }
    }
}

} // namespace unravel
