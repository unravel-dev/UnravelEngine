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

/**
 * @brief How inspect_vector_value draws a vector: its first `width` components, as a color or as fields, dragged at
 * `drag_speed` per pixel (0: the inspector's speed) and kept between min_value and max_value while max_value is the
 * greater.
 */
struct vector_value_style
{
    uint32_t width{4};
    bool is_color{};
    /// A color may go above 1: light a surface gives off.
    bool is_hdr{};
    float drag_speed{};
    float min_value{};
    float max_value{};
};

/**
 * @brief Draws the first `width` components of @p value with the inspector's own widget for a float, a math::vec2 to
 * math::vec4 or a math::color, so a value kept in a vec4 (a material graph's) reads and drags as the inspector's rows
 * do. The result is the inspector's; nothing is recorded for undo.
 */
auto inspect_vector_value(rtti::context& ctx, math::vec4& value, const vector_value_style& style) -> inspect_result;

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
    void before_inspect(const entt::meta_data& prop, const entt::meta_any& object) override;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_transform, math::transform)
} // namespace unravel
