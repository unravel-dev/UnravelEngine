#pragma once
#include "inspector.h"

#include <math/math.h>

namespace unravel
{

struct inspector_bvec2 : public crtp_meta_type<inspector_bvec2, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_bvec2, math::bvec2)

struct inspector_bvec3 : public crtp_meta_type<inspector_bvec3, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_bvec3, math::bvec3)

struct inspector_bvec4 : public crtp_meta_type<inspector_bvec4, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_bvec4, math::bvec4)

struct inspector_vec2 : public crtp_meta_type<inspector_vec2, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_vec2, math::vec2)

struct inspector_vec3 : public crtp_meta_type<inspector_vec3, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_vec3, math::vec3)

struct inspector_vec4 : public crtp_meta_type<inspector_vec4, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_vec4, math::vec4)

struct inspector_color : public crtp_meta_type<inspector_color, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_color, math::color)

struct inspector_quaternion : public crtp_meta_type<inspector_quaternion, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_quaternion, math::quat)

struct inspector_bbox : public crtp_meta_type<inspector_bbox, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_bbox, math::bbox)

struct inspector_transform : public crtp_meta_type<inspector_transform, inspector>
{
    void before_inspect(const entt::meta_data& prop) override;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_transform, math::transform)
} // namespace unravel
