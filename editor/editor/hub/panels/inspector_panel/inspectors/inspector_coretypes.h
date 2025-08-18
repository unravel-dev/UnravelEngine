#pragma once

#include "inspector.h"
#include <chrono>
#include <filesystem/filesystem.h>
#include <string>
#include <uuid/uuid.h>

namespace unravel
{
struct inspector_bool : public crtp_meta_type<inspector_bool, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_bool, bool)

struct inspector_float : public crtp_meta_type<inspector_float, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_float, float)

struct inspector_double : public crtp_meta_type<inspector_double, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_double, double)

struct inspector_int8 : public crtp_meta_type<inspector_int8, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_int8, std::int8_t)

struct inspector_int16 : public crtp_meta_type<inspector_int16, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_int16, std::int16_t)

struct inspector_int32 : public crtp_meta_type<inspector_int32, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_int32, std::int32_t)

struct inspector_int64 : public crtp_meta_type<inspector_int64, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_int64, std::int64_t)

struct inspector_uint8 : public crtp_meta_type<inspector_uint8, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_uint8, std::uint8_t)

struct inspector_uint16 : public crtp_meta_type<inspector_uint16, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_uint16, std::uint16_t)

struct inspector_uint32 : public crtp_meta_type<inspector_uint32, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_uint32, std::uint32_t)

struct inspector_uint64 : public crtp_meta_type<inspector_uint64, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_uint64, std::uint64_t)
struct inspector_string : public crtp_meta_type<inspector_string, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_string, std::string)

struct inspector_path : public crtp_meta_type<inspector_path, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_path, fs::path)

struct inspector_duration_sec_float : public crtp_meta_type<inspector_duration_sec_float, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_duration_sec_float, std::chrono::duration<float>)

struct inspector_duration_sec_double : public crtp_meta_type<inspector_duration_sec_double, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_duration_sec_double, std::chrono::duration<double>)

struct inspector_uuid : public crtp_meta_type<inspector_uuid, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom)
        -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_uuid, hpp::uuid)
} // namespace unravel
