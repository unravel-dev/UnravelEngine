#pragma once

#include <math/math.h>

#include <algorithm>
#include <cmath>

namespace unravel
{

/// UE 5.8's default Earth atmosphere (USkyAtmosphereComponent constructor), distances in km, coefficients per km.
namespace atmosphere_defaults
{
constexpr float bottom_radius_km = 6360.0f;
constexpr float top_radius_km = 6420.0f;
/// The transmittance origin sits this far above the ground (UE GetTransmittanceAtGroundLevel).
constexpr float origin_altitude_km = 0.5f;
constexpr float rayleigh_scale_height_km = 8.0f;
constexpr float mie_scale_height_km = 1.2f;
constexpr float rayleigh_scattering_r = 0.005802f;
constexpr float rayleigh_scattering_g = 0.013558f;
constexpr float rayleigh_scattering_b = 0.033100f;
constexpr float mie_extinction = 0.003996f + 0.000444f;
constexpr float ozone_absorption_r = 0.000650f;
constexpr float ozone_absorption_g = 0.001881f;
constexpr float ozone_absorption_b = 0.000085f;
/// Ozone density: a tent peaking at 1 at this altitude and falling to 0 this far either side of it.
constexpr float ozone_tip_altitude_km = 25.0f;
constexpr float ozone_half_width_km = 15.0f;
/// Optical-depth samples along the ray to the top of the atmosphere (UE uses 15).
constexpr int transmittance_samples = 15;
} // namespace atmosphere_defaults

/**
 * @brief Transmittance of sunlight through UE's default atmosphere, seen from the ground (UE
 * FAtmosphereSetup::GetTransmittanceAtGroundLevel). A directional light flagged as the atmosphere sun is
 * multiplied by it, which is what makes UE's sunlight warm, and darker and redder towards the horizon.
 * @param direction_to_sun Unit vector towards the sun, +Y up.
 * @return Linear RGB transmittance in [0, 1]; near 0 once the sun is below the horizon.
 */
inline auto compute_atmosphere_sun_transmittance(const math::vec3& direction_to_sun) -> math::vec3
{
    namespace ad = atmosphere_defaults;
    const float elevation_sin = std::clamp(direction_to_sun.y, -1.0f, 1.0f);
    const float elevation_cos = std::sqrt(std::max(0.0f, 1.0f - elevation_sin * elevation_sin));
    // 2D: the transmittance is symmetric about the zenith, so only the elevation matters.
    const float origin_x = 0.0f;
    const float origin_y = ad::bottom_radius_km + ad::origin_altitude_km;
    const float b = origin_y * elevation_sin;
    const float c = origin_y * origin_y - ad::top_radius_km * ad::top_radius_km;
    const float ray_length = -b + std::sqrt(std::max(0.0f, b * b - c));
    const float step = ray_length / float(ad::transmittance_samples);
    math::vec3 optical_depth(0.0f);
    for(int i = 0; i < ad::transmittance_samples; ++i)
    {
        // UE samples at the start of each step.
        const float t = step * float(i);
        const float x = origin_x + elevation_cos * t;
        const float y = origin_y + elevation_sin * t;
        const float height = std::sqrt(x * x + y * y) - ad::bottom_radius_km;
        const float rayleigh_density = std::exp(-height / ad::rayleigh_scale_height_km);
        const float mie_density = std::exp(-height / ad::mie_scale_height_km);
        const float ozone_density =
            std::clamp(1.0f - std::abs(height - ad::ozone_tip_altitude_km) / ad::ozone_half_width_km, 0.0f, 1.0f);
        optical_depth += step * (rayleigh_density * math::vec3(ad::rayleigh_scattering_r,
                                                               ad::rayleigh_scattering_g,
                                                               ad::rayleigh_scattering_b) +
                                 mie_density * math::vec3(ad::mie_extinction) +
                                 ozone_density * math::vec3(ad::ozone_absorption_r,
                                                            ad::ozone_absorption_g,
                                                            ad::ozone_absorption_b));
    }
    return math::vec3(std::exp(-optical_depth.x), std::exp(-optical_depth.y), std::exp(-optical_depth.z));
}

} // namespace unravel
