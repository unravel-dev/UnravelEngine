#include "auto_exposure_pass.h"
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <bx/math.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace unravel
{
namespace
{
/// Set to 1 to force the next clean average dispatch to the target exposure.
constexpr const char* snap_key = "AUTO_EXPOSURE_SNAP";
/// Next texel of the exposure history ring.
constexpr const char* history_index_key = "AUTO_EXPOSURE_HISTORY_INDEX";
constexpr const char* exposure_key = "AUTO_EXPOSURE";
constexpr const char* history_key = "AUTO_EXPOSURE_HISTORY";
constexpr const char* histogram_key = "AUTO_EXPOSURE_HISTOGRAM";
constexpr const char* readback_target_key = "AUTO_EXPOSURE_READBACK";
/// Per metering cell, its log2 scene luminance: the histogram writes it, the local exposure
/// grid bins it (cs_local_exposure_grid.sc).
constexpr const char* log_lum_key = "EXPOSURE_LOG_LUM";
/// Local exposure, only while the settings are not neutral: the flattened bilateral grid, the
/// 1/32 resolution log luminance, the horizontal blur pass and the blurred result.
constexpr const char* local_grid_key = "LOCAL_EXPOSURE_GRID";
constexpr const char* local_downsampled_key = "LOCAL_EXPOSURE_DOWNSAMPLED";
constexpr const char* local_blur_temp_key = "LOCAL_EXPOSURE_BLUR_TEMP";
constexpr const char* local_blurred_key = "LOCAL_EXPOSURE_BLURRED";
/// Largest blur radius in blurred texels: UE clamps its Gaussian radius to its sample budget
/// (GetClampedKernelRadius, MAX_FILTER_COMPILE_TIME_SAMPLES 32). Mirrors
/// LOCAL_EXPOSURE_BLUR_MAX_RADIUS in cs_local_exposure_blur.sc.
constexpr float local_blur_max_radius = 31.0f;
/// The layout (tile count, uv scales) of the view's local exposure textures.
constexpr const char* local_layout_key = "LOCAL_EXPOSURE_LAYOUT";
constexpr std::uint32_t local_point_clamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
/// The compensation curve LUT and the hash of the keys it was baked from.
constexpr const char* compensation_curve_key = "AUTO_EXPOSURE_COMPENSATION_CURVE";
constexpr const char* compensation_curve_hash_key = "AUTO_EXPOSURE_COMPENSATION_CURVE_HASH";

/// Bilinear, clamped: the metering taps sit on texel corners and average 2x2 blocks.
constexpr std::uint32_t metering_sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

/// Pre-exposure values outside this range are treated as invalid input to the histogram.
constexpr float min_valid_pre_exposure = 1e-12f;

/**
 * Slope multiplier for the exponential adaptation phase so its speed at the transition matches
 * the linear phase (UE PostProcessEyeAdaptation.cpp, ExponentialUpM / ExponentialDownM).
 */
auto compute_slope_match(float speed, float transition_stops, float frame_time) -> float
{
    const float safe_speed = std::max(speed, 0.001f);
    const float start_time = transition_stops / safe_speed;
    const float step = 1.0f - std::pow(2.0f, -frame_time * safe_speed);
    return frame_time / (step * start_time);
}

/**
 * Creates a compute-writable 1-row texture holding @p seed.
 *
 * Two-step init to dodge a bgfx GL-backend incompatibility: with BGFX_TEXTURE_COMPUTE_WRITE,
 * bgfx creates the texture via glTexStorage2D (immutable storage) but then uploads an initial
 * payload via glTexImage2D, which is GL_INVALID_OPERATION on immutable storage. So the
 * texture is created empty and the seed goes through update_texture_2d (glTexSubImage2D).
 */
auto create_seeded_row_texture(std::uint16_t width, bgfx::TextureFormat::Enum format, const std::vector<float>& seed)
    -> gfx::texture::ptr
{
    auto texture = std::make_shared<gfx::texture>(width, 1, false, 1, format, BGFX_TEXTURE_COMPUTE_WRITE);
    const bgfx::Memory* payload = bgfx::copy(seed.data(), static_cast<std::uint32_t>(seed.size() * sizeof(float)));
    bgfx::updateTexture2D(texture->native_handle(), 0, 0, 0, 0, width, 1, payload);
    return texture;
}

/// The view's compute target @p key, recreated when its size or format changed.
auto ensure_compute_target(gfx::render_view& rview, const char* key, const usize32_t& size, bgfx::TextureFormat::Enum format)
    -> gfx::texture::ptr
{
    auto& texture = rview.tex_get_or_emplace(key);
    if(gfx::needs_recreate(texture, size, format))
    {
        texture.reset();
        texture = std::make_shared<gfx::texture>(std::uint16_t(size.width),
                                                 std::uint16_t(size.height),
                                                 false,
                                                 1,
                                                 format,
                                                 BGFX_TEXTURE_COMPUTE_WRITE);
    }
    return texture;
}

/// The compensation curve in stops at @p ev100: linear between keys sorted by x, constant past
/// the ends (FRichCurve's default extrapolation).
auto evaluate_compensation_curve(const std::vector<math::vec2>& sorted_keys, float ev100) -> float
{
    if(ev100 <= sorted_keys.front().x)
    {
        return sorted_keys.front().y;
    }
    if(ev100 >= sorted_keys.back().x)
    {
        return sorted_keys.back().y;
    }
    const auto upper = std::upper_bound(sorted_keys.begin(),
                                        sorted_keys.end(),
                                        ev100,
                                        [](float value, const math::vec2& key) { return value < key.x; });
    const auto lower = upper - 1;
    const float span = std::max(upper->x - lower->x, 1e-6f);
    return math::mix(lower->y, upper->y, (ev100 - lower->x) / span);
}

/// FNV-1a over the keys' bytes: the LUT is rebaked only when the curve changes.
auto hash_compensation_curve(const std::vector<math::vec2>& keys) -> std::uint64_t
{
    constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
    constexpr std::uint64_t fnv_prime = 1099511628211ull;
    std::uint64_t hash = fnv_offset;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(keys.data());
    const std::size_t size = keys.size() * sizeof(math::vec2);
    for(std::size_t i = 0; i < size; ++i)
    {
        hash = (hash ^ bytes[i]) * fnv_prime;
    }
    return hash ^ keys.size();
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
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_readback = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/fs_exposure_readback.sc");

    if(!cs_histogram || !cs_average || !vs_clip_quad || !fs_readback)
    {
        return false;
    }

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

    readback_program_.cache_uniforms();
    readback_program_.program = std::make_shared<gpu_program>(vs_clip_quad, fs_readback);

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

auto_exposure_pass::readback_state::readback_state()
{
    for(auto& query : queries)
    {
        query = BGFX_INVALID_HANDLE;
    }
}

auto_exposure_pass::readback_state::~readback_state()
{
    reset();
}

void auto_exposure_pass::readback_state::reset()
{
    for(auto& query : queries)
    {
        if(bgfx::isValid(query))
        {
            bgfx::destroy(query);
        }
        query = BGFX_INVALID_HANDLE;
    }
    created = false;
    value = 1.0f;
}

void auto_exposure_pass::ensure_resources(gfx::render_view& rview)
{
    auto& exposure_tex = rview.tex_get_or_emplace(exposure_key);
    if(gfx::needs_recreate(exposure_tex, {1, 1}, bgfx::TextureFormat::RGBA32F))
    {
        // Seed a sane state so any reader sampling AUTO_EXPOSURE before the first
        // run_average() (e.g. tonemapping on frame 0) sees exposure 1, no bias and no local
        // exposure. The snap flag makes run_average force-converge on its first dispatch with
        // a clean histogram, so the seed only matters for that short window.
        exposure_tex.reset();
        exposure_tex = create_seeded_row_texture(1, bgfx::TextureFormat::RGBA32F, {1.0f, 1.0f, 0.0f, 1.0f});
        rview.data_get_or_emplace(snap_key, 1u) = 1u;
    }

    auto& history_tex = rview.tex_get_or_emplace(history_key);
    if(gfx::needs_recreate(history_tex, {history_length, 1}, bgfx::TextureFormat::RGBA32F))
    {
        history_tex.reset();
        history_tex = create_seeded_row_texture(history_length,
                                                bgfx::TextureFormat::RGBA32F,
                                                std::vector<float>(std::size_t(history_length) * 4u, 0.0f));
        rview.data_get_or_emplace(history_index_key, 0u) = 0u;
    }

    auto& histogram_tex = rview.tex_get_or_emplace(histogram_key);
    if(gfx::needs_recreate(histogram_tex, {std::uint32_t(histogram_bins), 1}, bgfx::TextureFormat::R32F))
    {
        histogram_tex.reset();
        histogram_tex = create_seeded_row_texture(std::uint16_t(histogram_bins),
                                                  bgfx::TextureFormat::R32F,
                                                  std::vector<float>(std::size_t(histogram_bins), 0.0f));
    }
}

auto auto_exposure_pass::get_exposure_texture(gfx::render_view& rview) const -> gfx::texture::ptr
{
    return rview.tex_safe_get(exposure_key);
}

auto auto_exposure_pass::get_history_texture(gfx::render_view& rview) const -> gfx::texture::ptr
{
    return rview.tex_safe_get(history_key);
}

auto auto_exposure_pass::get_histogram_texture(gfx::render_view& rview) const -> gfx::texture::ptr
{
    return rview.tex_safe_get(histogram_key);
}

auto auto_exposure_pass::get_history_index(gfx::render_view& rview) const -> std::uint32_t
{
    const auto* index = rview.data().try_get<std::uint32_t>(history_index_key);
    return index != nullptr ? *index : 0u;
}

auto auto_exposure_pass::get_local_exposure_view(gfx::render_view& rview) const -> local_exposure_view
{
    local_exposure_view view;
    view.grid = rview.tex_safe_get(local_grid_key);
    view.blurred = rview.tex_safe_get(local_blurred_key);
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
    auto& log_lum_tex = rview.tex_get_or_emplace(log_lum_key);
    if(gfx::needs_recreate(log_lum_tex, meter_size, bgfx::TextureFormat::R16F))
    {
        log_lum_tex.reset();
        log_lum_tex = std::make_shared<gfx::texture>(std::uint16_t(meter_width),
                                                     std::uint16_t(meter_height),
                                                     false,
                                                     1,
                                                     bgfx::TextureFormat::R16F,
                                                     BGFX_TEXTURE_COMPUTE_WRITE);
    }

    gfx::render_pass pass("Auto Exposure/Histogram");

    histogram_program_.program->begin();

    gfx::set_texture(histogram_program_.s_hdr_input, 0, params.input->get_texture(), metering_sampler_flags);

    bgfx::setBuffer(1, histogram_buffer_, bgfx::Access::ReadWrite);
    bgfx::setImage(2, log_lum_tex->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::R16F);

    const float log_range = max_log_lum - min_log_lum;
    const float histogram_params[4] = {min_log_lum, 1.0f / log_range, float(meter_width), float(meter_height)};
    gfx::set_uniform(histogram_program_.u_histogram_params, histogram_params);

    const float aspect_ratio = float(input_width) / float(input_height);
    const float log2_pre_exposure = std::log2(std::max(params.pre_exposure, min_valid_pre_exposure));
    const float metering_params[4] = {float(static_cast<int>(config.metering_mode)), config.metering_area, aspect_ratio, log2_pre_exposure};
    gfx::set_uniform(histogram_program_.u_metering_params, metering_params);

    const float metering_cell[4] = {float(input_width) / float(meter_width),
                                    float(input_height) / float(meter_height),
                                    1.0f / float(input_width),
                                    1.0f / float(input_height)};
    gfx::set_uniform(histogram_program_.u_metering_cell, metering_cell);

    const std::uint32_t groups_x = (meter_width + 15) / 16;
    const std::uint32_t groups_y = (meter_height + 15) / 16;
    bgfx::dispatch(pass.id, histogram_program_.program->native_handle(), groups_x, groups_y, 1);

    histogram_program_.program->end();
}

auto auto_exposure_pass::update_compensation_curve(gfx::render_view& rview, const settings& config)
    -> gfx::texture::ptr
{
    if(config.compensation_curve.empty())
    {
        rview.tex_remove(compensation_curve_key);
        return nullptr;
    }
    const std::uint64_t hash = hash_compensation_curve(config.compensation_curve);
    auto& baked_hash = rview.data().get_or_emplace<std::uint64_t>(compensation_curve_hash_key);
    auto& curve_tex = rview.tex_get_or_emplace(compensation_curve_key);
    if(curve_tex && baked_hash == hash)
    {
        return curve_tex;
    }
    // UE CreateCurveLUT: 2^curve per sample in R16F, read back with linear filtering.
    std::vector<math::vec2> sorted_keys = config.compensation_curve;
    std::sort(sorted_keys.begin(),
              sorted_keys.end(),
              [](const math::vec2& a, const math::vec2& b) { return a.x < b.x; });
    std::vector<std::uint16_t> lut(compensation_curve_samples);
    const float step = (compensation_curve_max_ev - compensation_curve_min_ev) / float(compensation_curve_samples - 1);
    for(std::uint16_t i = 0; i < compensation_curve_samples; ++i)
    {
        const float ev100 = compensation_curve_min_ev + step * float(i);
        lut[i] = bx::halfFromFloat(std::exp2(evaluate_compensation_curve(sorted_keys, ev100)));
    }
    const bgfx::Memory* payload = bgfx::copy(lut.data(), std::uint32_t(lut.size() * sizeof(std::uint16_t)));
    curve_tex = std::make_shared<gfx::texture>(compensation_curve_samples,
                                               1,
                                               false,
                                               1,
                                               bgfx::TextureFormat::R16F,
                                               BGFX_TEXTURE_NONE,
                                               payload);
    baked_hash = hash;
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
    auto& snap = rview.data_get_or_emplace(snap_key, 0u);
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
        const bool snap_requested = snap != 0u;
        snap = 0u;
        // UE forces the target on camera cuts and when min >= max makes the range a fixed exposure.
        if(snap_requested || params.camera_cut || config.min_ev >= config.max_ev)
        {
            force_target = 1.0f;
        }
    }

    auto& history_index = rview.data_get_or_emplace(history_index_key, 0u);
    const float history_texel = float(history_index % history_length);
    history_index = (history_index + 1u) % history_length;

    gfx::render_pass pass("Auto Exposure/Average");

    average_program_.program->begin();

    bgfx::setBuffer(0, histogram_buffer_, bgfx::Access::ReadWrite);
    bgfx::setImage(1, exposure_tex->native_handle(), 0, bgfx::Access::ReadWrite, bgfx::TextureFormat::RGBA32F);
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

    // The local exposure shape, for the average local exposure the pre-exposure folds in (UE
    // ComputeAverageLocalExposure). The detail strength is deliberately absent: with no spatial
    // term a bin's own luminance IS its base, so detail cancels out of the average.
    const float params4[4] = {config.local_highlight_contrast,
                              config.local_shadow_contrast,
                              config.local_middle_grey_bias,
                              0.0f};
    gfx::set_uniform(average_program_.u_average_params4, params4);

    // The compensation curve LUT, addressed as UE GetExposureCompensationCurveLUTScaleBias does:
    // texel centres at the sample EV100 values. Without a curve the white texture reads 1 (no
    // extra stops).
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

    bgfx::dispatch(pass.id, average_program_.program->native_handle(), 1, 1, 1);

    average_program_.program->end();
}

void auto_exposure_pass::run_local_exposure(gfx::render_view& rview, const run_params& params)
{
    const settings& config = params.config;
    const bool wants_local_exposure =
        config.is_local_exposure_enabled() && local_grid_program_.program && local_grid_program_.program->is_valid() &&
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
    auto log_lum_tex = rview.tex_safe_get(log_lum_key);
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
        bgfx::dispatch(pass.id, local_grid_program_.program->native_handle(), tiles_x, tiles_y, 1);
        local_grid_program_.program->end();
    }
    run_local_exposure_blur(rview, config, meter_size);
}

void auto_exposure_pass::run_local_exposure_blur(gfx::render_view& rview,
                                                 const settings& config,
                                                 const usize32_t& meter_size)
{
    auto log_lum_tex = rview.tex_safe_get(log_lum_key);
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
        bgfx::dispatch(pass.id, local_downsample_program_.program->native_handle(), groups_x, groups_y, 1);
        local_downsample_program_.program->end();
    }
    // UE GetBlurRadius: the kernel size is a DIAMETER as a percentage of the view width.
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
        bgfx::dispatch(pass.id, local_blur_program_.program->native_handle(), groups_x, groups_y, 1);
        local_blur_program_.program->end();
    }
}

