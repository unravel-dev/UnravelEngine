#include "auto_exposure_pass.h"
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <algorithm>
#include <cmath>
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
/// per-tile mean the grid pass reduces on the way, and its Gaussian.
constexpr const char* local_grid_key = "LOCAL_EXPOSURE_GRID";
constexpr const char* local_mean_key = "LOCAL_EXPOSURE_MEAN";
constexpr const char* local_blurred_key = "LOCAL_EXPOSURE_BLURRED";

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
auto create_seeded_row_texture(std::uint16_t width, gfx::texture_format format, const std::vector<float>& seed)
    -> gfx::texture::ptr
{
    auto texture = std::make_shared<gfx::texture>(width, 1, false, 1, format, BGFX_TEXTURE_COMPUTE_WRITE);
    const gfx::memory_view* payload = gfx::copy(seed.data(), static_cast<std::uint32_t>(seed.size() * sizeof(float)));
    gfx::update_texture_2d(texture->native_handle(), 0, 0, 0, 0, width, 1, payload);
    return texture;
}
} // namespace

auto auto_exposure_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto cs_histogram = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_luminance_histogram.sc");
    auto cs_average = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_histogram_average.sc");
    auto cs_local_grid = am.get_asset<gfx::shader>("engine:/data/shaders/exposure/cs_local_exposure_grid.sc");
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
    if(cs_local_grid && cs_local_blur)
    {
        local_grid_program_.cache_uniforms();
        local_grid_program_.program = std::make_shared<gpu_program>(cs_local_grid);
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

    destroy_readback_queries();

    return 0;
}

