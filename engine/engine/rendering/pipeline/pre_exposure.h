#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace unravel
{

/**
 * @brief A view's scene-color pre-exposure for one frame.
 *
 * Every pass that writes scene lighting into the frame's HDR buffers multiplies it by the
 * pre-exposure, so those buffers hold values near the displayed range and every threshold that
 * acts on them tracks the exposure. Post-processing divides it back out. Temporal histories
 * were written under the previous frame's scale and are multiplied by the history correction
 * when read. Persistent GI stores keep their own fixed scale (GI_CACHED_LIGHTING_PRE_EXPOSURE),
 * so their contents stay valid when the exposure moves.
 *
 * The pre-exposure is @ref value, the CPU's share (the manual exposure), times the GPU's share, the
 * adapted exposure, which never reaches the CPU. A pass at the start of each run writes the GPU's share
 * from the adapted exposure the last frame left in AUTO_EXPOSURE into the view's environment SH
 * (IRRADIANCE_SH), as the texel after the nine coefficients (cs_pre_exposure.sc), and every shader that
 * writes or reads scene lighting multiplies it in (pre_exposure.sh, @ref bind).
 */
struct pre_exposure_state
{
    /// Key of the per-view state in gfx::render_view::data().
    static constexpr const char* view_key = "PRE_EXPOSURE";
    /// Range a computed pre-exposure is clamped to (2^-30 .. 2^16); keeps the inverse finite.
    static constexpr float min_value = 9.3132257e-10f;
    static constexpr float max_value = 65536.0f;
    /// The stage pre_exposure.sh declares s_pre_exposure at unless the shader sets PRE_EXPOSURE_STAGE.
    static constexpr std::uint8_t default_stage = 15;
    /// The texel of the view's environment SH that holds the GPU's share, after the nine coefficients
    /// (PRE_EXPOSURE_GPU_TEXEL of pre_exposure.sh and cs_pre_exposure.sc).
    static constexpr std::uint16_t gpu_texel = 9;

    /// The CPU's share of this frame's scale on scene color: the manual exposure.
    float value = 1.0f;
    /// The CPU's share the previous frame of this view rendered with.
    float previous = 1.0f;
    /// The view's environment SH (IRRADIANCE_SH), whose texel @ref gpu_texel is the GPU's share
    /// (pre_exposure.sh, s_pre_exposure). Shaders that sample the SH anyway read the share there.
    bgfx::TextureHandle gpu_texture = BGFX_INVALID_HANDLE;
    /// The s_pre_exposure sampler uniform.
    bgfx::UniformHandle gpu_sampler = BGFX_INVALID_HANDLE;

    /// 1 / value: the CPU's share of the scale that turns pre-exposed color back into scene-referred color.
    auto get_inverse() const -> float
    {
        return 1.0f / value;
    }

    /// value / previous: the CPU's share of the rescale of history written last frame into this frame's scale.
    auto get_history_correction() const -> float
    {
        return value / previous;
    }

    /// Shader layout of u_pre_exposure (pre_exposure.sh): x = value, y = 1 / value,
    /// z = value / previous, w = previous.
    auto to_uniform() const -> std::array<float, 4>
    {
        return {value, get_inverse(), get_history_correction(), previous};
    }

    /**
     * @brief Binds the GPU's share (s_pre_exposure) at @p stage for the next draw or dispatch.
     *
     * @p stage is the one the shader declares it at (PRE_EXPOSURE_STAGE, @ref default_stage when the
     * shader sets none).
     */
    void bind(std::uint8_t stage = default_stage) const
    {
        if(bgfx::isValid(gpu_texture) && bgfx::isValid(gpu_sampler))
        {
            bgfx::setTexture(stage, gpu_sampler, gpu_texture, BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
        }
    }
};

} // namespace unravel
