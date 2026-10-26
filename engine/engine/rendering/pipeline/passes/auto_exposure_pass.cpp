#include "auto_exposure_pass.h"
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/render_feature_support.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <bx/math.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace unravel
{
namespace
{
/// The metering bookkeeping of one render view, which only run_average advances.
struct metering_state
{
    static constexpr const char* view_key = "AUTO_EXPOSURE_METERING_STATE";

    /// Forces the next clean average dispatch to converge straight to the target exposure.
    bool force_snap{false};
    /// Next texel of the exposure history ring.
    std::uint32_t history_index{0};
};
constexpr const char* exposure_key = "AUTO_EXPOSURE";
constexpr const char* history_key = "AUTO_EXPOSURE_HISTORY";
constexpr const char* histogram_key = "AUTO_EXPOSURE_HISTOGRAM";
/// Per metering cell, its log2 scene luminance: the histogram writes it, the local exposure
/// grid bins it (cs_local_exposure_grid.sc).
constexpr const char* log_lum_key = "EXPOSURE_LOG_LUM";
/// Local exposure, only while the settings are not neutral: the flattened bilateral grid, the
/// 1/32 resolution log luminance, the horizontal blur pass and the blurred result.
constexpr const char* local_grid_key = "LOCAL_EXPOSURE_GRID";
constexpr const char* local_downsampled_key = "LOCAL_EXPOSURE_DOWNSAMPLED";
constexpr const char* local_blur_temp_key = "LOCAL_EXPOSURE_BLUR_TEMP";
constexpr const char* local_blurred_key = "LOCAL_EXPOSURE_BLURRED";
/// Largest blur radius in blurred texels: the blur's tap limit (63 taps per axis). Mirrors
/// LOCAL_EXPOSURE_BLUR_MAX_RADIUS in cs_local_exposure_blur.sc.
constexpr float local_blur_max_radius = 31.0f;
/// The layout (tile count, uv scales) of the view's local exposure textures.
constexpr const char* local_layout_key = "LOCAL_EXPOSURE_LAYOUT";
constexpr std::uint32_t local_point_clamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
/// The compensation curve LUT and the hash of the keys it was baked from.
constexpr const char* compensation_curve_key = "AUTO_EXPOSURE_COMPENSATION_CURVE";
constexpr const char* compensation_curve_key_data = "AUTO_EXPOSURE_COMPENSATION_CURVE_DATA";

/// Bilinear, clamped: the metering taps sit on texel corners and average 2x2 blocks.
constexpr std::uint32_t metering_sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

/// Pre-exposure values outside this range are treated as invalid input to the histogram.
constexpr float min_valid_pre_exposure = 1e-12f;

/// One-texel copies of what the average and the pre-exposure read from last frame, so they only write
/// AUTO_EXPOSURE and the environment SH: read-write images of four-channel float formats are optional on
/// D3D11 feature level 11_0 and on some Vulkan devices.
constexpr const char* previous_exposure_key = "AUTO_EXPOSURE_PREVIOUS";
constexpr const char* previous_pre_exposure_key = "PRE_EXPOSURE_PREVIOUS";

/// A one-texel copy target of the value a compute pass carries from frame to frame.
auto acquire_previous_texel(gfx::render_view& rview, const char* key) -> const gfx::texture::ptr&
{
    return rview.tex_acquire(key,
                             {.size = {1, 1}, .format = bgfx::TextureFormat::RGBA32F, .flags = BGFX_TEXTURE_BLIT_DST});
}

/**
 * Slope multiplier for the exponential adaptation phase so its speed at the transition matches
 * the linear phase.
 */
auto compute_slope_match(float speed, float transition_stops, float frame_time) -> float
{
    const float safe_speed = std::max(speed, 0.001f);
    const float start_time = transition_stops / safe_speed;
    const float step = 1.0f - std::pow(2.0f, -frame_time * safe_speed);
    return frame_time / (step * start_time);
}

/// The desc of a compute-writable 1-row texture.
auto make_row_desc(std::uint32_t width, bgfx::TextureFormat::Enum format) -> gfx::texture_desc
{
    return gfx::texture_desc{.size = {width, 1}, .format = format, .flags = BGFX_TEXTURE_COMPUTE_WRITE};
}

/**
 * Uploads @p seed into the compute-writable 1-row texture @p texture, created empty.
 *
 * Two-step init to dodge a bgfx GL-backend incompatibility: with BGFX_TEXTURE_COMPUTE_WRITE,
 * bgfx creates the texture via glTexStorage2D (immutable storage) but then uploads an initial
 * payload via glTexImage2D, which is GL_INVALID_OPERATION on immutable storage. So the
 * texture is created empty and the seed goes through update_texture_2d (glTexSubImage2D).
 */
void seed_row_texture(const gfx::texture::ptr& texture, const std::vector<float>& seed)
{
    const bgfx::Memory* payload = bgfx::copy(seed.data(), static_cast<std::uint32_t>(seed.size() * sizeof(float)));
    bgfx::updateTexture2D(texture->native_handle(), 0, 0, 0, 0, texture->info.width, 1, payload);
}

/// The view's compute target @p key, rebuilt when its size or format changed.
auto ensure_compute_target(gfx::render_view& rview, const char* key, const usize32_t& size, bgfx::TextureFormat::Enum format)
    -> gfx::texture::ptr
{
    return rview.tex_acquire(key, {.size = size, .format = format, .flags = BGFX_TEXTURE_COMPUTE_WRITE});
}
} // namespace

auto auto_exposure_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto cs_histogram = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_luminance_histogram.sc");
    auto cs_average = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_histogram_average.sc");
    auto cs_local_grid = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_local_exposure_grid.sc");
    auto cs_local_downsample =
        am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_local_exposure_downsample.sc");
    auto cs_local_blur = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_local_exposure_blur.sc");
    auto cs_pre_exposure = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_pre_exposure.sc");

    if(!cs_histogram || !cs_average || !cs_pre_exposure)
    {
        return false;
    }

    pre_exposure_program_.cache_uniforms();
    pre_exposure_program_.program = std::make_shared<gpu_program>(cs_pre_exposure);

    histogram_program_.cache_uniforms();
    histogram_program_.program = std::make_shared<gpu_program>(cs_histogram);

    average_program_.cache_uniforms();
    average_program_.program = std::make_shared<gpu_program>(cs_average);

    // Local exposure is optional: without its programs the settings simply never engage.
    if(cs_local_grid && cs_local_downsample && cs_local_blur)
    {
        local_grid_program_.cache_uniforms();
        local_grid_program_.program = std::make_shared<gpu_program>(cs_local_grid);
        local_downsample_program_.cache_uniforms();
        local_downsample_program_.program = std::make_shared<gpu_program>(cs_local_downsample);
        local_blur_program_.cache_uniforms();
        local_blur_program_.program = std::make_shared<gpu_program>(cs_local_blur);
    }

    // NOTE: the buffer's initial contents are undefined and CANNOT be seeded from the
    // CPU -- bgfx forbids update() on COMPUTE_WRITE buffers (the same constraint the GI
    // surface-list buffers document). The very first histogram dispatch therefore
    // accumulates into garbage; run_average() discards that first measurement via
    // histogram_bins_valid_ (the pass itself zeroes the bins after reading, so every
    // later frame is clean).
    histogram_buffer_ = bgfx::createDynamicIndexBuffer(histogram_bins,
                                                       BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
    histogram_bins_valid_ = false;

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
    rview.tex_acquire(exposure_key,
                      make_row_desc(1, bgfx::TextureFormat::RGBA32F),
                      [&](const gfx::texture::ptr& exposure_tex)
                      {
                          // Seed a sane state so any reader sampling AUTO_EXPOSURE before the first
                          // run_average() (e.g. tonemapping on frame 0) sees exposure 1, no bias and no
                          // local exposure. The snap flag makes run_average force-converge on its first
                          // dispatch with a clean histogram, so the seed only matters for that short window.
                          seed_row_texture(exposure_tex, {1.0f, 1.0f, 0.0f, 1.0f});
                          rview.data().get_or_emplace<metering_state>(metering_state::view_key).force_snap = true;
                      });

    rview.tex_acquire(history_key,
                      make_row_desc(history_length, bgfx::TextureFormat::RGBA32F),
                      [&](const gfx::texture::ptr& history_tex)
                      {
                          seed_row_texture(history_tex, std::vector<float>(std::size_t(history_length) * 4u, 0.0f));
                          rview.data().get_or_emplace<metering_state>(metering_state::view_key).history_index = 0u;
                      });

    rview.tex_acquire(histogram_key,
                      make_row_desc(std::uint32_t(histogram_bins), bgfx::TextureFormat::R32F),
                      [](const gfx::texture::ptr& histogram_tex)
                      {
                          seed_row_texture(histogram_tex, std::vector<float>(std::size_t(histogram_bins), 0.0f));
                      });
}

auto auto_exposure_pass::get_exposure_texture(gfx::render_view& rview) const -> gfx::texture::ptr
{
    return rview.tex_find(exposure_key);
}

auto auto_exposure_pass::get_history_texture(gfx::render_view& rview) const -> gfx::texture::ptr
{
    return rview.tex_find(history_key);
}

auto auto_exposure_pass::get_histogram_texture(gfx::render_view& rview) const -> gfx::texture::ptr
{
    return rview.tex_find(histogram_key);
}

auto auto_exposure_pass::get_history_index(gfx::render_view& rview) const -> std::uint32_t
{
    const auto* state = rview.data().try_get<metering_state>(metering_state::view_key);
    return state != nullptr ? state->history_index : 0u;
}

auto auto_exposure_pass::get_local_exposure_view(gfx::render_view& rview) const -> local_exposure_view
{
    local_exposure_view view;
    view.grid = rview.tex_find(local_grid_key);
    view.blurred = rview.tex_find(local_blurred_key);
    if(!view.grid || !view.blurred)
    {
        // Neutral settings (or no programs): the tonemapper's own no-op path.
        return {};
    }
    // The layout run_local_exposure recorded for this view's textures.
    if(const auto* layout = rview.data().try_get<local_exposure_view>(local_layout_key))
    {
        view.tiles_x = layout->tiles_x;
        view.tiles_y = layout->tiles_y;
        view.grid_uv_scale = layout->grid_uv_scale;
        view.blurred_uv_scale = layout->blurred_uv_scale;
    }
    return view;
}

auto auto_exposure_pass::compute_metering_size(const usize32_t& input_size) -> usize32_t
{
    const std::uint32_t input_width = std::max(1u, input_size.width);
    const std::uint32_t input_height = std::max(1u, input_size.height);
    std::uint32_t meter_width = (input_width + metering_cell_texels - 1) / metering_cell_texels;
    std::uint32_t meter_height = (input_height + metering_cell_texels - 1) / metering_cell_texels;
    const std::uint32_t longest = std::max(meter_width, meter_height);
    if(longest > max_metering_dim)
    {
        const float cap_scale = float(max_metering_dim) / float(longest);
        meter_width = std::max(1u, std::uint32_t(float(meter_width) * cap_scale));
        meter_height = std::max(1u, std::uint32_t(float(meter_height) * cap_scale));
    }
    return {meter_width, meter_height};
}

void auto_exposure_pass::run_histogram(gfx::render_view& rview, const run_params& params)
{
    const settings& config = params.config;
    const auto input_size = params.input->get_size();
    const std::uint32_t input_width = std::max(1u, input_size.width);
    const std::uint32_t input_height = std::max(1u, input_size.height);

    // One metering cell per 4x4 source block, capped on the long edge.
    const auto meter_size = compute_metering_size(input_size);
    const std::uint32_t meter_width = meter_size.width;
    const std::uint32_t meter_height = meter_size.height;

    // The per-cell log luminance the local exposure grid bins. Written every frame (one texel
    // per cell), so the local exposure chain can be switched on without a warm-up frame.
    const auto& log_lum_tex = rview.tex_acquire(
        log_lum_key,
        {.size = meter_size, .format = bgfx::TextureFormat::R16F, .flags = BGFX_TEXTURE_COMPUTE_WRITE});

    gfx::render_pass pass("Auto Exposure/Histogram");

    histogram_program_.program->begin();

    gfx::set_texture(histogram_program_.s_hdr_input, 0, params.input->get_texture(), metering_sampler_flags);
    params.pre_exposure.bind(stages::histogram_pre_exposure);

    bgfx::setBuffer(1, histogram_buffer_, bgfx::Access::ReadWrite);
    bgfx::setImage(2, log_lum_tex->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::R16F);

    const float log_range = max_log_lum - min_log_lum;
    const float histogram_params[4] = {min_log_lum, 1.0f / log_range, float(meter_width), float(meter_height)};
    gfx::set_uniform(histogram_program_.u_histogram_params, histogram_params);

    const float aspect_ratio = float(input_width) / float(input_height);
    const float log2_pre_exposure = std::log2(std::max(params.pre_exposure.value, min_valid_pre_exposure));
    const float metering_params[4] = {float(static_cast<int>(config.metering_mode)), config.metering_area, aspect_ratio, log2_pre_exposure};
    gfx::set_uniform(histogram_program_.u_metering_params, metering_params);

    const float metering_cell[4] = {float(input_width) / float(meter_width),
                                    float(input_height) / float(meter_height),
                                    1.0f / float(input_width),
                                    1.0f / float(input_height)};
    gfx::set_uniform(histogram_program_.u_metering_cell, metering_cell);

    const std::uint32_t groups_x = (meter_width + 15) / 16;
    const std::uint32_t groups_y = (meter_height + 15) / 16;
    gfx::dispatch(pass.id, histogram_program_.program->native_handle(), groups_x, groups_y, 1);

    histogram_program_.program->end();
}

auto auto_exposure_pass::update_compensation_curve(gfx::render_view& rview, const settings& config)
    -> gfx::texture::ptr
{
    if(!config.compensation_curve.is_valid())
    {
        rview.tex_remove(compensation_curve_key);
        return nullptr;
    }
    // The curve compares by value, so the baked copy is both the cache key and the record of what
    // was baked; no hash over the keys' bytes, which could not tell -0 from 0 anyway.
    auto& baked_curve = rview.data().get_or_emplace<math::curve<float>>(compensation_curve_key_data);
    bool is_built = false;
    const auto& curve_tex =
        rview.tex_acquire(compensation_curve_key,
                          {.size = {compensation_curve_samples, 1}, .format = bgfx::TextureFormat::R16F},
                          [&](const gfx::texture::ptr& /*tex*/)
                          {
                              is_built = true;
                          });
    if(!curve_tex->is_valid())
    {
        // The allocation was refused (GPU memory budget). The texture has no initial contents to
        // restore it from later, so drop it and build it again next frame; until then the
        // neutral (white) curve is bound.
        rview.tex_remove(compensation_curve_key);
        return nullptr;
    }
    if(!is_built && baked_curve == config.compensation_curve)
    {
        return curve_tex;
    }
    // The curve LUT: 2^curve per sample in R16F, read back with linear filtering.
    std::vector<std::uint16_t> lut(compensation_curve_samples);
    const float step = (compensation_curve_max_ev - compensation_curve_min_ev) / float(compensation_curve_samples - 1);
    for(std::uint16_t i = 0; i < compensation_curve_samples; ++i)
    {
        const float ev100 = compensation_curve_min_ev + step * float(i);
        lut[i] = bx::halfFromFloat(std::exp2(config.compensation_curve.sample(ev100)));
    }
    const bgfx::Memory* payload = bgfx::copy(lut.data(), std::uint32_t(lut.size() * sizeof(std::uint16_t)));
    bgfx::updateTexture2D(curve_tex->native_handle(), 0, 0, 0, 0, compensation_curve_samples, 1, payload);
    baked_curve = config.compensation_curve;
    return curve_tex;
}

void auto_exposure_pass::run_average(gfx::render_view& rview, const run_params& params)
{
    auto exposure_tex = rview.tex_get(exposure_key);
    auto history_tex = rview.tex_get(history_key);
    auto histogram_tex = rview.tex_get(histogram_key);
    if(!exposure_tex || !history_tex || !histogram_tex)
    {
        return;
    }

    const settings& config = params.config;

    float effective_dt = params.delta_time;
    float force_target = 0.0f;
    auto& state = rview.data().get_or_emplace<metering_state>(metering_state::view_key);
    if(!histogram_bins_valid_)
    {
        // First dispatch since the buffer was created: its initial contents are
        // undefined and CANNOT be seeded from the CPU -- bgfx asserts on update()
        // for COMPUTE_WRITE buffers (the GI surface-list buffers document the same
        // constraint). This pass zeroes the bins after reading them, so every later
        // frame is clean; discard this one measurement (dt = 0 holds the current
        // exposure) and leave any pending snap for the next, clean dispatch.
        histogram_bins_valid_ = true;
        effective_dt = 0.0f;
    }
    else
    {
        const bool snap_requested = state.force_snap;
        state.force_snap = false;
        // Force the target on camera cuts and when min >= max makes the range a fixed exposure.
        if(snap_requested || params.camera_cut || config.min_ev >= config.max_ev)
        {
            force_target = 1.0f;
        }
    }

    const float history_texel = float(state.history_index % history_length);
    state.history_index = (state.history_index + 1u) % history_length;

    gfx::render_pass pass("Auto Exposure/Average");
    // A view runs its blits before its dispatches: the copy holds the last frame's exposure.
    const auto& previous_tex = acquire_previous_texel(rview, previous_exposure_key);
    bgfx::blit(pass.id,
               bgfx::TextureRegion{.handle = previous_tex->native_handle(), .width = 1, .height = 1},
               bgfx::TextureRegion{.handle = exposure_tex->native_handle(), .width = 1, .height = 1});

    average_program_.program->begin();

    bgfx::setBuffer(0, histogram_buffer_, bgfx::Access::ReadWrite);
    bgfx::setImage(1, exposure_tex->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
    bgfx::setImage(2, history_tex->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
    bgfx::setImage(3, histogram_tex->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::R32F);

    const float log_range = max_log_lum - min_log_lum;
    const float params0[4] = {min_log_lum, log_range, config.low_percentile, config.high_percentile};
    gfx::set_uniform(average_program_.u_average_params0, params0);

    const float params1[4] = {config.min_ev, config.max_ev, config.compensation, 0.0f};
    gfx::set_uniform(average_program_.u_average_params1, params1);

    const float params2[4] = {effective_dt, config.speed_up, config.speed_down, force_target};
    gfx::set_uniform(average_program_.u_average_params2, params2);

    const float slope_up = compute_slope_match(config.speed_up, exponential_transition_stops, slope_match_frame_time);
    const float slope_down = compute_slope_match(config.speed_down, exponential_transition_stops, slope_match_frame_time);
    const float params3[4] = {slope_up, slope_down, exponential_transition_stops, history_texel};
    gfx::set_uniform(average_program_.u_average_params3, params3);

    // The local exposure shape, for the average local exposure the pre-exposure folds in. The
    // detail strength is deliberately absent: with no spatial term a bin's own luminance IS its
    // base, so detail cancels out of the average.
    const float params4[4] = {config.local_highlight_contrast,
                              config.local_shadow_contrast,
                              config.local_middle_grey_bias,
                              0.0f};
    gfx::set_uniform(average_program_.u_average_params4, params4);

    // The compensation curve LUT, addressed with texel centres at the sample EV100 values.
    // Without a curve the white texture reads 1 (no extra stops).
    const auto curve_tex = update_compensation_curve(rview, config);
    const float samples = float(compensation_curve_samples);
    const float lut_scale = (samples - 1.0f) / (samples * (compensation_curve_max_ev - compensation_curve_min_ev));
    const float lut_bias = 0.5f / samples - compensation_curve_min_ev * lut_scale;
    const float params5[4] = {lut_scale, lut_bias, 0.0f, 0.0f};
    gfx::set_uniform(average_program_.u_average_params5, params5);
    gfx::set_texture(average_program_.s_compensation_curve,
                     4,
                     curve_tex ? curve_tex : default_textures::get().white_texture(),
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    gfx::set_texture(average_program_.s_previous_exposure,
                     stages::average_previous_exposure,
                     previous_tex,
                     local_point_clamp);

    gfx::dispatch(pass.id, average_program_.program->native_handle(), 1, 1, 1);

    average_program_.program->end();
}

void auto_exposure_pass::run_local_exposure(gfx::render_view& rview, const run_params& params)
{
    const settings& config = params.config;
    const bool wants_local_exposure =
        config.is_local_exposure_enabled() &&
        get_render_feature_support().is_available(render_feature::local_exposure) &&
        local_grid_program_.program && local_grid_program_.program->is_valid() &&
        local_downsample_program_.program && local_downsample_program_.program->is_valid() &&
        local_blur_program_.program && local_blur_program_.program->is_valid();
    if(!wants_local_exposure)
    {
        // Neutral settings: drop the buffers so the consumer's "no view" branch is the only
        // state to reason about, and a view that never uses local exposure keeps its memory.
        rview.tex_remove(local_grid_key);
        rview.tex_remove(local_downsampled_key);
        rview.tex_remove(local_blur_temp_key);
        rview.tex_remove(local_blurred_key);
        return;
    }
    auto log_lum_tex = rview.tex_find(log_lum_key);
    if(!log_lum_tex)
    {
        return;
    }

    const auto meter_size = compute_metering_size(params.input->get_size());
    const std::uint32_t tiles_x =
        std::max(1u, (meter_size.width + local_exposure_tile_cells - 1) / local_exposure_tile_cells);
    const std::uint32_t tiles_y =
        std::max(1u, (meter_size.height + local_exposure_tile_cells - 1) / local_exposure_tile_cells);
    // The grid is the flattened 3D atlas: one row per tile row, each tile's slices end to end.
    const usize32_t grid_size{tiles_x * local_exposure_slices, tiles_y};
    auto grid_tex = ensure_compute_target(rview, local_grid_key, grid_size, bgfx::TextureFormat::RGBA32F);

    auto& layout = rview.data().get_or_emplace<local_exposure_view>(local_layout_key);
    layout.tiles_x = float(tiles_x);
    layout.tiles_y = float(tiles_y);
    layout.grid_uv_scale = {float(meter_size.width) / float(tiles_x * local_exposure_tile_cells),
                            float(meter_size.height) / float(tiles_y * local_exposure_tile_cells)};

    const float log_range = max_log_lum - min_log_lum;
    {
        gfx::render_pass pass("Auto Exposure/Local Grid");
        local_grid_program_.program->begin();
        gfx::set_texture(local_grid_program_.s_exposure_log_lum, 0, log_lum_tex, local_point_clamp);
        bgfx::setImage(1, grid_tex->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
        const float grid_params[4] = {float(meter_size.width),
                                      float(meter_size.height),
                                      float(local_exposure_tile_cells),
                                      float(local_exposure_slices)};
        gfx::set_uniform(local_grid_program_.u_local_grid_params, grid_params);
        const float grid_range[4] = {min_log_lum, 1.0f / log_range, float(tiles_x), float(tiles_y)};
        gfx::set_uniform(local_grid_program_.u_local_grid_range, grid_range);
        // One workgroup per tile (cs_local_exposure_grid.sc).
        gfx::dispatch(pass.id, local_grid_program_.program->native_handle(), tiles_x, tiles_y, 1);
        local_grid_program_.program->end();
    }
    run_local_exposure_blur(rview, config, meter_size);
}

void auto_exposure_pass::run_local_exposure_blur(gfx::render_view& rview,
                                                 const settings& config,
                                                 const usize32_t& meter_size)
{
    auto log_lum_tex = rview.tex_find(log_lum_key);
    const usize32_t blur_size{std::max(1u, (meter_size.width + local_exposure_blur_cells - 1) / local_exposure_blur_cells),
                              std::max(1u, (meter_size.height + local_exposure_blur_cells - 1) / local_exposure_blur_cells)};
    auto downsampled_tex = ensure_compute_target(rview, local_downsampled_key, blur_size, bgfx::TextureFormat::R16F);
    auto temp_tex = ensure_compute_target(rview, local_blur_temp_key, blur_size, bgfx::TextureFormat::R16F);
    auto blurred_tex = ensure_compute_target(rview, local_blurred_key, blur_size, bgfx::TextureFormat::R16F);

    // The view covers meter / blur_cells texels; the last texel is partial.
    const float covered_width = float(meter_size.width) / float(local_exposure_blur_cells);
    const float covered_height = float(meter_size.height) / float(local_exposure_blur_cells);
    auto& layout = rview.data().get_or_emplace<local_exposure_view>(local_layout_key);
    layout.blurred_uv_scale = {covered_width / float(blur_size.width), covered_height / float(blur_size.height)};

    const std::uint32_t groups_x = (blur_size.width + 7) / 8;
    const std::uint32_t groups_y = (blur_size.height + 7) / 8;
    {
        gfx::render_pass pass("Auto Exposure/Local Downsample");
        local_downsample_program_.program->begin();
        gfx::set_texture(local_downsample_program_.s_exposure_log_lum, 0, log_lum_tex, local_point_clamp);
        bgfx::setImage(1, downsampled_tex->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::R16F);
        const float downsample_params[4] = {float(meter_size.width),
                                            float(meter_size.height),
                                            float(blur_size.width),
                                            float(blur_size.height)};
        gfx::set_uniform(local_downsample_program_.u_local_downsample_params, downsample_params);
        gfx::dispatch(pass.id, local_downsample_program_.program->native_handle(), groups_x, groups_y, 1);
        local_downsample_program_.program->end();
    }
    // The kernel size is a DIAMETER as a percentage of the view width.
    const float radius = std::clamp(covered_width * std::max(config.local_blurred_kernel_percent, 0.0f) * 0.01f * 0.5f,
                                    1e-3f,
                                    local_blur_max_radius);
    const std::array<std::pair<gfx::texture::ptr, gfx::texture::ptr>, 2> blur_passes{
        std::make_pair(downsampled_tex, temp_tex),
        std::make_pair(temp_tex, blurred_tex)};
    for(std::uint32_t axis = 0; axis < 2; ++axis)
    {
        gfx::render_pass pass(axis == 0 ? "Auto Exposure/Local Blur X" : "Auto Exposure/Local Blur Y");
        local_blur_program_.program->begin();
        gfx::set_texture(local_blur_program_.s_local_blur_input, 0, blur_passes[axis].first, local_point_clamp);
        bgfx::setImage(1, blur_passes[axis].second->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::R16F);
        const float blur_params[4] = {float(blur_size.width), float(blur_size.height), radius, float(axis)};
        gfx::set_uniform(local_blur_program_.u_local_blur_params, blur_params);
        gfx::dispatch(pass.id, local_blur_program_.program->native_handle(), groups_x, groups_y, 1);
        local_blur_program_.program->end();
    }
}

void auto_exposure_pass::run(gfx::render_view& rview, const run_params& params)
{
    APP_SCOPE_PERF("Rendering/Auto Exposure Pass");

    ensure_resources(rview);

    run_histogram(rview, params);
    // Before the average: the average folds the local exposure's mean into AUTO_EXPOSURE.a,
    // which the next frame's pre-exposure then carries.
    run_local_exposure(rview, params);
    run_average(rview, params);
}

void auto_exposure_pass::update_gpu_pre_exposure(gfx::render_view& rview,
                                                 const gfx::texture::ptr& environment_sh,
                                                 float manual_exposure,
                                                 bool is_auto_exposure_active)
{
    // begin() links the program once its shader has loaded (it may compile after init). Without it, or on a
    // device that cannot run it, the share stays at the 1 the environment SH starts with.
    if(!environment_sh || !get_render_feature_support().is_available(render_feature::gpu_pre_exposure) ||
       !pre_exposure_program_.program || !pre_exposure_program_.program->begin())
    {
        return;
    }
    // Without auto exposure the share returns to 1, and the history correction carries that step too.
    const auto exposure_tex = is_auto_exposure_active ? rview.tex_find(exposure_key) : nullptr;
    const auto& input = exposure_tex ? exposure_tex : default_textures::get().white_texture();
    const float params[4] = {manual_exposure, pre_exposure_state::min_value, pre_exposure_state::max_value, 0.0f};

    gfx::render_pass pass("Auto Exposure/Pre-Exposure");
    // A view runs its blits before its dispatches: the copy holds the share the last frame left.
    const auto& previous_tex = acquire_previous_texel(rview, previous_pre_exposure_key);
    bgfx::blit(pass.id,
               bgfx::TextureRegion{.handle = previous_tex->native_handle(), .width = 1, .height = 1},
               bgfx::TextureRegion{.handle = environment_sh->native_handle(),
                                   .x = pre_exposure_state::gpu_texel,
                                   .width = 1,
                                   .height = 1});
    gfx::set_uniform(pre_exposure_program_.u_pre_exposure_params, params);
    gfx::set_texture(pre_exposure_program_.s_exposure, 0, input);
    gfx::set_texture(pre_exposure_program_.s_previous_pre_exposure,
                     stages::previous_pre_exposure,
                     previous_tex,
                     local_point_clamp);
    bgfx::setImage(1, environment_sh->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
    gfx::dispatch(pass.id, pre_exposure_program_.program->native_handle(), 1, 1, 1);
    pre_exposure_program_.program->end();
}

void auto_exposure_pass::release_resources(gfx::render_view& rview)
{
    rview.tex_remove(exposure_key);
    rview.tex_remove(history_key);
    rview.tex_remove(histogram_key);
    rview.tex_remove(log_lum_key);
    rview.tex_remove(local_grid_key);
    rview.tex_remove(local_downsampled_key);
    rview.tex_remove(local_blur_temp_key);
    rview.tex_remove(local_blurred_key);
    rview.tex_remove(compensation_curve_key);
    rview.tex_remove(previous_exposure_key);
    rview.data().get_or_emplace<metering_state>(metering_state::view_key).force_snap = true;
}

} // namespace unravel
