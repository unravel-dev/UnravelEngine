#include "atmosphere_sky_irradiance.h"

#include <engine/rendering/atmosphere_transmittance.h>
#include <math/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace unravel
{
// Named rather than anonymous: unity builds merge translation units, and these helpers must not meet another file's.
namespace atmosphere_sky_detail
{
namespace ad = atmosphere_defaults;

constexpr float pi = 3.14159265358979f;
/// UE's default Mie scattering (the extinction in atmosphere_defaults adds the absorption), anisotropy and ground albedo.
constexpr float mie_scattering = 0.003996f;
constexpr float mie_anisotropy = 0.8f;
constexpr float ground_albedo = 0.4f;
/// The sky light's capture height above the ground (UE PlanetRadiusOffset, 5 m).
constexpr float observer_altitude_km = 0.005f;
/// UE's rendered sky lights a horizontal surface with 0.085 x the outside sun at a 50 degree sun (17 directions matched
/// at a fixed exposure against this engine's sky); this model gives 0.073 there. The factor carries the difference,
/// mostly UE's brighter sky near the horizon, while the model supplies how the ratio moves with the sun.
constexpr float ue_sky_irradiance_calibration = 1.16f;

/// Transmittance table over height (squared spacing, denser near the ground) and view zenith cosine.
constexpr int transmittance_heights = 32;
constexpr int transmittance_cosines = 64;
/// Multi-scattering table over height and sun zenith cosine, and its sphere of directions per entry.
constexpr int multi_scattering_heights = 16;
constexpr int multi_scattering_cosines = 16;
constexpr int multi_scattering_directions = 64;
constexpr int multi_scattering_steps = 20;
/// Sky irradiance integration: hemisphere grid and ray steps per direction.
constexpr int irradiance_zenith_steps = 12;
constexpr int irradiance_azimuth_steps = 24;
constexpr int irradiance_ray_steps = 24;
/// Table of the result over the sine of the sun's elevation, [0, 1].
constexpr int ratio_entries = 33;

const math::vec3 luminance_weights(0.2126f, 0.7152f, 0.0722f);

struct medium_sample
{
    math::vec3 rayleigh_scattering;
    float mie_scattering = 0.0f;
    math::vec3 extinction;
};

auto sample_medium(float height_km) -> medium_sample
{
    medium_sample m;
    const float rayleigh_density = std::exp(-height_km / ad::rayleigh_scale_height_km);
    const float mie_density = std::exp(-height_km / ad::mie_scale_height_km);
    const float ozone_density =
        std::clamp(1.0f - std::abs(height_km - ad::ozone_tip_altitude_km) / ad::ozone_half_width_km, 0.0f, 1.0f);
    m.rayleigh_scattering =
        rayleigh_density * math::vec3(ad::rayleigh_scattering_r, ad::rayleigh_scattering_g, ad::rayleigh_scattering_b);
    m.mie_scattering = mie_density * mie_scattering;
    m.extinction = m.rayleigh_scattering + math::vec3(mie_density * ad::mie_extinction) +
                   ozone_density * math::vec3(ad::ozone_absorption_r, ad::ozone_absorption_g, ad::ozone_absorption_b);
    return m;
}

/// Distance along the ray to the sphere of @p radius around the planet centre, or a negative value when it misses.
auto intersect_sphere(const math::vec3& origin, const math::vec3& direction, float radius) -> float
{
    const float b = math::dot(origin, direction);
    const float c = math::dot(origin, origin) - radius * radius;
    const float discriminant = b * b - c;
    if(discriminant < 0.0f)
    {
        return -1.0f;
    }
    const float root = std::sqrt(discriminant);
    const float near_t = -b - root;
    const float far_t = -b + root;
    if(far_t < 0.0f)
    {
        return -1.0f;
    }
    return near_t > 0.0f ? near_t : far_t;
}

auto hits_ground(const math::vec3& origin, const math::vec3& direction) -> bool
{
    return math::dot(origin, direction) < 0.0f && intersect_sphere(origin, direction, ad::bottom_radius_km) > 0.0f;
}

auto exp3(const math::vec3& v) -> math::vec3
{
    return math::vec3(std::exp(v.x), std::exp(v.y), std::exp(v.z));
}

/// Precomputed transmittance to the top of the atmosphere and multi-scattering, then the irradiance table.
class sky_model
{
public:
    sky_model()
    {
        build_transmittance();
        build_multi_scattering();
        build_ratios();
    }

    auto get_ratio(float sun_elevation_sin) const -> float
    {
        const float x = std::clamp(sun_elevation_sin, 0.0f, 1.0f) * float(ratio_entries - 1);
        const int i0 = std::min(int(x), ratio_entries - 2);
        const float f = x - float(i0);
        return ratios_[i0] * (1.0f - f) + ratios_[i0 + 1] * f;
    }

private:
    void build_transmittance()
    {
        transmittance_.resize(size_t(transmittance_heights) * transmittance_cosines);
        for(int h = 0; h < transmittance_heights; ++h)
        {
            const float u = float(h) / float(transmittance_heights - 1);
            const float radius = ad::bottom_radius_km + (ad::top_radius_km - ad::bottom_radius_km) * u * u;
            for(int c = 0; c < transmittance_cosines; ++c)
            {
                const float cosine = -1.0f + 2.0f * float(c) / float(transmittance_cosines - 1);
                const math::vec3 direction(std::sqrt(std::max(0.0f, 1.0f - cosine * cosine)), cosine, 0.0f);
                transmittance_[size_t(h) * transmittance_cosines + c] =
                    integrate_transmittance(math::vec3(0.0f, radius, 0.0f), direction);
            }
        }
    }

    auto integrate_transmittance(const math::vec3& origin, const math::vec3& direction) const -> math::vec3
    {
        if(hits_ground(origin, direction))
        {
            return math::vec3(0.0f);
        }
        const float length = intersect_sphere(origin, direction, ad::top_radius_km);
        const float step = length / float(ad::transmittance_samples);
        math::vec3 optical_depth(0.0f);
        for(int i = 0; i < ad::transmittance_samples; ++i)
        {
            const math::vec3 p = origin + direction * ((float(i) + 0.5f) * step);
            optical_depth += sample_medium(math::length(p) - ad::bottom_radius_km).extinction * step;
        }
        return exp3(-optical_depth);
    }

    /// Transmittance from @p p toward the sun: 0 in the planet's shadow.
    auto sun_transmittance(const math::vec3& p, const math::vec3& sun) const -> math::vec3
    {
        if(hits_ground(p, sun))
        {
            return math::vec3(0.0f);
        }
        const float radius = math::length(p);
        const float cosine = math::dot(p / radius, sun);
        const float u = std::sqrt(std::clamp((radius - ad::bottom_radius_km) / (ad::top_radius_km - ad::bottom_radius_km), 0.0f, 1.0f));
        return sample_table(transmittance_, transmittance_heights, transmittance_cosines, u, (cosine + 1.0f) * 0.5f, false);
    }

    void build_multi_scattering()
    {
        const auto directions = fibonacci_sphere(multi_scattering_directions);
        multi_scattering_.resize(size_t(multi_scattering_heights) * multi_scattering_cosines);
        for(int h = 0; h < multi_scattering_heights; ++h)
        {
            const float radius =
                ad::bottom_radius_km + (ad::top_radius_km - ad::bottom_radius_km) * (float(h) + 0.5f) / float(multi_scattering_heights);
            for(int c = 0; c < multi_scattering_cosines; ++c)
            {
                const float cosine = -1.0f + 2.0f * (float(c) + 0.5f) / float(multi_scattering_cosines);
                const math::vec3 sun(std::sqrt(std::max(0.0f, 1.0f - cosine * cosine)), cosine, 0.0f);
                multi_scattering_[size_t(h) * multi_scattering_cosines + c] =
                    integrate_multi_scattering(math::vec3(0.0f, radius, 0.0f), sun, directions);
            }
        }
    }

    /// Hillaire 2020: second-order light with an isotropic phase, L2, and the transfer f of unit isotropic light;
    /// the infinite series of higher orders is L2 / (1 - f). The ground reflects the sun with UE's albedo.
    auto integrate_multi_scattering(const math::vec3& origin,
                                    const math::vec3& sun,
                                    const std::vector<math::vec3>& directions) const -> math::vec3
    {
        math::vec3 second_order(0.0f);
        math::vec3 transfer(0.0f);
        const float isotropic_phase = 1.0f / (4.0f * pi);
        for(const auto& direction : directions)
        {
            const float ground_t = hits_ground(origin, direction) ? intersect_sphere(origin, direction, ad::bottom_radius_km) : -1.0f;
            const float length = ground_t > 0.0f ? ground_t : intersect_sphere(origin, direction, ad::top_radius_km);
            const float step = length / float(multi_scattering_steps);
            math::vec3 view_transmittance(1.0f);
            for(int i = 0; i < multi_scattering_steps; ++i)
            {
                const math::vec3 p = origin + direction * ((float(i) + 0.5f) * step);
                const medium_sample m = sample_medium(math::length(p) - ad::bottom_radius_km);
                const math::vec3 scattering = m.rayleigh_scattering + math::vec3(m.mie_scattering);
                const math::vec3 step_transmittance = exp3(-m.extinction * step);
                const math::vec3 integral = (math::vec3(1.0f) - step_transmittance) / math::max(m.extinction, math::vec3(1e-9f));
                second_order += view_transmittance * scattering * sun_transmittance(p, sun) * isotropic_phase * integral;
                transfer += view_transmittance * scattering * integral;
                view_transmittance *= step_transmittance;
            }
            if(ground_t > 0.0f)
            {
                const math::vec3 ground = origin + direction * ground_t;
                const float sun_cosine = std::max(math::dot(math::normalize(ground), sun), 0.0f);
                second_order += view_transmittance * sun_transmittance(ground, sun) * sun_cosine * ground_albedo / pi;
            }
        }
        const float inv_count = 1.0f / float(directions.size());
        second_order *= inv_count;
        transfer *= inv_count;
        return second_order / math::max(math::vec3(1.0f) - transfer, math::vec3(1e-4f));
    }

    auto multi_scattering(const math::vec3& p, const math::vec3& sun) const -> math::vec3
    {
        const float radius = math::length(p);
        const float cosine = math::dot(p / radius, sun);
        const float u = (radius - ad::bottom_radius_km) / (ad::top_radius_km - ad::bottom_radius_km);
        return sample_table(multi_scattering_, multi_scattering_heights, multi_scattering_cosines, u, (cosine + 1.0f) * 0.5f, true);
    }

    /// Single scattering of the sun plus multi-scattering, along one view ray from the observer.
    auto integrate_sky_radiance(const math::vec3& origin, const math::vec3& direction, const math::vec3& sun) const -> math::vec3
    {
        const float length = intersect_sphere(origin, direction, ad::top_radius_km);
        const float step = length / float(irradiance_ray_steps);
        const float cosine = math::dot(direction, sun);
        const float rayleigh_phase = 3.0f / (16.0f * pi) * (1.0f + cosine * cosine);
        const float g2 = mie_anisotropy * mie_anisotropy;
        const float mie_phase =
            (1.0f - g2) / (4.0f * pi * std::pow(1.0f + g2 - 2.0f * mie_anisotropy * cosine, 1.5f));
        math::vec3 radiance(0.0f);
        math::vec3 view_transmittance(1.0f);
        for(int i = 0; i < irradiance_ray_steps; ++i)
        {
            const math::vec3 p = origin + direction * ((float(i) + 0.5f) * step);
            const medium_sample m = sample_medium(math::length(p) - ad::bottom_radius_km);
            const math::vec3 scattering = m.rayleigh_scattering + math::vec3(m.mie_scattering);
            const math::vec3 source =
                sun_transmittance(p, sun) * (m.rayleigh_scattering * rayleigh_phase + math::vec3(m.mie_scattering * mie_phase)) +
                multi_scattering(p, sun) * scattering;
            const math::vec3 step_transmittance = exp3(-m.extinction * step);
            radiance += view_transmittance * source * (math::vec3(1.0f) - step_transmittance) /
                        math::max(m.extinction, math::vec3(1e-9f));
            view_transmittance *= step_transmittance;
        }
        return radiance;
    }

    auto integrate_horizontal_irradiance(float sun_elevation_sin) const -> float
    {
        const float sun_cos = std::sqrt(std::max(0.0f, 1.0f - sun_elevation_sin * sun_elevation_sin));
        const math::vec3 sun(sun_cos, sun_elevation_sin, 0.0f);
        const math::vec3 origin(0.0f, ad::bottom_radius_km + observer_altitude_km, 0.0f);
        const float d_zenith = 0.5f * pi / float(irradiance_zenith_steps);
        const float d_azimuth = 2.0f * pi / float(irradiance_azimuth_steps);
        math::vec3 irradiance(0.0f);
        for(int z = 0; z < irradiance_zenith_steps; ++z)
        {
            const float zenith = (float(z) + 0.5f) * d_zenith;
            for(int a = 0; a < irradiance_azimuth_steps; ++a)
            {
                const float azimuth = (float(a) + 0.5f) * d_azimuth;
                const math::vec3 direction(std::sin(zenith) * std::cos(azimuth), std::cos(zenith), std::sin(zenith) * std::sin(azimuth));
                irradiance += integrate_sky_radiance(origin, direction, sun) * (std::cos(zenith) * std::sin(zenith) * d_zenith * d_azimuth);
            }
        }
        return math::dot(irradiance, luminance_weights);
    }

    void build_ratios()
    {
        for(int i = 0; i < ratio_entries; ++i)
        {
            // The horizon entry samples just above it: a sun exactly on the horizon is half below the planet's edge.
            const float elevation_sin = std::max(float(i) / float(ratio_entries - 1), 0.01f);
            ratios_[i] = ue_sky_irradiance_calibration * integrate_horizontal_irradiance(elevation_sin);
        }
    }

    static auto fibonacci_sphere(int count) -> std::vector<math::vec3>
    {
        std::vector<math::vec3> directions;
        directions.reserve(size_t(count));
        const float golden_angle = pi * (3.0f - std::sqrt(5.0f));
        for(int i = 0; i < count; ++i)
        {
            const float y = 1.0f - 2.0f * (float(i) + 0.5f) / float(count);
            const float ring = std::sqrt(std::max(0.0f, 1.0f - y * y));
            const float angle = golden_angle * float(i);
            directions.emplace_back(std::cos(angle) * ring, y, std::sin(angle) * ring);
        }
        return directions;
    }

    /// Bilinear lookup in a [heights x cosines] table at u, v in [0, 1]; cell-centred tables offset by half a cell.
    static auto sample_table(const std::vector<math::vec3>& table, int rows, int columns, float u, float v, bool is_cell_centred)
        -> math::vec3
    {
        const float offset = is_cell_centred ? 0.5f : 0.0f;
        const float scale_u = is_cell_centred ? float(rows) : float(rows - 1);
        const float scale_v = is_cell_centred ? float(columns) : float(columns - 1);
        const float x = std::clamp(u * scale_u - offset, 0.0f, float(rows - 1) - 1e-3f);
        const float y = std::clamp(v * scale_v - offset, 0.0f, float(columns - 1) - 1e-3f);
        const int x0 = int(x);
        const int y0 = int(y);
        const int x1 = std::min(x0 + 1, rows - 1);
        const int y1 = std::min(y0 + 1, columns - 1);
        const float fx = x - float(x0);
        const float fy = y - float(y0);
        const auto at = [&](int r, int c) { return table[size_t(r) * columns + c]; };
        return (at(x0, y0) * (1.0f - fx) + at(x1, y0) * fx) * (1.0f - fy) + (at(x0, y1) * (1.0f - fx) + at(x1, y1) * fx) * fy;
    }

    std::vector<math::vec3> transmittance_;
    std::vector<math::vec3> multi_scattering_;
    std::array<float, ratio_entries> ratios_{};
};

auto get_sky_model() -> const sky_model&
{
    static const sky_model model;
    return model;
}
} // namespace atmosphere_sky_detail

auto compute_atmosphere_sky_to_sun_ratio(float sun_elevation_sin) -> float
{
    return atmosphere_sky_detail::get_sky_model().get_ratio(sun_elevation_sin);
}

} // namespace unravel
