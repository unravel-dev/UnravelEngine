#pragma once
#include "inspector.h"

#include <engine/scripting/ecs/components/script_component.h>

namespace unravel
{

struct inspector_mono_object : public crtp_meta_type<inspector_mono_object, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_mono_object, dotnet::object)

struct inspector_mono_object_pinned : public crtp_meta_type<inspector_mono_object_pinned, inspector_mono_object>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_mono_object_pinned, dotnet::object_pinned_ptr)

/// A key of a C# enum type in the editor copy of a dictionary: the value, and the enum type that
/// names it. Keys compare by value alone.
struct mono_enum_key
{
    std::int64_t value{};
    dotnet::type type{};

    friend auto operator<(const mono_enum_key& lhs, const mono_enum_key& rhs) -> bool
    {
        return lhs.value < rhs.value;
    }

    friend auto operator==(const mono_enum_key& lhs, const mono_enum_key& rhs) -> bool
    {
        return lhs.value == rhs.value;
    }
};

/// A combo of the names of the enum; a value the enum does not name shows as its number.
struct inspector_mono_enum_key : public crtp_meta_type<inspector_mono_enum_key, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_mono_enum_key, mono_enum_key)
} // namespace unravel
