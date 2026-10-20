#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

namespace unravel
{

/**
 * @brief Logs the GPU time of every render pass, averaged over a few seconds of frames.
 *
 * The per-pass breakdown for a device without the editor's profiler panel. It reads bgfx's per-view
 * statistics, so the BGFX_DEBUG_PROFILER flag must be on. Views of the same name add up. On a
 * tile-based GPU the timestamps are taken at render pass boundaries, and the profiler splits passes
 * bgfx would otherwise merge, so the numbers rank the passes rather than add up to the frame exactly.
 */
class gpu_pass_log
{
public:
    /// Adds the last frame's view statistics; logs the averages and starts again every log interval.
    void collect();

private:
    void report(double window_seconds);

    /// The summed GPU milliseconds of one pass name over the window.
    std::map<std::string, double> pass_gpu_ms_;
    double frame_gpu_ms_{};
    uint32_t frames_{};
    std::chrono::steady_clock::time_point window_start_{};
};

} // namespace unravel
