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

} // namespace unravel
