#include "temporal_probe_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/camera.h>

#include <algorithm>
#include <cmath>

namespace unravel
{
namespace
{
/// The longest measurement a tool may arm, in frames.
constexpr uint32_t max_frames = 4096u;
/// A pixel's reprojected change is reported only when it was measured on at least this share of
/// the frame pairs; the rest were disoccluded, on object motion or on a depth edge too often.
constexpr float min_change_coverage = 0.5f;
/// Share thresholds, in 8-bit display levels.
constexpr float std_share_low = 1.5f;
constexpr float std_share_high = 4.0f;
constexpr float change_share_low = 1.0f;
constexpr float change_share_high = 4.0f;
constexpr uint32_t compute_group = 8u;

/// Value at quantile @p q of @p values (reorders them).
auto quantile(std::vector<float>& values, float q) -> float
{
    if(values.empty())
    {
        return 0.0f;
    }
    const auto index = static_cast<size_t>(q * float(values.size() - 1) + 0.5f);
    std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(index), values.end());
    return values[index];
}

/// Share of @p values strictly above @p threshold.
auto share_above(const std::vector<float>& values, float threshold) -> float
{
    if(values.empty())
    {
        return 0.0f;
    }
    const auto count = std::count_if(values.begin(),
                                     values.end(),
                                     [threshold](float value)
                                     {
                                         return value > threshold;
                                     });
    return float(count) / float(values.size());
}
} // namespace

auto temporal_probe_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs = am.get_asset<gfx::shader>("engine:/data/shaders/taa/cs_temporal_probe.sc");
    program_.cache_uniforms();
    program_.program = std::make_unique<gpu_program>(cs);
    return program_.is_valid();
}

void temporal_probe_pass::request(uint32_t frames, bool is_lowpass)
{
    frames_requested_ = std::clamp(frames, 2u, max_frames);
    is_lowpass_ = is_lowpass;
    frames_done_ = 0;
    armed_ = true;
    result_ = {};
    discard_pending_readback_ = readback_pending_;
}

auto temporal_probe_pass::get_frames_done() const -> uint32_t
{
    return frames_done_;
}

auto temporal_probe_pass::get_frames_requested() const -> uint32_t
{
    return frames_requested_;
}

auto temporal_probe_pass::is_busy() const -> bool
{
    return armed_ || readback_pending_;
}

auto temporal_probe_pass::get_result() const -> const result&
{
    return result_;
}

void temporal_probe_pass::run(const run_params& params)
{
    // A landed readback first: bgfx wrote it on the render thread once its frame retired.
    if(readback_pending_ && gfx::get_render_frame() >= readback_ready_frame_)
    {
        readback_pending_ = false;
        if(!discard_pending_readback_)
        {
            reduce_readback();
        }
        discard_pending_readback_ = false;
    }
    if(!armed_ || readback_pending_ || !program_.is_valid() || !params.color || !params.depth ||
       params.cam == nullptr)
    {
        return;
    }
    const auto width = static_cast<uint16_t>(params.color->info.width);
    const auto height = static_cast<uint16_t>(params.color->info.height);
    if(width == 0 || height == 0)
    {
        return;
    }
    if(!sums_ || width != width_ || height != height_)
    {
        // A resize mid-measurement restarts it: the sums describe other pixels.
        allocate(width, height);
        frames_done_ = 0;
    }
    APP_SCOPE_PERF("Instruments/Temporal Probe");
    dispatch_frame(params);
    if(frames_done_ >= frames_requested_)
    {
        armed_ = false;
        issue_readback();
    }
}

