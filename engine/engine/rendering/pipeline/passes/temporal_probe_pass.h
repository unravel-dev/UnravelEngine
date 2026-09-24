#pragma once

#include <engine/rendering/gpu_program.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/render_view.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace unravel
{

class camera;

/**
 * @brief Measures the temporal stability of the image a pipeline displays.
 *
 * An instrument for GI and anti-aliasing work (tasks/lumen57_deep_dive_2026-09-14.md, plan item
 * 1.0). A tool arms it for a number of frames; each armed frame one compute dispatch folds the
 * displayed luminance into per-pixel statistics - a running mean and sum of squared deviations
 * (the variance a still camera sees) and the absolute change against the previous frame
 * reprojected through the velocity buffer (flicker that survives camera motion). After the
 * last frame one readback lands the sums and the CPU reduces them to percentiles. Screen
 * captures taken through the editor tooling are ~170 ms apart and cannot see per-frame flicker.
 *
 * Nothing is dispatched unless a measurement is armed, so a normal frame pays nothing.
 */
class temporal_probe_pass
{
public:
    /// Columns and rows of the screen grid the per-cell means are reported on.
    static constexpr uint32_t grid_size = 8u;

    /// Statistics over the probed pixels, in 8-bit display levels (0-255).
    struct result
    {
        bool valid = false;
        uint32_t frames = 0;
        /// True when the statistics ran on the low-pass lane (see request).
        bool lowpass = false;
        uint32_t width = 0;
        uint32_t height = 0;
        /// Pixels whose reprojected change was measured on at least half of the frames.
        uint32_t delta_pixels = 0;
        /// Per-pixel luminance standard deviation over the frames: 50th / 95th / 99th percentile.
        std::array<float, 3> std_percentiles{};
        /// Share of pixels whose standard deviation exceeds 1.5 and 4 levels.
        std::array<float, 2> std_shares{};
        /// Per-pixel mean absolute reprojected change: 50th / 95th / 99th percentile.
        std::array<float, 3> delta_percentiles{};
        /// Share of measured pixels whose mean change exceeds 1 and 4 levels.
        std::array<float, 2> delta_shares{};
        /// Mean of the per-pixel mean change over the measured pixels.
        float delta_mean = 0.0f;
        /// Mean standard deviation per grid cell, row-major from the top of the image.
        std::vector<float> std_grid;
        /// Mean change per grid cell over its measured pixels, same layout.
        std::vector<float> delta_grid;
    };

    struct run_params
    {
        /// The displayed image: the pipeline output's colour attachment, display-encoded.
        gfx::texture::ptr color;
        /// This frame's velocity buffer (RG = total uv delta, BA = object part); null = no reprojection.
        gfx::texture::ptr velocity;
        /// This frame's device depth.
        gfx::texture::ptr depth;
        /// Last frame's device depth; null disables the disocclusion test.
        gfx::texture::ptr prev_depth;
        const camera* cam = nullptr;
    };

    auto init(rtti::context& ctx) -> bool;

    /// Arms a measurement of @p frames frames starting with the next run; a running one restarts. @p is_lowpass
    /// runs every statistic on a small box mean of the displayed luminance (cs_temporal_probe.sc,
    /// PROBE_LOWPASS_RADIUS): under camera motion the raw reprojected change is dominated by the sub-pixel
    /// resampling of textured detail, which the box removes while patch-scale flicker stays.
    void request(uint32_t frames, bool is_lowpass = false);

    /// Frames folded in so far by the current or last measurement.
    auto get_frames_done() const -> uint32_t;

    /// Frames the current or last measurement was armed for.
    auto get_frames_requested() const -> uint32_t;

    /// True while a measurement accumulates or its readback is in flight.
    auto is_busy() const -> bool;

    /// The last completed measurement; valid is false until one lands after a request.
    auto get_result() const -> const result&;

    /// Folds this frame in when armed and services the readback. Call once per camera frame,
    /// after the image is final.
    void run(const run_params& params);

private:
    void allocate(uint16_t width, uint16_t height);
    void dispatch_frame(const run_params& params);
    void issue_readback();
    void reduce_readback();

    struct probe_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_color;
        gfx::program::uniform_ptr s_velocity;
        gfx::program::uniform_ptr s_depth;
        gfx::program::uniform_ptr s_prev_depth;
        gfx::program::uniform_ptr s_prev_luma;
        gfx::program::uniform_ptr u_probe_params;
        gfx::program::uniform_ptr u_probe_params2;

        void cache_uniforms()
        {
            cache_uniform(program.get(), s_color, "s_color", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_velocity, "s_velocity", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_prev_depth, "s_prev_depth", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_prev_luma, "s_prev_luma", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_probe_params, "u_probe_params", bgfx::UniformType::Vec4);
            cache_uniform(program.get(), u_probe_params2, "u_probe_params2", bgfx::UniformType::Vec4);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    };

    probe_program program_;
    /// RGBA32F: running mean, sum of squared deviations, sum of change, frames the change was measured on.
    gfx::texture::ptr sums_;
    /// This frame's and last frame's displayed luminance, alternating.
    std::array<gfx::texture::ptr, 2> luma_{};
    gfx::texture::ptr readback_;
    std::vector<float> readback_data_;
    result result_{};
    uint32_t frames_requested_ = 0;
    uint32_t frames_done_ = 0;
    uint32_t luma_write_ = 0;
    uint32_t readback_ready_frame_ = 0;
    uint32_t readback_frames_ = 0;
    uint16_t width_ = 0;
    uint16_t height_ = 0;
    bool armed_ = false;
    bool readback_pending_ = false;
    /// Set by a request made while a readback was in flight: that readback is discarded.
    bool discard_pending_readback_ = false;
    /// The lane the current measurement runs on, and the lane the in-flight readback was measured on.
    bool is_lowpass_ = false;
    bool readback_lowpass_ = false;
};

} // namespace unravel
