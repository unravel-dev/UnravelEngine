#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/rendering/ecs/systems/rendering_system.h>

namespace unravel
{
namespace
{

void internal_m2n_gizmos_add_sphere(const math::color& color, const math::vec3& position, float radius)
{
    auto& ctx = engine::context();
    auto& path = ctx.get_cached<rendering_system>();
    path.add_debugdraw_call(
        [color, position, radius](gfx::dd_raii& dd)
        {
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(color);
            dd.encoder.setWireframe(true);

            bx::Sphere sphere;
            sphere.center.x = position.x;
            sphere.center.y = position.y;
            sphere.center.z = position.z;
            sphere.radius = radius;
            dd.encoder.draw(sphere);
        });
}

void internal_m2n_gizmos_add_ray(const math::color& color,
                                 const math::vec3& position,
                                 const math::vec3& direction,
                                 float max_distance)
{
    const float direction_length = math::length(direction);
    if(direction_length <= 0.0f)
    {
        return;
    }
    // The direction need not be normalized: the ray is max_distance long whatever its length.
    const math::vec3 end = position + direction * (max_distance / direction_length);
    auto& ctx = engine::context();
    auto& path = ctx.get_cached<rendering_system>();
    path.add_debugdraw_call(
        [color, position, end](gfx::dd_raii& dd)
        {
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(color);
            dd.encoder.setWireframe(true);
            dd.encoder.push();
            dd.encoder.moveTo(position.x, position.y, position.z);
            dd.encoder.lineTo(end.x, end.y, end.z);
            dd.encoder.pop();
        });
}

} // namespace

void register_gizmos_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.Gizmos");
    reg.add_internal_call("internal_m2n_gizmos_add_sphere", script_internal_call(internal_m2n_gizmos_add_sphere));
    reg.add_internal_call("internal_m2n_gizmos_add_ray", script_internal_call(internal_m2n_gizmos_add_ray));
}

} // namespace unravel
