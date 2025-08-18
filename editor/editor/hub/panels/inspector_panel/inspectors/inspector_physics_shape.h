#pragma once
#include "inspector.h"

#include <engine/physics/ecs/components/physics_component.h>

namespace unravel
{

struct inspector_physics_compound_shape : public crtp_meta_type<inspector_physics_compound_shape, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_physics_compound_shape, physics_compound_shape)
} // namespace unravel
