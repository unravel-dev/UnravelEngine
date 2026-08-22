#include "light_component.h"
namespace unravel
{
const light& light_component::get_light() const
{
    return light_;
}

void light_component::set_light(const light& l)
{
    light_ = l;
}

auto light_component::get_bounds_sphere_impl(const math::vec3* light_direction) const -> math::bsphere
{
    math::bsphere result;

    if(light_.type == light_type::point)
    {
        result = math::bsphere(math::vec3(0.0f, 0.0f, 0.0f), light_.point_data.range);
    }
    else if(light_.type == light_type::spot)
    {
        float range = light_.spot_data.get_range();

        if(light_direction)
        {
            float clamped_inner_cone_angle =
                math::radians(math::clamp(light_.spot_data.get_inner_angle(), 0.0f, 89.0f));
            float clamped_outer_cone_angle = math::clamp(math::radians(light_.spot_data.get_outer_angle()),
                                                         clamped_inner_cone_angle + 0.001f,
                                                         math::radians(89.0f) + 0.001f);
            float cos_outer_cone = math::cos(clamped_outer_cone_angle);
            // Use the law of cosines to find the distance to the furthest edge of the
            // spotlight cone from a
            // position that is halfway down the spotlight direction
            const float radius = math::sqrt(1.25f * range * range - range * range * cos_outer_cone);
            math::vec3 center = math::vec3(0.0f, 0.0f, 0.0f) + 0.5f * (*light_direction) * range;

            result = math::bsphere(center, radius);
        }
        else
        {
            result = math::bsphere(math::vec3(0.0f, 0.0f, 0.0f), range);
        }
    }
    else
    {
        result = math::bsphere(math::vec3(0.0f, 0.0f, 0.0f), 999999999.0f);
    }

    return result;
}

auto light_component::get_bounds_sphere() const -> math::bsphere
{
    return get_bounds_sphere_impl(nullptr);
}

auto light_component::get_bounds_sphere_precise(const math::vec3& light_direction) const -> math::bsphere
{
    return get_bounds_sphere_impl(&light_direction);
}

auto light_component::get_bounds() const -> math::bbox
{
    auto sphere = get_bounds_sphere();
    math::bbox result;
    result.from_sphere(sphere.position, sphere.radius);
    return result;
}

auto light_component::get_bounds_precise(const math::vec3& light_direction) const -> math::bbox
{
    auto sphere = get_bounds_sphere_precise(light_direction);
    math::bbox result;
    result.from_sphere(sphere.position, sphere.radius);
    return result;
}

int light_component::compute_projected_sphere_rect(irect32_t& rect,
                                                   const math::vec3& light_position,
                                                   const math::vec3& light_direction,
                                                   const math::vec3& view_origin,
                                                   const math::transform& view,
                                                   const math::transform& proj)
{
    if(light_.type == light_type::point)
    {
        return math::compute_projected_sphere_rect(rect.left,
                                                   rect.right,
                                                   rect.top,
                                                   rect.bottom,
                                                   light_position,
                                                   light_.point_data.range,
                                                   view_origin,
                                                   view,
                                                   proj);
    }
    else if(light_.type == light_type::spot)
    {
        float range = light_.spot_data.get_range();
        float clamped_inner_cone_angle = math::radians(math::clamp(light_.spot_data.get_inner_angle(), 0.0f, 89.0f));
        float clamped_outer_cone_angle = math::clamp(math::radians(light_.spot_data.get_outer_angle()),
                                                     clamped_inner_cone_angle + 0.001f,
                                                     math::radians(89.0f) + 0.001f);
        float cos_outer_cone = math::cos(clamped_outer_cone_angle);
        // Use the law of cosines to find the distance to the furthest edge of the
        // spotlight cone from a
        // position that is halfway down the spotlight direction
        const float radius = math::sqrt(1.25f * range * range - range * range * cos_outer_cone);
        math::vec3 center = light_position + 0.5f * light_direction * range;

        return math::compute_projected_sphere_rect(rect.left,
                                                   rect.right,
                                                   rect.top,
                                                   rect.bottom,
                                                   center,
                                                   radius,
                                                   view_origin,
                                                   view,
                                                   proj);
    }
    else
    {
        return 1;
    }
}

auto light_component::get_shadowmap_generator() -> shadow::shadowmap_generator&
{
    return *shadowmap_generator_;
}

auto skylight_component::get_mode() const noexcept -> const sky_mode&
{
    return mode_;
}
void skylight_component::set_mode(const sky_mode& mode)
{
    mode_ = mode;
}

auto skylight_component::get_turbidity() const noexcept -> float
{
    return turbidity_;
}

void skylight_component::set_turbidity(float turbidity)
{
    turbidity_ = math::clamp(turbidity, 1.9f, 10.0f);
}

auto skylight_component::get_cloud_mode() const noexcept -> skylight_component::cloud_mode
{
    return cloud_mode_;
}

void skylight_component::set_cloud_mode(cloud_mode mode)
{
    cloud_mode_ = mode;
}

auto skylight_component::get_cloud_coverage() const noexcept -> float
{
    return cloud_coverage_;
}

void skylight_component::set_cloud_coverage(float coverage)
{
    cloud_coverage_ = math::clamp(coverage, 0.0f, 1.0f);
}

auto skylight_component::get_cloud_base_altitude() const noexcept -> float
{
    return cloud_base_altitude_;
}

void skylight_component::set_cloud_base_altitude(float altitude)
{
    cloud_base_altitude_ = math::max(altitude, 100.0f);
}

auto skylight_component::get_cloud_thickness() const noexcept -> float
{
    return cloud_thickness_;
}

void skylight_component::set_cloud_thickness(float thickness)
{
    cloud_thickness_ = math::clamp(thickness, 100.0f, 100000.0f);
}

auto skylight_component::get_cloud_size() const noexcept -> float
{
    return cloud_size_;
}

void skylight_component::set_cloud_size(float size)
{
    cloud_size_ = math::clamp(size, 100.0f, 1000000.0f);
}

auto skylight_component::get_cloud_softness() const noexcept -> float
{
    return cloud_softness_;
}

void skylight_component::set_cloud_softness(float softness)
{
    cloud_softness_ = math::clamp(softness, 0.01f, 0.8f);
}

auto skylight_component::get_cloud_detail_erode() const noexcept -> float
{
    return cloud_detail_erode_;
}

void skylight_component::set_cloud_detail_erode(float erode)
{
    cloud_detail_erode_ = math::clamp(erode, 0.0f, 2.0f);
}

auto skylight_component::get_cloud_macro_variation() const noexcept -> float
{
    return cloud_macro_variation_;
}

void skylight_component::set_cloud_macro_variation(float variation)
{
    cloud_macro_variation_ = math::clamp(variation, 0.0f, 1.5f);
}

auto skylight_component::get_cloud_speed() const noexcept -> float
{
    return cloud_speed_;
}

void skylight_component::set_cloud_speed(float speed)
{
    cloud_speed_ = math::clamp(speed, 0.0f, 200.0f);
}

auto skylight_component::get_cloud_wind_direction() const noexcept -> float
{
    return cloud_wind_direction_;
}

void skylight_component::set_cloud_wind_direction(float degrees)
{
    cloud_wind_direction_ = math::mod(degrees, 360.0f);
}

auto skylight_component::get_cloud_density() const noexcept -> float
{
    return cloud_density_;
}

void skylight_component::set_cloud_density(float density)
{
    cloud_density_ = math::max(density, 0.0f);
}

auto skylight_component::get_cloud_shadow_strength() const noexcept -> float
{
    return cloud_shadow_strength_;
}

void skylight_component::set_cloud_shadow_strength(float strength)
{
    cloud_shadow_strength_ = math::clamp(strength, 0.02f, 1.0f);
}

auto skylight_component::get_irradiance_intensity() const noexcept -> float
{
    return irradiance_intensity_;
}

void skylight_component::set_irradiance_intensity(float intensity)
{
    irradiance_intensity_ = math::max(intensity, 0.0f);
}

auto skylight_component::get_irradiance_tint() const noexcept -> const math::color&
{
    return irradiance_tint_;
}

void skylight_component::set_irradiance_tint(const math::color& color)
{
    irradiance_tint_ = color;
}

auto skylight_component::get_irradiance_quality() const noexcept -> irradiance_quality
{
    return irradiance_quality_;
}

void skylight_component::set_irradiance_quality(irradiance_quality quality)
{
    irradiance_quality_ = quality;
}

auto skylight_component::get_irradiance_use_sky() const noexcept -> bool
{
    return irradiance_use_sky_;
}

void skylight_component::set_irradiance_use_sky(bool use_sky)
{
    irradiance_use_sky_ = use_sky;
}

auto skylight_component::get_sky_brightness() const noexcept -> float
{
    return sky_brightness_;
}

void skylight_component::set_sky_brightness(float brightness)
{
    sky_brightness_ = math::max(brightness, 0.0f);
}

} // namespace unravel
