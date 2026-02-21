#pragma once

#include <math/math.h>

namespace unravel
{

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
