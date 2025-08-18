#pragma once
#include "inspector.h"

#include <engine/scripting/ecs/components/script_component.h>

namespace unravel
{

struct inspector_mono_object : public crtp_meta_type<inspector_mono_object, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_mono_object, mono::mono_object)

struct inspector_mono_scoped_object : public crtp_meta_type<inspector_mono_scoped_object, inspector_mono_object>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_mono_scoped_object, mono::mono_scoped_object)
} // namespace unravel
