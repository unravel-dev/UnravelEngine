#include "gizmo_character_controller_component.h"
#include <engine/ecs/components/transform_component.h>
#include <engine/physics/gizmos/gizmos.h>

namespace unravel
{

void gizmo_character_controller_component::draw(rtti::context& ctx,
                                                entt::meta_any& var,
                                                const camera& cam,
                                                gfx::dd_raii& dd,
                                                dd_2d_raii& dd_2d)
{
    entt::as_derived(var);
    auto& data = var.cast<character_controller_component&>();
    auto owner = data.get_owner();
    if(!owner || !owner.all_of<transform_component>())
    {
        return;
    }
    const auto& transform = owner.get<transform_component>();
    const auto& world_transform = transform.get_transform_global();
    float cylinder_half_height = (data.get_height() - 2.0f * data.get_radius()) * 0.5f;
    if(cylinder_half_height < 0.0f)
    {
        cylinder_half_height = 0.0f;
    }
    math::vec3 capsule_center = data.get_center();
    math::vec3 up{0.0f, 1.0f, 0.0f};
    auto top = capsule_center + up * cylinder_half_height;
    auto bottom = capsule_center - up * cylinder_half_height;
    DebugDrawEncoderScopePush scope(dd.encoder);
    auto trans = world_transform;
    trans.reset_scale();
    dd.encoder.pushTransform((const float*)trans);
    dd.encoder.setColor(0x8800ffff);
    dd.encoder.setWireframe(true);
    dd.encoder.drawCapsule(to_bx(bottom), to_bx(top), data.get_radius());
    dd.encoder.popTransform();
}

void gizmo_character_controller_component::draw_billboard(rtti::context& ctx,
                                                          entt::meta_any& var,
                                                          const camera& cam,
                                                          gfx::dd_raii& dd)
{
}

} // namespace unravel
