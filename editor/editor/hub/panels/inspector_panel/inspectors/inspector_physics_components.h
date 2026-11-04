#pragma once
#include "inspector.h"

#include <engine/physics/ecs/components/joint_component.h>
#include <engine/physics/ecs/components/ragdoll_component.h>
#include <engine/physics/ragdoll_builder.h>

namespace unravel
{

/**
 * @brief Gives the ragdoll on @p owner the bodies build_humanoid_ragdoll makes for its model, as one undo step. On a
 *        prefab instance the changed fields become overrides. The inspector's Auto Setup and the MCP tool both call it.
 */
auto auto_setup_ragdoll(rtti::context& ctx, entt::handle owner, const ragdoll_build_options& options) -> ragdoll_build_result;

/**
 * @brief Ragdoll: reflected properties, what it holds and does, and Auto Setup from the humanoid skeleton.
 */
struct inspector_ragdoll_component : public crtp_meta_type<inspector_ragdoll_component, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;

private:
    /// What the last Auto Setup made, and on which ragdoll: shown under its button for that one only.
    std::string auto_setup_report_;
    entt::handle auto_setup_owner_;
    float total_mass_{70.0f};
};

/**
 * @brief Joint: reflected properties and, while playing, what the simulation reports about it.
 */
struct inspector_joint_component : public crtp_meta_type<inspector_joint_component, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_ragdoll_component, ragdoll_component)
REFLECT_INSPECTOR_INLINE(inspector_joint_component, joint_component)

} // namespace unravel