void auto_exposure_pass::ensure_resources(gfx::render_view& rview)
{
    auto& exposure_tex = rview.tex_get_or_emplace(exposure_key);
    if(gfx::needs_recreate(exposure_tex, {1, 1}, gfx::texture_format::RGBA32F))
    {
        // Seed a sane state so any reader sampling AUTO_EXPOSURE before the first
        // run_average() (e.g. tonemapping on frame 0) sees exposure 1, no bias and no local
        // exposure. The snap flag makes run_average force-converge on its first dispatch with
        // a clean histogram, so the seed only matters for that short window.
        exposure_tex.reset();
        exposure_tex = create_seeded_row_texture(1, gfx::texture_format::RGBA32F, {1.0f, 1.0f, 0.0f, 1.0f});
        rview.data_get_or_emplace(snap_key, 1u) = 1u;
    }

    auto& history_tex = rview.tex_get_or_emplace(history_key);
    if(gfx::needs_recreate(history_tex, {history_length, 1}, gfx::texture_format::RGBA32F))
    {
        history_tex.reset();
        history_tex = create_seeded_row_texture(history_length,
                                                gfx::texture_format::RGBA32F,
                                                std::vector<float>(std::size_t(history_length) * 4u, 0.0f));
        rview.data_get_or_emplace(history_index_key, 0u) = 0u;
    }

    auto& histogram_tex = rview.tex_get_or_emplace(histogram_key);
    if(gfx::needs_recreate(histogram_tex, {std::uint32_t(histogram_bins), 1}, gfx::texture_format::R32F))
    {
        histogram_tex.reset();
        histogram_tex = create_seeded_row_texture(std::uint16_t(histogram_bins),
                                                  gfx::texture_format::R32F,
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
    // The blurred texture IS the tile grid, so it carries the layout the consumer needs; the
    // grid's own width is tiles x slices.
    view.tiles_x = float(view.blurred->info.width);
    view.tiles_y = float(view.blurred->info.height);
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
    if(gfx::needs_recreate(log_lum_tex, meter_size, gfx::texture_format::R16F))
    {
        log_lum_tex.reset();
        log_lum_tex = std::make_shared<gfx::texture>(std::uint16_t(meter_width),
                                                     std::uint16_t(meter_height),
                                                     false,
                                                     1,
                                                     gfx::texture_format::R16F,
                                                     BGFX_TEXTURE_COMPUTE_WRITE);
    }

    gfx::render_pass pass("Auto Exposure/Histogram");

    histogram_program_.program->begin();

    gfx::set_texture(histogram_program_.s_hdr_input, 0, params.input->get_texture(), metering_sampler_flags);

    gfx::set_buffer(1, histogram_buffer_, bgfx::Access::ReadWrite);
    gfx::set_image(2, log_lum_tex->native_handle(), 0, bgfx::Access::Write, gfx::texture_format::R16F);

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

    gfx::set_buffer(0, histogram_buffer_, bgfx::Access::ReadWrite);
    gfx::set_image(1, exposure_tex->native_handle(), 0, bgfx::Access::ReadWrite, gfx::texture_format::RGBA32F);
    gfx::set_image(2, history_tex->native_handle(), 0, bgfx::Access::Write, gfx::texture_format::RGBA32F);
    gfx::set_image(3, histogram_tex->native_handle(), 0, bgfx::Access::Write, gfx::texture_format::R32F);

    const float log_range = max_log_lum - min_log_lum;
    const float params0[4] = {min_log_lum, log_range, config.low_percentile, config.high_percentile};
    gfx::set_uniform(average_program_.u_average_params0, params0);

    const float params1[4] = {config.min_ev, config.max_ev, config.compensation, config.dark_adaptation};
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

    bgfx::dispatch(pass.id, average_program_.program->native_handle(), 1, 1, 1);

    average_program_.program->end();
}

void auto_exposure_pass::run_local_exposure(gfx::render_view& rview, const run_params& params)
{
    const settings& config = params.config;
    const bool wants_local_exposure = config.is_local_exposure_enabled() &&
                                      local_grid_program_.program && local_grid_program_.program->is_valid() &&
                                      local_blur_program_.program && local_blur_program_.program->is_valid();
    if(!wants_local_exposure)
    {
        // Neutral settings: drop the buffers so the consumer's "no view" branch is the only
        // state to reason about, and a view that never uses local exposure keeps its memory.
        rview.tex_remove(local_grid_key);
        rview.tex_remove(local_mean_key);
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
    const usize32_t tile_size{tiles_x, tiles_y};
    // The grid is the flattened 3D atlas: one row per tile row, each tile's slices end to end.
    const usize32_t grid_size{tiles_x * local_exposure_slices, tiles_y};

    auto ensure_compute_target = [&rview](const char* key, const usize32_t& size, gfx::texture_format format)
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
    };
    auto grid_tex = ensure_compute_target(local_grid_key, grid_size, gfx::texture_format::RGBA32F);
    auto mean_tex = ensure_compute_target(local_mean_key, tile_size, gfx::texture_format::R32F);
    auto blurred_tex = ensure_compute_target(local_blurred_key, tile_size, gfx::texture_format::R32F);

    const std::uint32_t groups_x = (tiles_x + 7) / 8;
    const std::uint32_t groups_y = (tiles_y + 7) / 8;
    const float log_range = max_log_lum - min_log_lum;
    constexpr std::uint32_t point_clamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    {
        gfx::render_pass pass("Auto Exposure/Local Grid");
        local_grid_program_.program->begin();
        gfx::set_texture(local_grid_program_.s_exposure_log_lum, 0, log_lum_tex, point_clamp);
        gfx::set_image(1, grid_tex->native_handle(), 0, bgfx::Access::Write, gfx::texture_format::RGBA32F);
        gfx::set_image(2, mean_tex->native_handle(), 0, bgfx::Access::Write, gfx::texture_format::R32F);
        const float grid_params[4] = {float(meter_size.width),
                                      float(meter_size.height),
                                      float(local_exposure_tile_cells),
                                      float(local_exposure_slices)};
        gfx::set_uniform(local_grid_program_.u_local_grid_params, grid_params);
        const float grid_range[4] = {min_log_lum, 1.0f / log_range, float(tiles_x), float(tiles_y)};
        gfx::set_uniform(local_grid_program_.u_local_grid_range, grid_range);
        bgfx::dispatch(pass.id, local_grid_program_.program->native_handle(), groups_x, groups_y, 1);
        local_grid_program_.program->end();
    }
    {
        gfx::render_pass pass("Auto Exposure/Local Blur");
        local_blur_program_.program->begin();
        gfx::set_texture(local_blur_program_.s_local_exposure_mean, 0, mean_tex, point_clamp);
        gfx::set_image(1, blurred_tex->native_handle(), 0, bgfx::Access::Write, gfx::texture_format::R32F);
        // The kernel is a percentage of the VIEW width, which on the tile grid is that
        // percentage of the tile count; the shader clamps it to its own tap budget.
        const float radius_tiles = float(tiles_x) * std::max(config.local_blurred_kernel_percent, 0.0f) * 0.01f;
        const float blur_params[4] = {float(tiles_x), float(tiles_y), std::max(radius_tiles, 1.0f), 0.0f};
        gfx::set_uniform(local_blur_program_.u_local_blur_params, blur_params);
        bgfx::dispatch(pass.id, local_blur_program_.program->native_handle(), groups_x, groups_y, 1);
        local_blur_program_.program->end();
    }
}

auto auto_exposure_pass::ensure_readback_queries() -> bool
{
    if(readback_queries_created_)
    {
        return true;
    }
    const auto* caps = gfx::get_caps();
    if(!caps || (caps->supported & BGFX_CAPS_OCCLUSION_QUERY) == 0)
    {
        return false;
    }
    for(auto& query : readback_queries_)
    {
        query = bgfx::createOcclusionQuery();
        if(!bgfx::isValid(query))
        {
            // The global pool is exhausted: give back what was taken and stay without a
            // readback (pre-exposure then follows the manual exposure only).
            destroy_readback_queries();
            return false;
        }
    }
    readback_queries_created_ = true;
    return true;
}

void auto_exposure_pass::destroy_readback_queries()
{
    for(auto& query : readback_queries_)
    {
        if(bgfx::isValid(query))
        {
            bgfx::destroy(query);
        }
        query = BGFX_INVALID_HANDLE;
    }
    readback_queries_created_ = false;
    readback_value_ = 1.0f;
}

void auto_exposure_pass::submit_exposure_readback(gfx::render_view& rview)
{
    auto exposure_tex = rview.tex_get(exposure_key);
    if(!exposure_tex || !readback_program_.program || !ensure_readback_queries())
    {
        return;
    }

    auto& target = rview.fbo_get_or_emplace(readback_target_key);
    if(!target)
    {
        auto target_tex = std::make_shared<gfx::texture>(1, 1, false, 1, gfx::texture_format::RGBA8, BGFX_TEXTURE_RT);
        target = std::make_shared<gfx::frame_buffer>();
        target->populate({target_tex});
    }

    const std::uint32_t tag = readback_submissions_ & ((1u << readback_tag_bits) - 1u);
    ++readback_submissions_;

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
        gfx::set_state(topology | BGFX_STATE_WRITE_RGB);
        gfx::submit(pass.id, readback_program_.program->native_handle(), readback_queries_[slot]);
    }
    gfx::set_state(BGFX_STATE_DEFAULT);
    readback_program_.program->end();
}

auto auto_exposure_pass::resolve_exposure_readback() -> float
{
    if(!readback_queries_created_)
    {
        return readback_value_;
    }

    std::array<bool, readback_query_count> bits{};
    for(std::uint32_t slot = 0; slot < readback_query_count; ++slot)
    {
        const auto result = bgfx::getResult(readback_queries_[slot]);
        if(result == bgfx::OcclusionQueryResult::NoResult)
        {
            return readback_value_;
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
        return readback_value_;
    }

    std::uint32_t code = 0;
    for(std::uint32_t bit = 0; bit < readback_code_bits; ++bit)
    {
        code |= std::uint32_t(bits[readback_tag_bits + bit]) << bit;
    }
    const float max_code = float((1u << readback_code_bits) - 1u);
    const float stops_per_code = (readback_max_log2 - readback_min_log2) / max_code;
    readback_value_ = std::pow(2.0f, readback_min_log2 + float(code) * stops_per_code);
    return readback_value_;
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
    rview.tex_remove(local_mean_key);
    rview.tex_remove(local_blurred_key);
    rview.fbo_remove(readback_target_key);
    rview.data_get_or_emplace(snap_key, 1u) = 1u;
    destroy_readback_queries();
}

} // namespace unravel
