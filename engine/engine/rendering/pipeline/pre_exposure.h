#pragma once

#include <array>

namespace unravel
{

/**
 * @brief A view's scene-color pre-exposure for one frame (UE View.PreExposure).
 *
 * Every pass that writes scene lighting into the frame's HDR buffers multiplies it by
 * @ref value, so those buffers hold values near the displayed range and every threshold that
 * acts on them tracks the exposure. Post-processing divides it back out. Temporal histories
 * were written under the previous frame's scale and are multiplied by
 * @ref get_history_correction when read (UE PrevViewInfo.SceneColorPreExposure). Persistent GI
 * stores keep their own fixed scale (GI_CACHED_LIGHTING_PRE_EXPOSURE, UE
 * r.EyeAdaptation.CachedLightingPreExposure).
 */
struct pre_exposure_state
{
    /// Key of the per-view state in gfx::render_view::data().
    static constexpr const char* view_key = "PRE_EXPOSURE";
    /// Range a computed pre-exposure is clamped to (2^-30 .. 2^16); keeps the inverse finite.
    static constexpr float min_value = 9.3132257e-10f;
    static constexpr float max_value = 65536.0f;

    /// This frame's scale on scene color.
    float value = 1.0f;
    /// The scale the previous frame of this view rendered with.
    float previous = 1.0f;

    /// 1 / value: turns pre-exposed color back into scene-referred color.
    auto get_inverse() const -> float
    {
        return 1.0f / value;
    }

    /// value / previous: rescales history written last frame into this frame's scale.
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
};

} // namespace unravel
