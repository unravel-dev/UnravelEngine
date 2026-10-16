#pragma once
#include "inspector.h"

#include <engine/animation/ecs/components/ik_components.h>

namespace unravel
{

/**
 * @brief Two-bone IK: reflected properties, live solve status, and Create Target / Create Pole,
 * which place an entity at the hand or foot (in front of the elbow or knee) and assign it.
 */
struct inspector_two_bone_ik_component : public crtp_meta_type<inspector_two_bone_ik_component, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

/**
 * @brief Chain IK: reflected properties, live solve status and Create Target / Create Pole.
 */
struct inspector_chain_ik_component : public crtp_meta_type<inspector_chain_ik_component, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

/**
 * @brief Look-at: reflected properties, live weight and Create Target in front of the eyes.
 */
struct inspector_look_at_ik_component : public crtp_meta_type<inspector_look_at_ik_component, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

/**
 * @brief Foot IK: reflected properties, live leg status and Auto Setup, which finds the feet and
 * the hips of the character's skeleton by humanoid role.
 */
struct inspector_foot_ik_component : public crtp_meta_type<inspector_foot_ik_component, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;

private:
    /// What the last Auto Setup found, and on which character: shown under its button for that one only.
    std::string auto_setup_report_;
    entt::handle auto_setup_owner_;
};

/**
 * @brief Spring bones: reflected properties, the simulated chain and Reset Simulation.
 */
struct inspector_spring_bone_component : public crtp_meta_type<inspector_spring_bone_component, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_two_bone_ik_component, two_bone_ik_component)
REFLECT_INSPECTOR_INLINE(inspector_chain_ik_component, chain_ik_component)
REFLECT_INSPECTOR_INLINE(inspector_look_at_ik_component, look_at_ik_component)
REFLECT_INSPECTOR_INLINE(inspector_foot_ik_component, foot_ik_component)
REFLECT_INSPECTOR_INLINE(inspector_spring_bone_component, spring_bone_component)

} // namespace unravel
