#pragma once
#include "inspector.h"

#include <engine/rendering/ecs/components/reflection_probe_component.h>

namespace unravel
{

struct inspector_reflection_probe_component
    : public crtp_meta_type<inspector_reflection_probe_component, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_reflection_probe_component, reflection_probe_component)

} // namespace unravel
