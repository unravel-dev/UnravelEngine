#include "light.h"

#include <algorithm>

namespace unravel
{
namespace
{
/// A directional light has no extent; its bounds only have to swallow any scene.
constexpr float DIRECTIONAL_LIGHT_BOUNDS_RADIUS = 999999999.0f;
/// The bounding sphere of a spot cone is derived for half angles below a right angle.
constexpr float MAX_SPOT_HALF_ANGLE_DEGREES = 89.0f;
/// Keeps the outer cone strictly wider than the inner one.
constexpr float MIN_SPOT_CONE_SPREAD_RADIANS = 0.001f;
/// outer_angle / inner_angle are FULL cone angles (the lighting shaders, the GI light buffer,
/// the shadow projection and the editor gizmo all halve them).
constexpr float SPOT_FULL_TO_HALF_ANGLE = 0.5f;
/// PCF filter radius in shadow map texels at PCF offset 1, at least one texel.
constexpr float PCF_RADIUS_TEXELS_PER_OFFSET = 2.0f;
constexpr float PCF_MIN_RADIUS_TEXELS = 1.0f;
/// Point lights pack four tetrahedron faces into one map and filter each at this fraction of the radius.
constexpr float PCF_POINT_FACE_RADIUS_SCALE = 0.25f;
/// A tent of N texels stands in for a filter disk of radius (N - 1) / 2: radii below these pick the 3x3 and the 5x5
/// tent, larger ones the 7x7.
constexpr float PCF_TENT3_MAX_RADIUS_TEXELS = 1.5f;
constexpr float PCF_TENT5_MAX_RADIUS_TEXELS = 2.5f;
/// The disk radius the 3x3 tent stands in for.
constexpr float PCF_TENT3_RADIUS_TEXELS = 1.0f;

/// The PCF filter radius in texels per unit of PCF offset on @p l's shadow map faces.
auto get_pcf_radius_texels_per_offset(const light& l) -> float
{
    const float face_scale = l.type == light_type::point ? PCF_POINT_FACE_RADIUS_SCALE : 1.0f;
    return PCF_RADIUS_TEXELS_PER_OFFSET * face_scale;
}
} // namespace

auto light::compute_world_bounds_sphere(const math::vec3& position, const math::vec3& direction) const -> math::bsphere
{
    if(type == light_type::point)
    {
        return math::bsphere(position, point_data.range);
    }
    if(type == light_type::directional)
    {
        return math::bsphere(position, DIRECTIONAL_LIGHT_BOUNDS_RADIUS);
    }
    const float range = spot_data.get_range();
    const float inner_half_angle =
        math::radians(math::clamp(spot_data.get_inner_angle() * SPOT_FULL_TO_HALF_ANGLE, 0.0f, MAX_SPOT_HALF_ANGLE_DEGREES));
    const float outer_half_angle = math::clamp(math::radians(spot_data.get_outer_angle() * SPOT_FULL_TO_HALF_ANGLE),
                                               inner_half_angle + MIN_SPOT_CONE_SPREAD_RADIANS,
                                               math::radians(MAX_SPOT_HALF_ANGLE_DEGREES) + MIN_SPOT_CONE_SPREAD_RADIANS);
    // Centred halfway down the axis; the law of cosines gives the distance from there to the
    // rim of the cone's far cap, the farthest point of the lit volume.
    const float radius = math::sqrt(1.25f * range * range - range * range * math::cos(outer_half_angle));
    return math::bsphere(position + 0.5f * range * direction, radius);
}

auto light::compute_source_axis(const math::vec3& world_y_axis) const -> math::vec3
{
    if(type == light_type::directional)
    {
        return math::vec3(0.0f);
    }
    return world_y_axis * source_length;
}

void light::spot::set_range(float r)
{
    if(r < 0)
        r = 0;

    range = r;
}

void light::spot::set_outer_angle(float angle)
{
    if(angle < inner_angle)
        angle = inner_angle;

    outer_angle = angle;
}

void light::spot::set_inner_angle(float angle)
{
    if(angle > outer_angle)
        angle = outer_angle;

    inner_angle = angle;
}

auto get_pcf_kernel(const light& l) -> pcf_kernel
{
    const float offset = std::max(l.shadow_params.pcf.x_offset, l.shadow_params.pcf.y_offset);
    const float radius_texels = std::max(get_pcf_radius_texels_per_offset(l) * offset, PCF_MIN_RADIUS_TEXELS);
    if(radius_texels < PCF_TENT3_MAX_RADIUS_TEXELS)
    {
        return pcf_kernel::tent3;
    }
    return radius_texels < PCF_TENT5_MAX_RADIUS_TEXELS ? pcf_kernel::tent5 : pcf_kernel::tent7;
}

void narrow_pcf_to_tent3(light& l)
{
    const float max_offset = PCF_TENT3_RADIUS_TEXELS / get_pcf_radius_texels_per_offset(l);
    l.shadow_params.pcf.x_offset = std::min(l.shadow_params.pcf.x_offset, max_offset);
    l.shadow_params.pcf.y_offset = std::min(l.shadow_params.pcf.y_offset, max_offset);
}
} // namespace unravel
