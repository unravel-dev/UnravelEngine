#pragma once
#include "gizmo.h"

#include <engine/physics/ecs/components/physics_component.h>

namespace unravel
{

struct gizmo_physics_component : public gizmo
{
    void draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d) override;
    void draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd) override;
};

GIZMO_REFLECT(gizmo_physics_component, physics_component)

} // namespace unravel
