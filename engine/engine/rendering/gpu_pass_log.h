#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

namespace bgfx
{
struct Stats;
} // namespace bgfx

namespace unravel
{

/**
 * @brief Logs the GPU time of every render pass, averaged over a few seconds of frames.
 *
 * The per-pass breakdown for a device without the editor's profiler panel. It reads bgfx's per-view
 * statistics, which need the BGFX_DEBUG_PROFILER flag; without it the log holds the frame times and
 * pacing alone. Views of the same name add up. On a tile-based GPU the timestamps are taken at render
 * pass boundaries, and the profiler splits passes bgfx would otherwise merge, so the numbers rank the
 * passes rather than add up to the frame exactly, and the frame itself runs slower than without them.
 */
class gpu_pass_log
{
public:
    /// Adds the last frame's view statistics; logs the averages and starts again every log interval.
    void collect();

private:
    /// Adds the CPU-side pacing of the last frame and the GPU's idle time since the frame before.
    void collect_pacing(const bgfx::Stats& stats, double gpu_to_ms);
    void report(double window_seconds);

    /// The summed GPU milliseconds of one pass name over the window.
    std::map<std::string, double> pass_gpu_ms_;
    double frame_gpu_ms_{};
    /// Window sums of bgfx's CPU pacing, ms: the time between bgfx::frame calls, the main thread's wait
    /// for the render thread, the render thread's wait for the next frame, and its submission.
    double cpu_frame_ms_{};
    double wait_render_ms_{};
    double wait_submit_ms_{};
    double render_submit_ms_{};
    /// GPU time between the end of one frame and the start of the next, over consecutive GPU frames.
    double gpu_idle_ms_{};
    uint32_t gpu_idle_frames_{};
    uint32_t last_gpu_frame_num_{};
    int64_t last_gpu_time_end_{};
    uint32_t frames_{};
    std::chrono::steady_clock::time_point window_start_{};
};

} // namespace unravel
