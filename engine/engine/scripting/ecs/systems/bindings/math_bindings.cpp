#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

namespace unravel
{
namespace
{


auto internal_m2n_from_euler_rad(const math::vec3& euler) -> math::quat
{
    return {euler};
}

auto internal_m2n_to_euler_rad(const math::quat& euler) -> math::vec3
{
    return math::eulerAngles(euler);
}

auto internal_m2n_angle_axis(float angle, const math::vec3& axis) -> math::quat
{
    return math::angleAxis(angle, axis);
}

auto internal_m2n_look_rotation(const math::vec3& forward, const math::vec3& up) -> math::quat
{
    return math::look_rotation(forward, up);
}

auto internal_m2n_from_to_rotation(const math::vec3& from, const math::vec3& to) -> math::quat
{
    return math::from_to_rotation(from, to);
}

} // namespace

void register_math_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Quaternion");
    reg.add_internal_call("internal_m2n_from_euler_rad", dotnet_internal_call(internal_m2n_from_euler_rad));
    reg.add_internal_call("internal_m2n_to_euler_rad", dotnet_internal_call(internal_m2n_to_euler_rad));
    reg.add_internal_call("internal_m2n_from_to_rotation", dotnet_internal_call(internal_m2n_from_to_rotation));
    reg.add_internal_call("internal_m2n_angle_axis", dotnet_internal_call(internal_m2n_angle_axis));
    reg.add_internal_call("internal_m2n_look_rotation", dotnet_internal_call(internal_m2n_look_rotation));
}

} // namespace unravel
