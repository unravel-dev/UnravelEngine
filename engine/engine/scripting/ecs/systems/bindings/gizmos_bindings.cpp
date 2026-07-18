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
    auto& ctx = engine::context();
    auto& path = ctx.get_cached<rendering_system>();
    path.add_debugdraw_call(
        [color, position, direction, max_distance](gfx::dd_raii& dd)
        {
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(color);
            dd.encoder.setWireframe(true);

            bx::Ray ray;
            ray.pos.x = position.x;
            ray.pos.y = position.y;
            ray.pos.z = position.z;

            ray.dir.x = direction.x;
            ray.dir.y = direction.y;
            ray.dir.z = direction.z;

            dd.encoder.push();
            dd.encoder.moveTo(ray.pos);
            dd.encoder.lineTo(bx::mul(ray.dir, max_distance));
            dd.encoder.pop();
        });
}

} // namespace

void register_gizmos_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.Gizmos");
    reg.add_internal_call("internal_m2n_gizmos_add_sphere", dotnet_internal_call(internal_m2n_gizmos_add_sphere));
    reg.add_internal_call("internal_m2n_gizmos_add_ray", dotnet_internal_call(internal_m2n_gizmos_add_ray));
}

} // namespace unravel
