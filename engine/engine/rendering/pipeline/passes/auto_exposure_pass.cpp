#include "auto_exposure_pass.h"
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <algorithm>
#include <cstring>

namespace unravel
{

auto auto_exposure_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto cs_histogram = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_luminance_histogram.sc");
    auto cs_average = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_histogram_average.sc");

    if(!cs_histogram || !cs_average)
    {
        return false;
    }

    histogram_program_.cache_uniforms();
    histogram_program_.program = std::make_shared<gpu_program>(cs_histogram);

    average_program_.cache_uniforms();
    average_program_.program = std::make_shared<gpu_program>(cs_average);

    histogram_buffer_ = bgfx::createDynamicIndexBuffer(histogram_bins,
                                                       BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);

    if(bgfx::isValid(histogram_buffer_))
    {
        // Dynamic buffers are created with undefined contents. The average pass zeroes
        // the bins after consuming them, but the very first histogram dispatch would
        // accumulate into garbage -- and the first-frame snap then converges exposure
        // fully onto that garbage-influenced target, which takes the 1-3 s adaptation
        // constants to decay. Seed the bins to zero once instead.
        const bgfx::Memory* zeroed = bgfx::alloc(histogram_bins * sizeof(std::uint32_t));
        std::memset(zeroed->data, 0, zeroed->size);
        bgfx::update(histogram_buffer_, 0, zeroed);
    }

    return histogram_program_.program->is_valid() &&
           average_program_.program->is_valid() &&
           bgfx::isValid(histogram_buffer_);
}

auto auto_exposure_pass::shutdown() -> int32_t
{
    if(bgfx::isValid(histogram_buffer_))
    {
        bgfx::destroy(histogram_buffer_);
        histogram_buffer_ = BGFX_INVALID_HANDLE;
    }


    return 0;
}

void auto_exposure_pass::ensure_resources(gfx::render_view& rview)
{
    auto& exposure_tex = rview.tex_get_or_emplace("AUTO_EXPOSURE");
    if(gfx::needs_recreate(exposure_tex, {1, 1}))
    {
        // Two-step init to dodge a bgfx GL-backend incompatibility: when
        // BGFX_TEXTURE_COMPUTE_WRITE is set, bgfx creates the texture via glTexStorage2D
        // (immutable storage), but the same code path then tries to upload any initial
        // `_mem` payload via glTexImage2D -- which is GL_INVALID_OPERATION on immutable
        // storage. So we create the texture with no initial data, then push the seed
        // value through update_texture_2d (which routes to glTexSubImage2D, the API
        // that *is* allowed on immutable storage). Reproduces for any R32F + COMPUTE_WRITE
        // texture regardless of dimensions; size doesn't matter.
        exposure_tex.reset();
        exposure_tex = std::make_shared<gfx::texture>(1,
                                                      1,
                                                      false,
                                                      1,
                                                      gfx::texture_format::R32F,
                                                      BGFX_TEXTURE_COMPUTE_WRITE);

        // Seed the texel to 1.0 so any reader sampling AUTO_EXPOSURE before the very first
        // run_average() (e.g. tonemapping on frame 0) sees a sane multiplier instead of
        // whatever was in freshly-allocated storage. The AUTO_EXPOSURE_SNAP flag below
        // makes run_average force-converge to the measured value on its first dispatch,
        // so the seed only matters for that single-frame window.
        const float             initial_exposure = 1.0f;
        const gfx::memory_view* initial_pixel    = gfx::copy(&initial_exposure, sizeof(initial_exposure));
        gfx::update_texture_2d(exposure_tex->native_handle(), 0, 0, 0, 0, 1, 1, initial_pixel);

        rview.data_get_or_emplace("AUTO_EXPOSURE_SNAP", 1u) = 1u;
    }
}

auto auto_exposure_pass::get_exposure_texture(gfx::render_view& rview) const -> gfx::texture::ptr
{
    return rview.tex_safe_get("AUTO_EXPOSURE");
}

