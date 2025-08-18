#pragma once
#include "inspector.h"

#include <engine/ecs/components/prefab_component.h>

namespace unravel
{

struct inspector_prefab_component : public crtp_meta_type<inspector_prefab_component, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_prefab_component, prefab_component)

} // namespace unravel
