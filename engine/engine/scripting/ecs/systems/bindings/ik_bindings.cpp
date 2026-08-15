#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/rendering/ecs/systems/ik_solvers.h>

namespace unravel
{
namespace
{

/**
 * @brief Builds solver params from the flattened scripting arguments.
 *
 * The C++ layer clamps these itself; doing it here as well would hide bad
 * script input rather than let the solver reject it consistently.
 */
auto make_solver_params(int max_iterations, float threshold, float weight) -> ik_solver_params
{
    ik_solver_params params;
    params.max_iterations = max_iterations;
    params.threshold = threshold;
    params.weight = weight;
    return params;
}

auto internal_m2n_utils_set_ik_position_ccd(entt::entity id,
                                            const math::vec3& target,
                                            const math::vec3& pole,
                                            int num_bones_in_chain,
                                            int max_iterations,
                                            float threshold,
                                            float weight) -> bool
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return false;
    }
    if(num_bones_in_chain <= 0)
    {
        return false;
    }
    return ik_set_position_ccd(e,
                               target,
                               ik_pole::from_legacy_point(pole),
                               size_t(num_bones_in_chain),
                               make_solver_params(max_iterations, threshold, weight))
        .applied;
}

auto internal_m2n_utils_set_ik_position_fabrik(entt::entity id,
                                               const math::vec3& target,
                                               const math::vec3& pole,
                                               int num_bones_in_chain,
                                               int max_iterations,
                                               float threshold,
                                               float weight) -> bool
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return false;
    }
    if(num_bones_in_chain <= 0)
    {
        return false;
    }
    return ik_set_position_fabrik(e,
                                  target,
                                  ik_pole::from_legacy_point(pole),
                                  size_t(num_bones_in_chain),
                                  make_solver_params(max_iterations, threshold, weight))
        .applied;
}

auto internal_m2n_utils_set_ik_position_two_bone(entt::entity id,
                                                 const math::vec3& target,
                                                 const math::vec3& pole,
                                                 float weight,
                                                 float soften) -> bool
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return false;
    }
    return ik_set_position_two_bone(e, target, ik_pole::from_legacy_point(pole), weight, soften).applied;
}

auto internal_m2n_utils_set_ik_rotation(entt::entity id, const math::quat& rotation, float weight) -> bool
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return false;
    }
    return ik_set_rotation(e, rotation, weight).applied;
}

auto internal_m2n_utils_set_ik_aim_position(entt::entity id,
                                            const math::vec3& target,
                                            const math::vec3& forward_axis,
                                            const math::vec3& up_axis,
                                            const math::vec3& world_up,
                                            float max_angle_radians,
                                            float weight) -> bool
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return false;
    }
    ik_aim_params params;
    params.forward_axis = forward_axis;
    params.up_axis = up_axis;
    params.world_up = world_up;
    params.max_angle_radians = max_angle_radians;
    params.weight = weight;
    return ik_aim_at_position(e, target, params).applied;
}

auto internal_m2n_utils_set_ik_look_at_position(entt::entity id, const math::vec3& target, float weight) -> bool
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return false;
    }
    return ik_look_at_position(e, target, weight).applied;
}

auto internal_m2n_utils_get_ik_bone_axis(entt::entity id) -> math::vec3
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return math::vec3(0.0f, 0.0f, 1.0f);
    }
    return ik_resolve_bone_axis_local(e);
}

auto internal_m2n_utils_get_ik_facing_axis(entt::entity id, const math::vec3& world_direction) -> math::vec3
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return math::vec3(0.0f, 0.0f, 1.0f);
    }
    return ik_resolve_facing_axis_local(e, world_direction);
}

} // namespace

void register_ik_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.IK");
    reg.add_internal_call("internal_m2n_utils_set_ik_position_ccd",
                          dotnet_internal_call(internal_m2n_utils_set_ik_position_ccd));
    reg.add_internal_call("internal_m2n_utils_set_ik_position_fabrik",
                          dotnet_internal_call(internal_m2n_utils_set_ik_position_fabrik));
    reg.add_internal_call("internal_m2n_utils_set_ik_position_two_bone",
                          dotnet_internal_call(internal_m2n_utils_set_ik_position_two_bone));

    reg.add_internal_call("internal_m2n_utils_set_ik_rotation",
                          dotnet_internal_call(internal_m2n_utils_set_ik_rotation));
    reg.add_internal_call("internal_m2n_utils_set_ik_aim_position",
                          dotnet_internal_call(internal_m2n_utils_set_ik_aim_position));
    reg.add_internal_call("internal_m2n_utils_set_ik_look_at_position",
                          dotnet_internal_call(internal_m2n_utils_set_ik_look_at_position));
    reg.add_internal_call("internal_m2n_utils_get_ik_bone_axis",
                          dotnet_internal_call(internal_m2n_utils_get_ik_bone_axis));
    reg.add_internal_call("internal_m2n_utils_get_ik_facing_axis",
                          dotnet_internal_call(internal_m2n_utils_get_ik_facing_axis));
}

} // namespace unravel