auto auto_exposure_pass::ensure_readback_queries(readback_state& state) -> bool
{
    if(state.created)
    {
        return true;
    }
    for(auto& query : state.queries)
    {
        query = bgfx::createOcclusionQuery();
        if(!bgfx::isValid(query))
        {
            // The global pool is exhausted: give back what was taken and stay without a
            // readback (pre-exposure then follows the manual exposure only).
            state.reset();
            return false;
        }
    }
    state.created = true;
    return true;
}

void auto_exposure_pass::submit_exposure_readback(gfx::render_view& rview)
{
    auto exposure_tex = rview.tex_get(exposure_key);
    auto& state = rview.data().get_or_emplace<readback_state>(readback_state::view_key);
    if(!exposure_tex || !readback_program_.program || !ensure_readback_queries(state))
    {
        return;
    }

    auto& target = rview.fbo_get_or_emplace(readback_target_key);
    if(!target)
    {
        auto target_tex = std::make_shared<gfx::texture>(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
        target = std::make_shared<gfx::frame_buffer>();
        target->populate({target_tex});
    }

    const std::uint32_t tag = state.submissions & ((1u << readback_tag_bits) - 1u);
    ++state.submissions;

    const float max_code = float((1u << readback_code_bits) - 1u);
    const float range[4] = {readback_min_log2, max_code / (readback_max_log2 - readback_min_log2), max_code, 0.0f};

    gfx::render_pass pass("Auto Exposure/Readback");
    pass.bind(target.get());

    if(!readback_program_.program->begin())
    {
        return;
    }
    for(std::uint32_t slot = 0; slot < readback_query_count; ++slot)
    {
        const bool is_code_bit = slot >= readback_tag_bits && slot < readback_tag_bits + readback_code_bits;
        const std::uint32_t bit_index = is_code_bit ? slot - readback_tag_bits
                                                    : (slot < readback_tag_bits ? slot : slot - readback_tag_bits - readback_code_bits);
        const float tag_bit_value = float((tag >> bit_index) & 1u);
        const float bit[4] = {is_code_bit ? 1.0f : 0.0f, float(bit_index), tag_bit_value, 0.0f};

        gfx::set_uniform(readback_program_.u_readback_bit, bit);
        gfx::set_uniform(readback_program_.u_readback_range, range);
        gfx::set_texture(readback_program_.s_exposure, 0, exposure_tex);
        const auto topology = gfx::clip_quad(1.0f);
        bgfx::setState(topology | BGFX_STATE_WRITE_RGB);
        bgfx::submit(pass.id, readback_program_.program->native_handle(), state.queries[slot]);
    }
    bgfx::setState(BGFX_STATE_DEFAULT);
    readback_program_.program->end();
}

auto auto_exposure_pass::resolve_exposure_readback(gfx::render_view& rview) -> float
{
    // A view without its own channel (never ran the readback, or its queries could not be
    // created) follows the manual exposure: never another view's value.
    auto* state = rview.data().try_get<readback_state>(readback_state::view_key);
    if(!state || !state->created)
    {
        return 1.0f;
    }

    std::array<bool, readback_query_count> bits{};
    for(std::uint32_t slot = 0; slot < readback_query_count; ++slot)
    {
        const auto result = bgfx::getResult(state->queries[slot]);
        if(result == bgfx::OcclusionQueryResult::NoResult)
        {
            return state->value;
        }
        bits[slot] = result == bgfx::OcclusionQueryResult::Visible;
    }

    std::uint32_t leading_tag = 0;
    std::uint32_t trailing_tag = 0;
    const std::uint32_t trailing_start = readback_tag_bits + readback_code_bits;
    for(std::uint32_t bit = 0; bit < readback_tag_bits; ++bit)
    {
        leading_tag |= std::uint32_t(bits[bit]) << bit;
        trailing_tag |= std::uint32_t(bits[trailing_start + bit]) << bit;
    }
    if(leading_tag != trailing_tag)
    {
        // A submission is still resolving: the queries in between may mix two frames.
        return state->value;
    }

    std::uint32_t code = 0;
    for(std::uint32_t bit = 0; bit < readback_code_bits; ++bit)
    {
        code |= std::uint32_t(bits[readback_tag_bits + bit]) << bit;
    }
    const float max_code = float((1u << readback_code_bits) - 1u);
    const float stops_per_code = (readback_max_log2 - readback_min_log2) / max_code;
    state->value = std::pow(2.0f, readback_min_log2 + float(code) * stops_per_code);
    return state->value;
}

void auto_exposure_pass::run(gfx::render_view& rview, const run_params& params)
{
    APP_SCOPE_PERF("Rendering/Auto Exposure Pass");

    ensure_resources(rview);

    run_histogram(rview, params);
    // Before the average: the average folds the local exposure's mean into AUTO_EXPOSURE.a,
    // which the pre-exposure readback then carries (UE LastAverageLocalExposure).
    run_local_exposure(rview, params);
    run_average(rview, params);
    submit_exposure_readback(rview);
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
    rview.fbo_remove(readback_target_key);
    rview.data_get_or_emplace(snap_key, 1u) = 1u;
    // THIS view's channel only: the pass instance also serves other views' frames.
    if(auto* state = rview.data().try_get<readback_state>(readback_state::view_key))
    {
        state->reset();
    }
}

} // namespace unravel
