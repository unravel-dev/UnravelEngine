#pragma once

#include "inspector.h"
#include <base/basetypes.hpp>

namespace unravel
{

struct inspector_range_float : public inspector
{
    REFLECTABLEV(inspector_range_float, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_range_float, range<float>)

struct inspector_range_double : public inspector
{
    REFLECTABLEV(inspector_range_double, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_range_double, range<double>)

struct inspector_range_int8 : public inspector
{
    REFLECTABLEV(inspector_range_int8, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_range_int8, range<int8_t>)

struct inspector_range_int16 : public inspector
{
    REFLECTABLEV(inspector_range_int16, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_range_int16, range<int16_t>)

struct inspector_range_int32 : public inspector
{
    REFLECTABLEV(inspector_range_int32, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_range_int32, range<int32_t>)

struct inspector_range_int64 : public inspector
{
    REFLECTABLEV(inspector_range_int64, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_range_int64, range<int64_t>)

struct inspector_range_uint8 : public inspector
{
    REFLECTABLEV(inspector_range_uint8, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_range_uint8, range<uint8_t>)

struct inspector_range_uint16 : public inspector
{
    REFLECTABLEV(inspector_range_uint16, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_range_uint16, range<uint16_t>)

struct inspector_range_uint32 : public inspector
{
    REFLECTABLEV(inspector_range_uint32, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_range_uint32, range<uint32_t>)

struct inspector_range_uint64 : public inspector
{
    REFLECTABLEV(inspector_range_uint64, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_range_uint64, range<uint64_t>)



struct inspector_size_float : public inspector
{
    REFLECTABLEV(inspector_size_float, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_size_float, size<float>)

struct inspector_size_double : public inspector
{
    REFLECTABLEV(inspector_size_double, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_size_double, size<double>)

struct inspector_size_int8 : public inspector
{
    REFLECTABLEV(inspector_size_int8, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_size_int8, size<int8_t>)

struct inspector_size_int16 : public inspector
{
    REFLECTABLEV(inspector_size_int16, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_size_int16, size<int16_t>)

struct inspector_size_int32 : public inspector
{
    REFLECTABLEV(inspector_size_int32, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_size_int32, size<int32_t>)

struct inspector_size_int64 : public inspector
{
    REFLECTABLEV(inspector_size_int64, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_size_int64, size<int64_t>)

struct inspector_size_uint8 : public inspector
{
    REFLECTABLEV(inspector_size_uint8, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_size_uint8, size<uint8_t>)

struct inspector_size_uint16 : public inspector
{
    REFLECTABLEV(inspector_size_uint16, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_size_uint16, size<uint16_t>)

struct inspector_size_uint32 : public inspector
{
    REFLECTABLEV(inspector_size_uint32, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_size_uint32, size<uint32_t>)

struct inspector_size_uint64 : public inspector
{
    REFLECTABLEV(inspector_size_uint64, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_size_uint64, size<uint64_t>)

} // namespace unravel
