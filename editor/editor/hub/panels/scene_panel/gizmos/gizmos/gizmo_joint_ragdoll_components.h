#pragma once
#include "gizmo.h"

#include <engine/physics/ecs/components/joint_component.h>
#include <engine/physics/ecs/components/ragdoll_component.h>

namespace unravel
{

/**
 * @brief Joint: its frame and axis, the range its limits allow, and lines to the bodies it connects.
 */
struct gizmo_joint_component : public gizmo
{
    void draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d) override;
    void draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd) override;
};

/**
 * @brief Ragdoll: every body's shape on its bone and the axis of its joint, coloured by who moves it.
 */
struct gizmo_ragdoll_component : public gizmo
{
    void draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d) override;
    void draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd) override;
};

GIZMO_REFLECT(gizmo_joint_component, joint_component)
GIZMO_REFLECT(gizmo_ragdoll_component, ragdoll_component)

} // namespace unravel
