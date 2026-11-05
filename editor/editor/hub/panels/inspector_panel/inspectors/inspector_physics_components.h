#pragma once
#include "inspector.h"

#include <engine/physics/ecs/components/joint_component.h>
#include <engine/physics/ecs/components/ragdoll_component.h>
#include <engine/physics/ragdoll_builder.h>

namespace unravel
{

/**
 * @brief Ragdoll: reflected properties, what it holds and does, and Auto Setup from the humanoid skeleton
 *        (editor_actions::auto_setup_ragdoll).
 */
struct inspector_ragdoll_component : public crtp_meta_type<inspector_ragdoll_component, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;

private:
    /// The total mass field, the Auto Setup button and what the last Auto Setup made.
    auto draw_auto_setup(rtti::context& ctx, ragdoll_component& data) -> inspect_result;

    /// What the last Auto Setup made, and on which ragdoll: shown under its button for that one only.
    std::string auto_setup_report_;
    entt::handle auto_setup_owner_;
    float total_mass_{ragdoll_build_options{}.total_mass};
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
