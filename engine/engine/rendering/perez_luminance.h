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
/// a documented calibration, kept in exactly one place.
constexpr float sky_ambient_cubemap_parity = 2.0f;

/**
 * @brief The one conversion from Perez luminance to engine units, including the
 * altitude-dependent horizon dimming. Both the sky dome and the irradiance bake use this.
 * @param sun_altitude Sun elevation, -1..1 (positive = above horizon).
 */
auto compute_perez_exposition(float sun_altitude) -> float;

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
};

void compute_irradiance_perez_params(const math::vec3& light_direction,
                                     float turbidity,
                                     irradiance_perez_params& out);

} // namespace unravel