void auto_exposure_pass::run_histogram(gfx::render_view& rview, const settings& config, const gfx::frame_buffer::ptr& input)
{
    const auto input_size = input->get_size();

    // Meter from a subsampled grid rather than the full-resolution buffer. The grid
    // is capped to max_metering_dim on the long edge (aspect preserved), which keeps
    // the histogram statistically equivalent while cutting texture bandwidth and the
    // number of atomic increments dramatically at high resolutions.
    const uint32_t longest = std::max(input_size.width, input_size.height);
    const float scale = (longest > max_metering_dim) ? (float(max_metering_dim) / float(longest)) : 1.0f;
    const uint32_t meter_width = std::max(1u, uint32_t(float(input_size.width) * scale));
    const uint32_t meter_height = std::max(1u, uint32_t(float(input_size.height) * scale));

    gfx::render_pass pass("Auto Exposure/Histogram");

    histogram_program_.program->begin();

    gfx::set_texture(histogram_program_.s_hdr_input, 0, input->get_texture());

    gfx::set_buffer(1, histogram_buffer_, bgfx::Access::ReadWrite);

    float log_range = max_log_lum - min_log_lum;
    float inv_log_range = 1.0f / log_range;
    float params[4] = {min_log_lum, inv_log_range, float(meter_width), float(meter_height)};
    gfx::set_uniform(histogram_program_.u_histogram_params, params);

    float metering_params[4] = {float(static_cast<int>(config.metering_mode)), config.metering_area, 0.0f, 0.0f};
    gfx::set_uniform(histogram_program_.u_metering_params, metering_params);

    uint32_t groups_x = (meter_width + 15) / 16;
    uint32_t groups_y = (meter_height + 15) / 16;
    bgfx::dispatch(pass.id, histogram_program_.program->native_handle(), groups_x, groups_y, 1);

    histogram_program_.program->end();
}

void auto_exposure_pass::run_average(gfx::render_view& rview, const settings& config, float dt)
{
    auto exposure_tex = rview.tex_get("AUTO_EXPOSURE");
    if(!exposure_tex)
    {
        return;
    }

    gfx::render_pass pass("Auto Exposure/Average");

    average_program_.program->begin();

    gfx::set_buffer(0, histogram_buffer_, bgfx::Access::ReadWrite);

    gfx::set_image(1, exposure_tex->native_handle(), 0, bgfx::Access::ReadWrite, gfx::texture_format::R32F);

    float log_range = max_log_lum - min_log_lum;
    float params0[4] = {min_log_lum, log_range, config.low_percentile, config.high_percentile};
    gfx::set_uniform(average_program_.u_average_params0, params0);

    float params1[4] = {config.min_ev, config.max_ev, config.compensation, config.dark_adaptation};
    gfx::set_uniform(average_program_.u_average_params1, params1);

    float effective_dt = dt;
    auto& snap = rview.data_get_or_emplace("AUTO_EXPOSURE_SNAP", 0u);
    if(snap != 0u)
    {
        effective_dt = 100.0f;
        snap = 0u;
    }

    float params2[4] = {effective_dt, config.adaptation_speed_up, config.adaptation_speed_down, 0.0f};
    gfx::set_uniform(average_program_.u_average_params2, params2);

    bgfx::dispatch(pass.id, average_program_.program->native_handle(), 1, 1, 1);

    average_program_.program->end();
}

void auto_exposure_pass::run(gfx::render_view& rview, const run_params& params)
{
    APP_SCOPE_PERF("Rendering/Auto Exposure Pass");

    ensure_resources(rview);

    run_histogram(rview, params.config, params.input);
    run_average(rview, params.config, params.delta_time);
}

void auto_exposure_pass::release_resources(gfx::render_view& rview)
{
    rview.tex_remove("AUTO_EXPOSURE");
    rview.data_get_or_emplace("AUTO_EXPOSURE_SNAP", 1u) = 1u;
}

} // namespace unravel
