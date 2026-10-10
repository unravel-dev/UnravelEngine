#pragma once

namespace unravel
{

/**
 * @brief Horizontal irradiance of the clear sky as a fraction of the sun's illuminance outside the atmosphere, for the
 * default atmosphere (atmosphere_defaults) at the given sun elevation.
 *
 * The sky is single scattering (Rayleigh and Mie, attenuated by the transmittance toward the sun) plus a
 * multi-scattering term with the default ground albedo, integrated over the upper hemisphere with the cosine weight
 * (the lower hemisphere counts as black). The sky ambient is calibrated to it, so the sky to sun balance follows the
 * sun instead of a fixed ratio: relatively more sky at a low sun. Tabulated once per process.
 * @param sun_elevation_sin Sine of the sun's elevation; values below 0 read the horizon entry.
 * @return Luminance ratio E_sky(up) / E_sun(outside the atmosphere).
 */
auto compute_atmosphere_sky_to_sun_ratio(float sun_elevation_sin) -> float;

} // namespace unravel
