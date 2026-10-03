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

void temporal_probe_pass::request(uint32_t frames, bool is_lowpass, bool keeps_images, std::vector<uint32_t> marks)
{
    frames_requested_ = std::clamp(frames, 2u, max_frames);
    is_lowpass_ = is_lowpass;
    keeps_images_ = keeps_images;
    std::sort(marks.begin(), marks.end());
    marks.erase(std::unique(marks.begin(), marks.end()), marks.end());
    marks.erase(std::remove_if(marks.begin(),
                               marks.end(),
                               [this](uint32_t mark)
                               {
                                   return mark == 0u || mark > frames_requested_;
                               }),
                marks.end());
    if(marks.size() > max_marks)
    {
        marks.resize(max_marks);
    }
    marks_requested_ = std::move(marks);
    marks_pending_.clear();
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
    if(std::binary_search(marks_requested_.begin(), marks_requested_.end(), frames_done_))
    {
        capture_mark(params);
    }
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
                                           bgfx::TextureFormat::RGBA32F,
                                           BGFX_TEXTURE_COMPUTE_WRITE);
    for(auto& luma : luma_)
    {
        luma = std::make_shared<gfx::texture>(width,
                                              height,
                                              false,
                                              1,
                                              bgfx::TextureFormat::R32F,
                                              BGFX_TEXTURE_COMPUTE_WRITE);
    }
    for(auto& age : age_)
    {
        age = std::make_shared<gfx::texture>(width,
                                             height,
                                             false,
                                             1,
                                             bgfx::TextureFormat::R32F,
                                             BGFX_TEXTURE_COMPUTE_WRITE);
    }
    readback_ = std::make_shared<gfx::texture>(width,
                                               height,
                                               false,
                                               1,
                                               bgfx::TextureFormat::RGBA32F,
                                               BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    readback_data_.assign(size_t(width) * size_t(height) * 4u, 0.0f);
    readback_luma_ = std::make_shared<gfx::texture>(width,
                                                    height,
                                                    false,
                                                    1,
                                                    bgfx::TextureFormat::R32F,
                                                    BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    readback_luma_data_.assign(size_t(width) * size_t(height), 0.0f);
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
    bgfx::setImage(5, sums_->native_handle(), 0, bgfx::Access::ReadWrite, bgfx::TextureFormat::RGBA32F);
    bgfx::setImage(6, luma_[luma_write_]->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    bgfx::setImage(7, age_[luma_write_]->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    gfx::set_texture(program_.s_prev_age, 8, age_[luma_read], point_clamp);
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
    bgfx::dispatch(pass.id,
                   program_.program->native_handle(),
                   (uint32_t(width_) + compute_group - 1u) / compute_group,
                   (uint32_t(height_) + compute_group - 1u) / compute_group,
                   1);
    program_.program->end();
    luma_write_ = luma_read;
    ++frames_done_;
}

void temporal_probe_pass::capture_mark(const run_params& params)
{
    pending_mark mark;
    mark.frame = frames_done_;
    mark.format = bgfx::TextureFormat::Enum(params.color->info.format);
    mark.color = std::make_shared<gfx::texture>(width_,
                                                height_,
                                                false,
                                                1,
                                                mark.format,
                                                BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    mark.age = std::make_shared<gfx::texture>(width_,
                                              height_,
                                              false,
                                              1,
                                              bgfx::TextureFormat::R32F,
                                              BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    const size_t pixels = size_t(width_) * height_;
    mark.color_data.assign(pixels * (params.color->info.bitsPerPixel / 8u), 0u);
    mark.age_data.assign(pixels, 0.0f);
    gfx::render_pass pass("Instruments/Temporal Probe Mark");
    bgfx::blit(pass.id,
               bgfx::TextureRegion{.handle = mark.color->native_handle(), .width = width_, .height = height_},
               bgfx::TextureRegion{.handle = params.color->native_handle(), .width = width_, .height = height_});
    // dispatch_frame has already flipped the write index: the frame just folded in wrote the other one.
    bgfx::blit(pass.id,
               bgfx::TextureRegion{.handle = mark.age->native_handle(), .width = width_, .height = height_},
               bgfx::TextureRegion{.handle = age_[1u - luma_write_]->native_handle(), .width = width_, .height = height_});
    marks_pending_.push_back(std::move(mark));
}

auto temporal_probe_pass::collect_marks() -> std::vector<mark_capture>
{
    std::vector<mark_capture> captures;
    const bool is_bottom_up = bgfx::getCaps()->originBottomLeft;
    for(auto& pending : readback_marks_)
    {
        mark_capture capture;
        capture.frame = pending.frame;
        capture.width = width_;
        capture.height = height_;
        capture.format = pending.format;
        const size_t row_bytes = pending.color_data.size() / std::max<size_t>(height_, 1u);
        capture.color.resize(pending.color_data.size());
        capture.age.resize(pending.age_data.size());
        for(uint32_t y = 0; y < height_; ++y)
        {
            const uint32_t image_y = is_bottom_up ? height_ - 1u - y : y;
            std::copy_n(pending.color_data.data() + size_t(y) * row_bytes,
                        row_bytes,
                        capture.color.data() + size_t(image_y) * row_bytes);
            std::copy_n(pending.age_data.data() + size_t(y) * width_,
                        width_,
                        capture.age.data() + size_t(image_y) * width_);
        }
        captures.push_back(std::move(capture));
    }
    readback_marks_.clear();
    return captures;
}

void temporal_probe_pass::issue_readback()
{
    gfx::render_pass pass("Instruments/Temporal Probe Readback");
    bgfx::blit(pass.id,
               bgfx::TextureRegion{.handle = readback_->native_handle(), .width = width_, .height = height_},
               bgfx::TextureRegion{.handle = sums_->native_handle(), .width = width_, .height = height_});
    readback_ready_frame_ = bgfx::read(bgfx::TextureRegion{.handle = readback_->native_handle()}, readback_data_.data());
    readback_keeps_images_ = keeps_images_;
    if(keeps_images_)
    {
        // dispatch_frame has already flipped the write index: the frame just folded in wrote the other one.
        const auto& last_luma = luma_[1u - luma_write_];
        bgfx::blit(pass.id,
                   bgfx::TextureRegion{.handle = readback_luma_->native_handle(), .width = width_, .height = height_},
                   bgfx::TextureRegion{.handle = last_luma->native_handle(), .width = width_, .height = height_});
        readback_ready_frame_ = std::max(readback_ready_frame_,
                                         bgfx::read(bgfx::TextureRegion{.handle = readback_luma_->native_handle()},
                                                    readback_luma_data_.data()));
    }
    readback_marks_ = std::move(marks_pending_);
    marks_pending_.clear();
    for(auto& mark : readback_marks_)
    {
        readback_ready_frame_ = std::max(readback_ready_frame_,
                                         bgfx::read(bgfx::TextureRegion{.handle = mark.color->native_handle()},
                                                    mark.color_data.data()));
        readback_ready_frame_ = std::max(readback_ready_frame_,
                                         bgfx::read(bgfx::TextureRegion{.handle = mark.age->native_handle()},
                                                    mark.age_data.data()));
    }
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
    const size_t plane = size_t(width) * height;
    std::vector<float> images;
    if(readback_keeps_images_)
    {
        images.assign(plane * 4u, 0.0f);
    }
    std::vector<double> std_cells(size_t(grid_size) * grid_size, 0.0);
    std::vector<double> change_cells(size_t(grid_size) * grid_size, 0.0);
    std::vector<uint32_t> std_counts(size_t(grid_size) * grid_size, 0u);
    std::vector<uint32_t> change_counts(size_t(grid_size) * grid_size, 0u);
    double change_sum = 0.0;
    // The grid is reported from the top of the image, but OpenGL render targets keep their origin
    // at the bottom left, so there the readback's first row is the bottom one.
    const bool is_bottom_up = bgfx::getCaps()->originBottomLeft;
    for(uint32_t y = 0; y < height; ++y)
    {
        const uint32_t image_y = is_bottom_up ? height - 1u - y : y;
        const uint32_t cell_y = std::min(image_y * grid_size / height, grid_size - 1u);
        for(uint32_t x = 0; x < width; ++x)
        {
            const uint32_t cell = cell_y * grid_size + std::min(x * grid_size / width, grid_size - 1u);
            const float* texel = &readback_data_[(size_t(y) * width + x) * 4u];
            const float deviation = std::sqrt(std::max(texel[1] / float(frames), 0.0f));
            deviations.push_back(deviation);
            std_cells[cell] += deviation;
            ++std_counts[cell];
            float change = -1.0f;
            if(frames > 1u && texel[3] >= std::max(min_measured, 1.0f))
            {
                change = texel[2] / texel[3];
                changes.push_back(change);
                change_cells[cell] += change;
                ++change_counts[cell];
                change_sum += change;
            }
            if(!images.empty())
            {
                const size_t pixel = size_t(image_y) * width + x;
                images[pixel] = texel[0];
                images[plane + pixel] = deviation;
                images[2u * plane + pixel] = change;
                images[3u * plane + pixel] = readback_luma_data_[size_t(y) * width + x];
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
    next.images = std::move(images);
    next.marks = collect_marks();
    result_ = std::move(next);
}

} // namespace unravel
