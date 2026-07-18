#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/rendering/ecs/systems/ik_solvers.h>

namespace unravel
{
namespace
{

void internal_m2n_utils_set_ik_posiiton_ccd(entt::entity id,
                                            const math::vec3& target,
                                            const math::vec3& pole,
                                            int num_bones_in_chain,
                                            int max_iterations,
                                            float threshold)
{
    auto e = get_entity_from_id(id);

    ik_set_position_ccd(e, target, pole, num_bones_in_chain, max_iterations, threshold);
}

void internal_m2n_utils_set_ik_posiiton_fabrik(entt::entity id,
                                               const math::vec3& target,
                                               const math::vec3& pole,
                                               int num_bones_in_chain,
                                               int max_iterations,
                                               float threshold)
{
    auto e = get_entity_from_id(id);

    ik_set_position_fabrik(e, target, pole, num_bones_in_chain, max_iterations, threshold);
}

void internal_m2n_utils_set_ik_posiiton_two_bone(entt::entity id,
                                                 const math::vec3& target,
                                                 const math::vec3& pole,
                                                 float weight,
                                                 float soften)
{
    auto e = get_entity_from_id(id);

    ik_set_position_two_bone(e, target, pole, weight, soften);
}

void internal_m2n_utils_set_ik_look_at_posiiton(entt::entity id, const math::vec3& target, float weight)
{
    auto e = get_entity_from_id(id);

    ik_look_at_position(e, target, weight);
}

} // namespace

void register_ik_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.IK");
    reg.add_internal_call("internal_m2n_utils_set_ik_posiiton_ccd",
                            dotnet_internal_call(internal_m2n_utils_set_ik_posiiton_ccd));
    reg.add_internal_call("internal_m2n_utils_set_ik_posiiton_fabrik",
                            dotnet_internal_call(internal_m2n_utils_set_ik_posiiton_fabrik));
    reg.add_internal_call("internal_m2n_utils_set_ik_posiiton_two_bone",
                            dotnet_internal_call(internal_m2n_utils_set_ik_posiiton_two_bone));

    reg.add_internal_call("internal_m2n_utils_set_ik_look_at_posiiton",
                            dotnet_internal_call(internal_m2n_utils_set_ik_look_at_posiiton));
}

} // namespace unravel
