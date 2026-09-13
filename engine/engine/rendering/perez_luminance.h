#pragma once

#include <math/math.h>

namespace unravel
{

/// THE Perez -> engine conversion. The Perez tables produce luminance in their own
/// (physical-ish) scale; every consumer of that data -- the atmospheric sky dome, the
/// irradiance SH bake, the flat sky ambient -- must scale it by this ONE constant so
/// their ratios hold by construction. Absolute magnitude is arbitrary (auto exposure
/// is scale-invariant); what this buys is that retuning the sky can never desync the
/// ambient it feeds. Do not introduce per-consumer copies of this factor.
constexpr float perez_luminance_to_engine = 0.1f;

/// Horizon dimming of the exposition: the luminance tables read hot at low sun, so the
/// conversion fades to this fraction at the horizon (1.0 at zenith). Art-directed, but
/// shared: the sky dome and the ambient dim together.
constexpr float perez_horizon_dim = 0.6f;

/// UI parity between the ANALYTIC sky ambient (physical-ish, exposition-scaled) and
/// DISPLAY-REFERRED cubemap ambient at the same user-facing intensity slider. Cubemap
/// content is arbitrary (pre-baked to roughly [0,1]), so this cannot be derived -- it is
/// a documented calibration, kept in exactly one place. Applies to the LEGACY fixed
/// conversion only: on the sun-relative path below the slider's 1.0 already means the
/// calibrated sky, so no parity factor is folded in.
constexpr float sky_ambient_cubemap_parity = 2.0f;

/// The sky's horizontal irradiance as a fraction of the directional light's luminous
/// intensity: E_sky(up) = ratio x I_sun. A clear sky delivers 10-20 klux against ~100 klux
/// of direct sun on a normal surface (ratio 0.1-0.2); 0.2 is the bright end. With this the
/// sun and the sky come from ONE knob, the directional light's intensity: the Perez
/// exposition is solved per frame so the SH bake's horizontal irradiance lands exactly here
/// (compute_irradiance_perez_params), and the sky dome, the world probes' sky texels and
/// the reflection captures share that exposition. Without a directional light the fixed
/// conversion above applies unchanged. Measured 2026-09-12 (audit section 2): the fixed
/// conversion gave a sun/sky ratio of 19 on the Sponza courtyard, 3-4x darker shade than
/// daylight, and its value was unrelated to the sun's intensity by construction. The bake
/// measured 0.21 x I_sun there (linear in the sun, stable under a sun pitch of +-10
/// degrees): the GPU bake integrates about 5 percent more than the transcription below
/// for the same sky, cloud coupling off - not root-caused; the transcription is what the
/// dome follows.
constexpr float perez_sky_to_sun_ratio = 0.2f;

/// Sun altitude (sine of the elevation) over which the day/night ramp of the sky ambient
/// rises from 0 at the horizon to 1: about 20 degrees. One ramp for the SH bake's
/// sun_weight and for the fade of the sun-relative exposition into the fixed conversion.
constexpr float perez_sun_weight_altitude = 0.35f;

/**
 * @brief The one conversion from Perez luminance to engine units, including the
 * altitude-dependent horizon dimming. Both the sky dome and the irradiance bake use this.
 * @param sun_altitude Sun elevation, -1..1 (positive = above horizon).
 */
auto compute_perez_exposition(float sun_altitude) -> float;

/**
 * @brief The day/night ramp of the sky ambient: 0 at the horizon, 1 from about 20 degrees
 * of elevation (smoothstep over perez_sun_weight_altitude).
 * @param sun_altitude Sun elevation, -1..1 (positive = above horizon).
 */
auto compute_perez_sun_weight(float sun_altitude) -> float;

/**
 * @brief Computes Perez sky and sun luminance from light direction (time-of-day).
 * Uses the same tables as the atmospheric pass for consistency.
 * @param light_direction Normalized sun direction (points toward sun).
 * @param out_sky_luminance_rgb Output sky zenith luminance in RGB.
 * @param out_sun_luminance_rgb Output sun luminance in RGB.
 */
void compute_perez_luminance(const math::vec3& light_direction,
                             math::vec3& out_sky_luminance_rgb,
                             math::vec3& out_sun_luminance_rgb);

/**
 * @brief Full Perez params for irradiance SH compute shader (mode 1).
 */
struct irradiance_perez_params
{
    math::vec3 sun_direction;
    math::vec3 sky_luminance_rgb;
    math::vec3 sun_luminance_rgb;
    math::vec3 sky_luminance_xyz;
    float exposition;
    float perez_coeff[5][4];
    /// The bake's horizontal irradiance at unit exposition (compute_perez_horizontal_irradiance).
    float horizontal_irradiance_unit = 0.0f;
    /// True when @ref exposition follows the directional light (perez_sky_to_sun_ratio); the
    /// ambient path then skips sky_ambient_cubemap_parity.
    bool sun_relative = false;
};

/**
 * @brief The horizontal irradiance the irradiance SH bake produces from this sky at unit
 * exposition: a CPU transcription of cs_irradiance_sh.sc mode 1 (the same 64 Hammersley
 * hemisphere samples, the same Perez chain, the SH bands the bake stores, read back at the
 * zenith through eval_irradiance_sh's cosine lobe). Clear sky only - the bake's cloud
 * coverage blend darkens it further, as an overcast sky should. Keep in step with the shader.
 */
auto compute_perez_horizontal_irradiance(const irradiance_perez_params& perez) -> float;

/**
 * @brief Fills the Perez parameters for the SH bake and the sky dome.
 * @param sun_intensity The directional light's luminous intensity in engine units (0 = no
 *        sun: the fixed conversion). See perez_sky_to_sun_ratio.
 */
void compute_irradiance_perez_params(const math::vec3& light_direction,
                                     float turbidity,
                                     float sun_intensity,
                                     irradiance_perez_params& out);

} // namespace unravel
