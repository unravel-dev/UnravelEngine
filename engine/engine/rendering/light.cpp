#include "light.h"

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
} // namespace unravel