void temporal_probe_pass::allocate(uint16_t width, uint16_t height)
{
    width_ = width;
    height_ = height;
    sums_ = std::make_shared<gfx::texture>(width,
                                           height,
                                           false,
                                           1,
                                           gfx::texture_format::RGBA32F,
                                           BGFX_TEXTURE_COMPUTE_WRITE);
    for(auto& luma : luma_)
    {
        luma = std::make_shared<gfx::texture>(width,
                                              height,
                                              false,
                                              1,
                                              gfx::texture_format::R32F,
                                              BGFX_TEXTURE_COMPUTE_WRITE);
    }
    readback_ = std::make_shared<gfx::texture>(width,
                                               height,
                                               false,
                                               1,
                                               gfx::texture_format::RGBA32F,
                                               BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    readback_data_.assign(size_t(width) * size_t(height) * 4u, 0.0f);
}

void temporal_probe_pass::dispatch_frame(const run_params& params)
{
    constexpr uint64_t point_clamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
    constexpr uint64_t linear_clamp = BGFX_SAMPLER_UVW_CLAMP;
    const bool has_previous = frames_done_ > 0;
    const uint32_t luma_read = 1u - luma_write_;
    gfx::render_pass pass("Instruments/Temporal Probe");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    program_.program->begin();
    gfx::set_texture(program_.s_color, 0, params.color, point_clamp);
    // Unbound inputs are substituted with this frame's depth and switched off by the lanes below.
    gfx::set_texture(program_.s_velocity, 1, params.velocity ? params.velocity : params.depth, point_clamp);
    gfx::set_texture(program_.s_depth, 2, params.depth, point_clamp);
    gfx::set_texture(program_.s_prev_depth, 3, params.prev_depth ? params.prev_depth : params.depth, point_clamp);
    gfx::set_texture(program_.s_prev_luma, 4, luma_[luma_read], linear_clamp);
    gfx::set_image(5, sums_->native_handle(), 0, gfx::access::ReadWrite, gfx::texture_format::RGBA32F);
    gfx::set_image(6, luma_[luma_write_]->native_handle(), 0, gfx::access::Write, gfx::texture_format::R32F);
    const float probe_params[4] = {float(width_),
                                   float(height_),
                                   params.velocity ? 1.0f : 0.0f,
                                   has_previous ? 1.0f : 0.0f};
    gfx::set_uniform(program_.u_probe_params, probe_params);
    const float probe_params2[4] = {float(frames_done_ + 1u),
                                    params.prev_depth ? 1.0f : 0.0f,
                                    is_lowpass_ ? 1.0f : 0.0f,
                                    0.0f};
    gfx::set_uniform(program_.u_probe_params2, probe_params2);
    gfx::dispatch(pass.id,
                  program_.program->native_handle(),
                  (uint32_t(width_) + compute_group - 1u) / compute_group,
                  (uint32_t(height_) + compute_group - 1u) / compute_group,
                  1);
    program_.program->end();
    luma_write_ = luma_read;
    ++frames_done_;
}

void temporal_probe_pass::issue_readback()
{
    gfx::render_pass pass("Instruments/Temporal Probe Readback");
    gfx::blit(pass.id, readback_->native_handle(), 0, 0, sums_->native_handle(), 0, 0, width_, height_);
    readback_ready_frame_ = gfx::read_texture(readback_->native_handle(), readback_data_.data());
    readback_frames_ = frames_done_;
    readback_lowpass_ = is_lowpass_;
    readback_pending_ = true;
}

void temporal_probe_pass::reduce_readback()
{
    const uint32_t width = width_;
    const uint32_t height = height_;
    const uint32_t frames = std::max(readback_frames_, 1u);
    const float min_measured = min_change_coverage * float(frames - 1u);
    std::vector<float> deviations;
    std::vector<float> changes;
    deviations.reserve(size_t(width) * height);
    changes.reserve(size_t(width) * height);
    std::vector<double> std_cells(size_t(grid_size) * grid_size, 0.0);
    std::vector<double> change_cells(size_t(grid_size) * grid_size, 0.0);
    std::vector<uint32_t> std_counts(size_t(grid_size) * grid_size, 0u);
    std::vector<uint32_t> change_counts(size_t(grid_size) * grid_size, 0u);
    double change_sum = 0.0;
    for(uint32_t y = 0; y < height; ++y)
    {
        const uint32_t cell_y = std::min(y * grid_size / height, grid_size - 1u);
        for(uint32_t x = 0; x < width; ++x)
        {
            const uint32_t cell = cell_y * grid_size + std::min(x * grid_size / width, grid_size - 1u);
            const float* texel = &readback_data_[(size_t(y) * width + x) * 4u];
            const float deviation = std::sqrt(std::max(texel[1] / float(frames), 0.0f));
            deviations.push_back(deviation);
            std_cells[cell] += deviation;
            ++std_counts[cell];
            if(frames > 1u && texel[3] >= std::max(min_measured, 1.0f))
            {
                const float change = texel[2] / texel[3];
                changes.push_back(change);
                change_cells[cell] += change;
                ++change_counts[cell];
                change_sum += change;
            }
        }
    }
    result next;
    next.valid = true;
    next.frames = frames;
    next.width = width;
    next.height = height;
    next.lowpass = readback_lowpass_;
    next.delta_pixels = static_cast<uint32_t>(changes.size());
    next.delta_mean = changes.empty() ? 0.0f : float(change_sum / double(changes.size()));
    next.std_shares = {share_above(deviations, std_share_low), share_above(deviations, std_share_high)};
    next.delta_shares = {share_above(changes, change_share_low), share_above(changes, change_share_high)};
    next.std_percentiles = {quantile(deviations, 0.5f), quantile(deviations, 0.95f), quantile(deviations, 0.99f)};
    next.delta_percentiles = {quantile(changes, 0.5f), quantile(changes, 0.95f), quantile(changes, 0.99f)};
    next.std_grid.resize(std_cells.size());
    next.delta_grid.resize(change_cells.size());
    for(size_t cell = 0; cell < std_cells.size(); ++cell)
    {
        next.std_grid[cell] = std_counts[cell] ? float(std_cells[cell] / std_counts[cell]) : 0.0f;
        next.delta_grid[cell] = change_counts[cell] ? float(change_cells[cell] / change_counts[cell]) : 0.0f;
    }
    result_ = std::move(next);
}

} // namespace unravel
