#pragma once

#include <engine/rendering/default_textures.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/graphics.h>
#include <math/math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace unravel
{

/**
 * @brief Everything the per-pixel local exposure needs (auto_exposure_pass owns the two lookups
 *        and the settings; the tonemapper and the bloom only apply them).
 *
 * Inactive - the default - when the exposure pass did not build a grid, which is exactly when the
 * settings are neutral (auto_exposure_pass::settings::is_local_exposure_enabled).
 */
struct local_exposure_params
{
    /// Flattened bilateral grid: texel (tile_x * slices + slice, tile_y), rg = the slice's raw sum
    /// of log2 luminance and of weight.
    gfx::texture::ptr grid;
    /// The 1/32 resolution Gaussian of the log luminance, the edge-blind level.
    gfx::texture::ptr blurred;
    float tiles_x = 0.0f;
    float tiles_y = 0.0f;
    /// Screen uv -> grid / blurred uv: the share of each the view covers.
    math::vec2 grid_uv_scale{1.0f, 1.0f};
    math::vec2 blurred_uv_scale{1.0f, 1.0f};
    float slices = 0.0f;
    /// The grid's luminance axis, matching the histogram's.
    float min_log_lum = 0.0f;
    float log_lum_range = 1.0f;
    float highlight_contrast = 1.0f;
    float shadow_contrast = 1.0f;
    float detail_strength = 1.0f;
    float blurred_blend = 0.6f;
    float middle_grey_bias = 0.0f;

    auto is_active() const -> bool
    {
        return grid && blurred && tiles_x > 0.0f && tiles_y > 0.0f && slices > 0.0f;
    }
};

/**
 * @brief The uniforms and samplers of exposure/local_exposure.sh, for every program that includes
 *        it. Cache them before creating the program (the uniforms_cache order contract).
 */
struct local_exposure_uniforms : uniforms_cache
{
    /// Sampler stages local_exposure.sh declares; its includers keep them free.
    static constexpr std::uint8_t grid_stage = 2;
    static constexpr std::uint8_t blurred_stage = 3;

    void cache_uniforms()
    {
        cache_uniform(nullptr, u_local_exposure, "u_local_exposure", bgfx::UniformType::Vec4);
        cache_uniform(nullptr, u_local_exposure2, "u_local_exposure2", bgfx::UniformType::Vec4);
        cache_uniform(nullptr, u_local_exposure3, "u_local_exposure3", bgfx::UniformType::Vec4);
        cache_uniform(nullptr, u_local_exposure4, "u_local_exposure4", bgfx::UniformType::Vec4);
        cache_uniform(nullptr, s_local_exposure_grid, "s_local_exposure_grid", bgfx::UniformType::Sampler);
        cache_uniform(nullptr, s_local_exposure_blurred, "s_local_exposure_blurred", bgfx::UniformType::Sampler);
    }

    /**
     * @brief Binds the lookups and the shape for the next draw. An inactive @p params binds 1x1
     *        stand-ins and the off switch, so the shader's local exposure is exactly 1.
     *
     * @param pre_exposure The pre-exposure the draw's input carries.
     */
    void submit(const local_exposure_params& params, float pre_exposure) const
    {
        // The grid is point-fetched (the shader gathers the flattened layout itself); the blurred
        // level is filtered bilinearly.
        constexpr std::uint64_t grid_sampler_flags = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        constexpr std::uint64_t blurred_sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        const bool active = params.is_active();
        const auto stand_in = default_textures::get().black_texture();
        gfx::set_texture(s_local_exposure_grid, grid_stage, active ? params.grid : stand_in, grid_sampler_flags);
        gfx::set_texture(s_local_exposure_blurred,
                         blurred_stage,
                         active ? params.blurred : stand_in,
                         blurred_sampler_flags);
        const float shape[4] = {params.highlight_contrast,
                                params.shadow_contrast,
                                params.detail_strength,
                                params.blurred_blend};
        gfx::set_uniform(u_local_exposure, shape);
        const float axis[4] = {std::log2(std::max(pre_exposure, 1e-12f)),
                               params.middle_grey_bias,
                               active ? 1.0f : 0.0f,
                               params.min_log_lum};
        gfx::set_uniform(u_local_exposure2, axis);
        const float layout[4] = {1.0f / std::max(params.log_lum_range, 1e-4f), params.slices, params.tiles_x, params.tiles_y};
        gfx::set_uniform(u_local_exposure3, layout);
        const float uv_scales[4] = {params.grid_uv_scale.x,
                                    params.grid_uv_scale.y,
                                    params.blurred_uv_scale.x,
                                    params.blurred_uv_scale.y};
        gfx::set_uniform(u_local_exposure4, uv_scales);
    }

    gfx::program::uniform_ptr u_local_exposure;
    gfx::program::uniform_ptr u_local_exposure2;
    gfx::program::uniform_ptr u_local_exposure3;
    gfx::program::uniform_ptr u_local_exposure4;
    gfx::program::uniform_ptr s_local_exposure_grid;
    gfx::program::uniform_ptr s_local_exposure_blurred;
};

} // namespace unravel
