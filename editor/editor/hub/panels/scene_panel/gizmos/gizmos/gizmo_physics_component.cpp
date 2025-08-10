#include "gizmo_physics_component.h"
#include <engine/ecs/components/transform_component.h>
#include <engine/physics/gizmos/gizmos.h>
namespace unravel
{

void gizmo_physics_component::draw(rtti::context& ctx, rttr::variant& var, const camera& cam, gfx::dd_raii& dd)
{
    auto& data = *var.get_value<physics_component*>();

    auto owner = data.get_owner();
    const auto& transform = owner.get<transform_component>();
    const auto& shape = data.get_shapes();
    const auto& world_transform = transform.get_transform_global();

    DebugDrawEncoderScopePush scope(dd.encoder);
    uint32_t color = 0x8800ff00;

    if(!data.is_autoscaled())
    {
        auto trans = world_transform;
        trans.reset_scale();
        dd.encoder.pushTransform((const float*)trans);

    }
    else
    {
        dd.encoder.pushTransform((const float*)world_transform);

    }

    dd.encoder.setColor(color);
    dd.encoder.setWireframe(true);
    ::unravel::draw(dd.encoder, shape);
    dd.encoder.popTransform();
}

void gizmo_physics_component::draw_billboard(rtti::context& ctx,
                                             rttr::variant& var,
                                             const camera& cam,
                                             gfx::dd_raii& dd)
{
}
} // namespace unravel
