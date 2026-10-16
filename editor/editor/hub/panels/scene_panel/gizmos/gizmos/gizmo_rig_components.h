#pragma once
#include "gizmo.h"

#include <engine/animation/ecs/components/ik_components.h>

#include <vector>

namespace unravel
{

/**
 * @brief Two-bone IK: the solved limb, its target and its pole.
 */
struct gizmo_two_bone_ik_component : public gizmo
{
    void draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d) override;
    void draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd) override;
};

/**
 * @brief Chain IK: the solved chain, its target and its pole.
 */
struct gizmo_chain_ik_component : public gizmo
{
    void draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d) override;
    void draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd) override;
};

/**
 * @brief Look-at: the turned chain, the line of sight and the per-bone turn limit.
 */
struct gizmo_look_at_ik_component : public gizmo
{
    void draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d) override;
    void draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd) override;
};

/**
 * @brief Foot IK: the ground probes, the planted foot goals, the legs and the pelvis drop.
 */
struct gizmo_foot_ik_component : public gizmo
{
    void draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d) override;
    void draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd) override;
};

/**
 * @brief Spring bones: the simulated chain, its joint radius and its colliders.
 */
struct gizmo_spring_bone_component : public gizmo
{
    void draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d) override;
    void draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd) override;
};

GIZMO_REFLECT(gizmo_two_bone_ik_component, two_bone_ik_component)
GIZMO_REFLECT(gizmo_chain_ik_component, chain_ik_component)
GIZMO_REFLECT(gizmo_look_at_ik_component, look_at_ik_component)
GIZMO_REFLECT(gizmo_foot_ik_component, foot_ik_component)
GIZMO_REFLECT(gizmo_spring_bone_component, spring_bone_component)

} // namespace unravel
